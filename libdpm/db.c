/*
 * Copyright (C) 2008 Marius Vollmer <marius.vollmer@gmail.com>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 */

#define _GNU_SOURCE

#include <time.h>
#include <ctype.h>

#include "dyn.h"
#include "store.h"
#include "db.h"
#include "parse.h"
#include "acq.h"

dyn_var dpm_database_name[1];

/* The root:

   - format              (string, "dpm-0")
   - strings             (string table)
   - packages            (package -> package, weak key)
   - installed version   (package -> something, maybe version, strong)
   - origin_available    (origin -> (package -> versions, strong), strong)
   - tags                (tag -> versions)
   - reverse_relations   (package -> list of versions, weak sets)

   A package:

   - id                  (small integer)
   - name                (interned string)

   A version:

   - id                  (small integer)
   - package             (package)
   - version             (string)
   - architecture        (interned string)
   - relations           (relation record, could be inlined)
   - tags                (list of strings)
   - shortdesc           (string)
   - fields              (field name -> string)

   A relation record:

   - pre-depends
   - depends
   - conflicts
   - provides
   - replaces
   - breaks
   - recommends
   - enhances
   - suggests

   A relation:

   - op                  (int)
   - package             (interned string)
   - version             (string)
   [ repeat for each additional alternative ]

   A package index:

   - path                (string, "http://ftp.fi.debian.org/.../Packages.bz2")
   - release             (release index or null)
   - versions            (list of versions)

   A release index

   - path                (string, "http://ftp.fi.debian.org/.../Release")
   - dist                (interned string, "stable")
   - valid keys          (list of strings, fingerprints)

   All strings used as dictionary keys are interned, of course.
   
   Control field values are interned as well, to save space.  Lot's of
   values are duplicated between different architectures and versions.
*/

struct dpm_db_struct {
  ss_store store;
  
  ss_tab *strings;
  ss_dict *packages;
  ss_dict *installed;
  ss_dict *origin_available;
  ss_dict *tags;
  ss_dict *reverse_rels;
};

static void
dpm_db_abort (struct dpm_db_struct *db)
{
  if (db->strings)
    ss_tab_abort (db->strings);
  if (db->packages)
    ss_dict_abort (db->packages);
  if (db->installed)
    ss_dict_abort (db->installed);
  if (db->origin_available)
    ss_dict_abort (db->origin_available);
  if (db->tags)
    ss_dict_abort (db->tags);
  if (db->reverse_rels)
    ss_dict_abort (db->reverse_rels);

  db->strings = NULL;
  db->packages = NULL;
  db->installed = NULL;
  db->origin_available = NULL;
  db->tags = NULL;
  db->reverse_rels = NULL;
}

static void
dpm_db_unref (dyn_type *type, void *object)
{
  struct dpm_db_struct *db = object;

  dpm_db_abort (db);
  dyn_unref (db->store);
}

static int
dpm_db_equal (void *a, void *b)
{
  return 0;
}

DYN_DEFINE_TYPE (dpm_db, "db");

static dyn_val
dpm_db_make (ss_store store)
{
  dpm_db db = dyn_new (dpm_db);
  db->store = dyn_ref (store);
  db->strings = NULL;
  db->packages = NULL;
  db->installed = NULL;
  db->origin_available = NULL;
  db->tags = NULL;
  db->reverse_rels = NULL;
  return db;
}

static dyn_var cur_db[1];

dpm_db
dpm_db_current ()
{
  return dyn_get (cur_db);
}

void
dpm_db_open ()
{
  dyn_val name = dyn_get (dpm_database_name);

  if (name == NULL)
    dyn_error ("dpm_database_name not set");

  dpm_db db = dpm_db_make (ss_open (name, SS_WRITE));
  dyn_let (cur_db, db);

  ss_val root = ss_get_root (db->store);

  if (root && !ss_streq (ss_ref_safely (root, 0), "dpm-0"))
    dyn_error ("%s is not a dpm database", name);

  db->strings =
    ss_tab_init (db->store, ss_ref_safely (root, 1));
  db->packages =
    ss_dict_init (db->store, ss_ref_safely (root, 2), SS_DICT_WEAK_KEYS);
  db->installed =
    ss_dict_init (db->store, ss_ref_safely (root, 3), SS_DICT_STRONG);
  db->origin_available =
    ss_dict_init (db->store, ss_ref_safely (root, 4), SS_DICT_STRONG);
  db->tags =
    ss_dict_init (db->store, ss_ref_safely (root, 5), SS_DICT_WEAK_SETS);
  db->reverse_rels =
    ss_dict_init (db->store, ss_ref_safely (root, 6), SS_DICT_WEAK_SETS);
}

void
dpm_db_checkpoint ()
{
  dpm_db db = dyn_get (cur_db);

  ss_val root = ss_new (db->store, 0, 7,
			ss_blob_new (db->store, 5, "dpm-0"),
			ss_tab_store (db->strings), 
			ss_dict_store (db->packages),
			ss_dict_store (db->installed),
			ss_dict_store (db->origin_available),
			ss_dict_store (db->tags),
			ss_dict_store (db->reverse_rels));
  ss_set_root (db->store, root);
}

void
dpm_db_done ()
{
  dpm_db db = dyn_get (cur_db);

  dpm_db_abort (db);
  ss_maybe_gc (db->store);
  dyn_unref (db->store);
  db->store = NULL;

  dyn_set (cur_db, NULL);
}

int
dpm_db_package_max_id ()
{
  dpm_db db = dyn_get (cur_db);
  return ss_tag_count (db->store, 65);
}

int
dpm_db_version_max_id ()
{
  dpm_db db = dyn_get (cur_db);
  return ss_tag_count (db->store, 64);
}

/* Strings 
 */

static ss_val
intern_soft (dpm_db db, const char *string)
{
  return ss_tab_intern_soft (db->strings, strlen (string), (void *)string);
}

ss_val 
dpm_db_intern (const char *string)
{
  return intern_soft (dyn_get (cur_db), string);
}

static ss_val
intern (dpm_db db, const char *string)
{
  return ss_tab_intern_blob (db->strings, strlen (string), (void *)string);
}

/* Basic accessors
 */

static dpm_package
find_create_package (dpm_db db, const char *name, int len)
{
  ss_val interned_name = ss_tab_intern_blob (db->strings, len, (void *)name);
  dpm_package pkg = ss_dict_get (db->packages, interned_name);
  if (pkg == NULL)
    {
      pkg = ss_new (db->store, 65, 2,
		    NULL,
		    interned_name);
      ss_dict_set (db->packages, interned_name, pkg);
    }
  return pkg;
}

dpm_package
dpm_db_find_package (const char *name)
{
  dpm_db db = dyn_get (cur_db);
  ss_val interned_name = intern_soft (db, name);
  if (interned_name)
    return ss_dict_get (db->packages, interned_name);
  else
    return NULL;
}

dpm_version
dpm_db_installed (dpm_package pkg)
{
  dpm_db db = dyn_get (cur_db);

  return ss_dict_get (db->installed, pkg);
}

void
dpm_db_set_installed (dpm_package pkg, dpm_version ver)
{
  dpm_db db = dyn_get (cur_db);

  return ss_dict_set (db->installed, pkg, ver);
}

/* Inserting
 */

dpm_origin
dpm_db_origin_find (const char *label)
{
  return intern (dyn_get (cur_db), label);
}

typedef struct {
  dpm_db db;

  ss_val package_key;
  ss_val version_key;
  ss_val architecture_key;
  ss_val description_key;
  ss_val tag_key;

  ss_val pre_depends_key;
  ss_val depends_key;
  ss_val conflicts_key;
  ss_val provides_key;
  ss_val replaces_key;
  ss_val breaks_key;
  ss_val recommends_key;
  ss_val enhances_key;
  ss_val suggests_key;

  dpm_origin origin;
  ss_dict *available;

  dpm_package package;

  int new_versions;
} update_data;

ss_val
parse_relations (update_data *ud, const char *value, int value_len)
{
  dyn_input in = dyn_open_string (value, value_len);

  int n_relations = 0;
  ss_val relations[2048];

  while (true)
    {
      int n_alternatives = 0;
      ss_val alternatives[3*64];

      dyn_foreach_iter (alt, dpm_parse_relation_alternatives, in)
	{
	  int op_code;
	  if (alt.op == NULL)
	    op_code = DPM_ANY;
	  else if (alt.op_len == 1 && !strncmp (alt.op, "=", 1))
	    op_code = DPM_EQ;
	  else if (alt.op_len == 2 && !strncmp (alt.op, "<<", 2))
	    op_code = DPM_LESS;
	  else if (alt.op_len == 2 && !strncmp (alt.op, "<=", 2))
	    op_code = DPM_LESSEQ;
	  else if (alt.op_len == 2 && !strncmp (alt.op, ">>", 2))
	    op_code = DPM_GREATER;
	  else if (alt.op_len == 2 && !strncmp (alt.op, ">=", 2))
	    op_code = DPM_GREATEREQ;
	  else if (alt.op_len == 1 && !strncmp (alt.op, "<", 1))
	    op_code = DPM_LESSEQ;    // sic
	  else if (alt.op_len == 1 && !strncmp (alt.op, ">", 1))
	    op_code = DPM_GREATEREQ; // sic
	  else
	    dyn_error ("Unknown relation operator: %B", alt.op, alt.op_len);

	  if (n_alternatives >= 3*64)
	    dyn_error ("Too many alternatives: %r", dpm_pkg_name (ud->package));

	  int i = n_alternatives;
	  alternatives[i++] = ss_from_int (op_code);
	  alternatives[i++] = find_create_package (ud->db, 
						   alt.name, alt.name_len);
	  alternatives[i++] = (alt.version
			       ? ss_tab_intern_blob (ud->db->strings,
						     alt.version_len,
						     (void *)alt.version)
			       : NULL);
	  n_alternatives = i;
	}

      if (n_alternatives > 0)
	{
	  if (n_relations >= 2048)
	    dyn_error ("Too many relations: %r", dpm_pkg_name (ud->package));
      
	  relations[n_relations++] = ss_newv (ud->db->store, 0,
					      n_alternatives, 
					      alternatives);
	}

      if (!dpm_parse_next_relation (in))
	break;
    }

  return ss_newv (ud->db->store, 0, n_relations, relations);
}

static void
record_version (update_data *ud, dpm_version ver)
{
  dpm_package pkg = dpm_ver_package (ver);
  ss_val rels_rec = dpm_ver_relations (ver);
  ss_val tags = dpm_ver_tags (ver);

  ss_dict_add (ud->available, pkg, ver);

  for (int i = 0; i < ss_len (rels_rec); i++)
    {
      ss_val rels = ss_ref (rels_rec, i);
      if (rels)
	for (int j = 0; j < ss_len (rels); j++)
	  {
	    ss_val rel = ss_ref (rels, j);
	    for (int k = 0; k < ss_len (rel); k += 3)
	      ss_dict_add (ud->db->reverse_rels, ss_ref (rel, k+1), ver);
	  }
    }

  if (tags)
    for (int i = 0; i < ss_len (tags); i++)
      ss_dict_add (ud->db->tags, ss_ref (tags, i), ver);
}

static bool
parse_package_stanza (update_data *ud, dyn_input in)
{
  dpm_db db = ud->db;

  ud->package = NULL;

  ss_val version = NULL;
  ss_val architecture = NULL;
  ss_val shortdesc = NULL;

  ss_val pre_depends = NULL;
  ss_val depends = NULL;
  ss_val conflicts = NULL;
  ss_val provides = NULL;
  ss_val replaces = NULL;
  ss_val breaks = NULL;
  ss_val recommends = NULL;
  ss_val enhances = NULL;
  ss_val suggests = NULL;

  int n_tags = 0;
  ss_val tags[64];

  int n_fields = 0;
  ss_val fields[64];

  if (!dpm_parse_looking_at_control (in))
    return false;

  dyn_foreach_iter (f, dpm_parse_control_fields, in)
    {
      ss_val key = ss_tab_intern_blob (db->strings,
				       f.name_len, (void *)f.name);

      if (key == ud->tag_key)
	{
	  dyn_block
	    {
	      /* XXX - tags have a more complicated syntax than comma
		       separated fields.
	      */
	      dyn_input t = dyn_open_string (f.value, f.value_len);
	      dyn_foreach_iter (f, dpm_parse_comma_fields, t)
		{
		  if (n_tags >= 64)
		    dyn_error ("Too many tags");
		  
		  tags[n_tags++] =
		    ss_tab_intern_blob (ud->db->strings,
					f.len, (void *)f.field);
		}
	    }
	}
      else if (key == ud->pre_depends_key)
	pre_depends = parse_relations (ud, f.value, f.value_len);
      else if (key == ud->depends_key)
	depends = parse_relations (ud, f.value, f.value_len);
      else if (key == ud->conflicts_key)
	conflicts = parse_relations (ud, f.value, f.value_len);
      else if (key == ud->provides_key)
	provides = parse_relations (ud, f.value, f.value_len);
      else if (key == ud->replaces_key)
	replaces = parse_relations (ud, f.value, f.value_len);
      else if (key == ud->breaks_key)
	breaks = parse_relations (ud, f.value, f.value_len);
      else if (key == ud->recommends_key)
	recommends = parse_relations (ud, f.value, f.value_len);
      else if (key == ud->enhances_key)
	enhances = parse_relations (ud, f.value, f.value_len);
      else if (key == ud->suggests_key)
	suggests = parse_relations (ud, f.value, f.value_len);
      else
	{
	  ss_val val = ss_tab_intern_blob (db->strings,
					   f.value_len, (void *)f.value);
	  
	  if (key == ud->package_key)
	    {
	      dpm_package pkg = ss_dict_get (db->packages, val);
	      if (pkg == NULL)
		{
		  pkg = ss_new (db->store, 65, 2,
				NULL,
				val);
		  ss_dict_set (db->packages, val, pkg);
		}
	      ud->package = pkg;
	    }
	  else if (key == ud->version_key)
	    version = val;
	  else if (key == ud->architecture_key)
	    architecture = val;
	  else
	    {
	      fields[n_fields] = key;
	      fields[n_fields+1] = val;
	      n_fields += 2;
	      if (n_fields > 63)
		dyn_error ("too many fields");
	    }
      
	  if (key == ud->description_key)
	    {
	      char *desc = ss_blob_start (val);
	      char *pos = memchr (desc, '\n', ss_len (val));
	      if (pos)
		shortdesc = ss_tab_intern_blob (db->strings,
						pos-desc, desc);
	      else
		shortdesc = val;
	    }
	}
    }

  if (ud->package == NULL)
    dyn_error ("Stanza without package");
  if (version == NULL)
    dyn_error ("Package without version: %r",
	       dpm_pkg_name (ud->package));
  if (architecture == NULL)
    dyn_error ("Package without architecture: %r",
	       dpm_pkg_name (ud->package));

  ss_val ver = ss_new (db->store, 64, 9,
		       NULL,
		       ud->package,
		       version,
		       architecture,
		       ss_new (db->store, 0, 9,
			       pre_depends,
			       depends,
			       conflicts,
			       provides,
			       replaces,
			       breaks,
			       recommends,
			       enhances,
			       suggests),
		       ss_newv (db->store, 0,
				n_tags, tags),
		       shortdesc,
		       ss_newv (db->store, 0,
				n_fields, fields),
                       ud->origin);
  
  record_version (ud, ver);
  ud->new_versions++;
  return true;
}

void
dpm_db_origin_update (dpm_origin origin,
		      dyn_input in,
		      bool reset)
{
  update_data ud;
  ud.db = dyn_get (cur_db);

  ud.package_key = intern (ud.db, "Package");
  ud.version_key = intern (ud.db, "Version");
  ud.architecture_key = intern (ud.db, "Architecture");
  ud.description_key = intern (ud.db, "Description");
  ud.tag_key = intern (ud.db, "Tag");

  ud.pre_depends_key = intern (ud.db, "Pre-Depends");
  ud.depends_key = intern (ud.db, "Depends");
  ud.conflicts_key = intern (ud.db, "Conflicts");
  ud.provides_key = intern (ud.db, "Provides");
  ud.replaces_key = intern (ud.db, "Replaces");
  ud.breaks_key = intern (ud.db, "Breaks");
  ud.recommends_key = intern (ud.db, "Recommends");
  ud.enhances_key = intern (ud.db, "Enhances");
  ud.suggests_key = intern (ud.db, "Suggests");

  ud.origin = origin;
  ud.available =
    ss_dict_init (ud.db->store,
		  (reset 
		   ? NULL
		   : ss_dict_get (ud.db->origin_available, origin)),
		  SS_DICT_STRONG);
  ud.new_versions = 0;

  while (parse_package_stanza (&ud, in))
    ;

  ss_dict_set (ud.db->origin_available, origin,
	       ss_dict_finish (ud.available));
}

/* Version comparison
 *
 * Lifted from apt.
 */

#define order(x) ((x) == '~' ? -1    \
		: isdigit((x)) ? 0   \
		: !(x) ? 0           \
		: isalpha((x)) ? (x) \
		: (x) + 256)

static int
compare_fragment (const char *A, const char *AEnd,
		  const char *B, const char *BEnd)
{
  if (A >= AEnd && B >= BEnd)
    return 0;
  if (A >= AEnd)
    {
      if (*B == '~') return 1;
      return -1;
    }
  if (B >= BEnd)
    {
      if (*A == '~') return -1;
      return 1;
    }

   /* Iterate over the whole string
      What this does is to split the whole string into groups of
      numeric and non numeric portions. For instance:
         a67bhgs89
      Has 4 portions 'a', '67', 'bhgs', '89'. A more normal:
         2.7.2-linux-1
      Has '2', '.', '7', '.' ,'-linux-','1' 
   */

   const char *lhs = A;
   const char *rhs = B;
   while (lhs != AEnd && rhs != BEnd)
     {
       int first_diff = 0;

       while (lhs != AEnd && rhs != BEnd &&
	      (!isdigit(*lhs) || !isdigit(*rhs)))
	 {
	   int vc = order(*lhs);
	   int rc = order(*rhs);
	   if (vc != rc)
	     return vc - rc;
	   lhs++; rhs++;
	 }

       while (*lhs == '0')
	 lhs++;
       while (*rhs == '0')
	 rhs++;
       while (isdigit(*lhs) && isdigit(*rhs))
	 {
	   if (!first_diff)
	     first_diff = *lhs - *rhs;
	   lhs++;
	   rhs++;
	 }
       
       if (isdigit(*lhs))
	 return 1;
       if (isdigit(*rhs))
	 return -1;
       if (first_diff)
	 return first_diff;
     }

   // The strings must be equal
   if (lhs == AEnd && rhs == BEnd)
     return 0;

   // lhs is shorter
   if (lhs == AEnd)
     {
       if (*rhs == '~') return 1;
       return -1;
     }

   // rhs is shorter
   if (rhs == BEnd)
     {
       if (*lhs == '~') return -1;
       return 1;
     }

   // Shouldnt happen
   return 1;
}
									/*}}}*/
int
dpm_db_compare_versions (ss_val a, ss_val b)
{
  const char *A = ss_blob_start (a);
  const char *AEnd = A + ss_len (a);
  const char *B = ss_blob_start (b);
  const char *BEnd = B + ss_len (b);

  // Strip off the epoch and compare it 
  const char *lhs = A;
  const char *rhs = B;
  for (;lhs != AEnd && *lhs != ':'; lhs++);
  for (;rhs != BEnd && *rhs != ':'; rhs++);
  if (lhs == AEnd)
    lhs = A;
  if (rhs == BEnd)
    rhs = B;
  
  // Special case: a zero epoch is the same as no epoch,
  // so remove it.
  if (lhs != A)
    {
      for (; *A == '0'; ++A);
      if (A == lhs)
	{
	  ++A;
	  ++lhs;
	}
    }
  if (rhs != B)
    {
      for (; *B == '0'; ++B);
      if (B == rhs)
	{
	  ++B;
	  ++rhs;
	}
    }

  // Compare the epoch
  int Res = compare_fragment(A,lhs,B,rhs);
  if (Res != 0)
    return Res;
  
  // Skip the :
  if (lhs != A)
    lhs++;
  if (rhs != B)
    rhs++;
  
  // Find the last - 
  const char *dlhs = AEnd-1;
  const char *drhs = BEnd-1;
  for (;dlhs > lhs && *dlhs != '-'; dlhs--);
  for (;drhs > rhs && *drhs != '-'; drhs--);
  
  if (dlhs == lhs)
    dlhs = AEnd;
  if (drhs == rhs)
    drhs = BEnd;
  
  // Compare the main version
  Res = compare_fragment(lhs,dlhs,rhs,drhs);
  if (Res != 0)
    return Res;
  
  // Skip the -
  if (dlhs != lhs)
    dlhs++;
  if (drhs != rhs)
    drhs++;
  
  return compare_fragment(dlhs,AEnd,drhs,BEnd);
}

static const char *relname[] = {
  [DPM_EQ] = "=",
  [DPM_LESS] = "<<",
  [DPM_LESSEQ] = "<=",
  [DPM_GREATER] = ">>",
  [DPM_GREATEREQ] = ">="
};

int
dpm_db_check_versions (ss_val a, int op, ss_val b)
{
  if (op == DPM_ANY)
    return a != NULL;

  int r = dpm_db_compare_versions (a, b);
  // dyn_print ("Comparing %r %s %r == %d\n", a, relname[op], b, r);

  switch (op) {
  case DPM_EQ:
    return r == 0;
  case DPM_LESS:
    return r < 0;
  case DPM_LESSEQ:
    return r <= 0;
  case DPM_GREATER:
    return r > 0;
  case DPM_GREATEREQ:
    return r >= 0;
  default:
    abort ();
  }
}

/* Accessors
 */

ss_val
dpm_db_version_get (dpm_version ver, const char *field)
{
  ss_val fields = dpm_ver_fields (ver);
  if (fields)
    for (int i = 0; i < ss_len (fields); i += 2)
      if (ss_streq (ss_ref (fields, i), field))
	return ss_ref (fields, i+1);
  return NULL;
}

ss_val
dpm_db_query_tag (const char *tag)
{
  dpm_db db = dyn_get (cur_db);

  ss_val interned_tag = intern_soft (db, tag);
  if (interned_tag)
    return ss_dict_get (db->tags, interned_tag);
  else
    return NULL;
}

ss_val
dpm_db_reverse_relations (dpm_package pkg)
{
  dpm_db db = dyn_get (cur_db);

  return ss_dict_get (db->reverse_rels, pkg);
}

void
dpm_db_origins_init (dpm_db_origins *iter)
{
  iter->db = dyn_ref (dyn_get (cur_db));
  ss_dict_entries_init (&iter->origins, iter->db->origin_available);
  iter->origin = iter->origins.key;
}

void
dpm_db_origins_fini (dpm_db_origins *iter)
{
  ss_dict_entries_fini (&iter->origins);
  dyn_unref (iter->db);
}

void
dpm_db_origins_step (dpm_db_origins *iter)
{
  ss_dict_entries_step (&iter->origins);
  iter->origin = iter->origins.key;
}

bool
dpm_db_origins_done (dpm_db_origins *iter)
{
  return ss_dict_entries_done (&iter->origins);
}

dpm_origin
dpm_db_origins_elt (dpm_db_origins *iter)
{
  return iter->origin;
}

void
dpm_db_origin_packages_init (dpm_db_origin_packages *iter, dpm_origin origin)
{
  iter->db = dyn_ref (dyn_get (cur_db));
  iter->dict = ss_dict_init (iter->db->store, 
                             ss_dict_get (iter->db->origin_available, origin),
                             SS_DICT_STRONG);
  ss_dict_entries_init (&iter->packages, iter->dict);
  iter->package = iter->packages.key;
  iter->versions = iter->packages.val;
}

void
dpm_db_origin_packages_fini (dpm_db_origin_packages *iter)
{
  ss_dict_entries_fini (&iter->packages);
  ss_dict_finish (iter->dict);
  dyn_unref (iter->db);
}

void
dpm_db_origin_packages_step (dpm_db_origin_packages *iter)
{
  ss_dict_entries_step (&iter->packages);
  iter->package = iter->packages.key;
  iter->versions = iter->packages.val;
}

bool
dpm_db_origin_packages_done (dpm_db_origin_packages *iter)
{
  return ss_dict_entries_done (&iter->packages);
}

dpm_origin
dpm_db_origin_packages_elt (dpm_db_origin_packages *iter)
{
  return iter->package;
}

static void
show_relation (ss_val rel)
{
  for (int i = 0; i < ss_len (rel); i += 3)
    {
      int op = dpm_rel_op (rel, i);
      if (i > 0)
	dyn_print (" | ");
      dyn_print ("%r", dpm_pkg_name (dpm_rel_package (rel, i)));
      if (op != DPM_ANY)
	dyn_print (" (%s %r)", relname[op], dpm_rel_version (rel, i));
    }
}

static void
show_relations (const char *field, ss_val rels)
{
  if (rels)
    {
      dyn_print ("%s: ", field);
      for (int i = 0; i < ss_len (rels); i++)
	{
	  if (i > 0)
	    dyn_print (", ");
	  show_relation (ss_ref (rels, i));
	}
      dyn_print ("\n");
    }
}

void
dpm_db_show_version (dpm_version ver)
{
  dyn_print ("Package: %r\n", dpm_pkg_name (dpm_ver_package (ver)));
  dyn_print ("Version: %r\n", dpm_ver_version (ver));
  dyn_print ("Architecture: %r\n", dpm_ver_architecture (ver));

  ss_val relations = dpm_ver_relations (ver);

  show_relations ("Pre-Depends", ss_ref (relations, 0));
  show_relations ("Depends", ss_ref (relations, 1));
  show_relations ("Conflicts", ss_ref (relations, 2));
  show_relations ("Provides", ss_ref (relations, 3));
  show_relations ("Replaces", ss_ref (relations, 4));
  show_relations ("Breaks", ss_ref (relations, 5));
  show_relations ("Recommends", ss_ref (relations, 6));
  show_relations ("Enhances", ss_ref (relations, 7));
  show_relations ("Suggests", ss_ref (relations, 8));

  ss_val fields = dpm_ver_fields (ver);
  for (int i = 0; i < ss_len (fields); i += 2)
    dyn_print ("%r: %r\n", ss_ref (fields, i), ss_ref (fields, i+1));

  ss_val tags = dpm_ver_tags (ver);
  if (tags)
    {
      int len = ss_len (tags);
      dyn_print ("Tags:");
      for (int i = 0; i < len; i++)
	dyn_print (" %r%s", ss_ref (tags, i), (i < len-1)? ",":"");
      dyn_print ("\n");
    }
}

/* Stats
 */

void
dpm_db_stats ()
{
  int n_packages = 0;
  int n_versions = 0;

#if 0
  dpm_db db = dyn_get (cur_db);

  dyn_foreach_x ((ss_val key, ss_val val),
		 ss_dict_foreach, db->available)
    {
      n_packages += 1;
      if (val)
	n_versions += ss_len (val);
    }
#endif

  fprintf (stderr, "%d packages, %d versions\n",
	   n_packages, n_versions);
}

