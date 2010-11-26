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
#include "conf.h"
#include "parse.h"
#include "acq.h"

DPM_CONF_DECLARE (database, "database",
		  "string", "dpm.db",
		  "The filename of the database.")

DPM_CONF_DECLARE (keyring, "keyring",
		  "string", "/usr/share/keyrings/debian-archive-keyring.gpg",
		  "The keyring to use.")

/* The root:

   - format              (string, "dpm-0")
   - strings             (string table)
   - packages            (package -> package, weak key)
   - available versions  (package -> list of versions, weak sets)
   - installed version   (package -> something, maybe version, strong)
   - indices             (path -> index, strong)
   - tags                (tag -> versions)
   - reverse_relations   (package -> list of versions, weak sets)
   - update_time         (time of most recent full update, time_t blob)
   - max_version_id      (small integer)

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
  ss_dict *available;
  ss_dict *installed;
  ss_dict *indices;
  ss_dict *tags;
  ss_dict *reverse_rels;

  time_t update_time;
};

static void
dpm_db_abort (struct dpm_db_struct *db)
{
  if (db->strings)
    ss_tab_abort (db->strings);
  if (db->packages)
    ss_dict_abort (db->packages);
  if (db->available)
    ss_dict_abort (db->available);
  if (db->installed)
    ss_dict_abort (db->installed);
  if (db->indices)
    ss_dict_abort (db->indices);
  if (db->tags)
    ss_dict_abort (db->tags);
  if (db->reverse_rels)
    ss_dict_abort (db->reverse_rels);

  db->strings = NULL;
  db->packages = NULL;
  db->available = NULL;
  db->installed = NULL;
  db->indices = NULL;
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
  db->available = NULL;
  db->installed = NULL;
  db->indices = NULL;
  db->tags = NULL;
  db->reverse_rels = NULL;
  db->update_time = 0;
  return db;
}

static dyn_var cur_db[1];

dpm_db
dpm_db_current ()
{
  return dyn_get (cur_db);
}

static time_t
blob_to_time (ss_val val)
{
  if (val)
    return *(time_t *)ss_blob_start (val);
  else
    return 0;
}

static ss_val
blob_from_time (ss_store ss, time_t t)
{
  return ss_blob_new (ss, sizeof (t), &t);
}

void
dpm_db_open ()
{
  const char *name = dpm_conf_string (database);

  dpm_db db = dpm_db_make (ss_open (name, SS_WRITE));
  dyn_let (cur_db, db);

  ss_val root = ss_get_root (db->store);

  if (root && !ss_streq (ss_ref_safely (root, 0), "dpm-0"))
    dyn_error ("%s is not a dpm database", name);

  db->strings = ss_tab_init (db->store, ss_ref_safely (root, 1));
  db->packages = ss_dict_init (db->store, ss_ref_safely (root, 2),
			       SS_DICT_WEAK_KEYS);
  db->available = ss_dict_init (db->store,
				ss_ref_safely (root, 3), SS_DICT_WEAK_SETS);
  db->installed = ss_dict_init (db->store,
				ss_ref_safely (root, 4), SS_DICT_STRONG);
  db->indices = ss_dict_init (db->store,
			      ss_ref_safely (root, 5), SS_DICT_STRONG);
  db->tags = ss_dict_init (db->store,
			   ss_ref_safely (root, 6), SS_DICT_WEAK_SETS);
  db->reverse_rels = ss_dict_init (db->store,
				   ss_ref_safely (root, 7), SS_DICT_WEAK_SETS);
  db->update_time = blob_to_time (ss_ref_safely (root, 8));
}

void
dpm_db_checkpoint ()
{
  dpm_db db = dyn_get (cur_db);

  ss_val root = ss_new (db->store, 0, 9,
			ss_blob_new (db->store, 5, "dpm-0"),
			ss_tab_store (db->strings), 
			ss_dict_store (db->packages),
			ss_dict_store (db->available),
			ss_dict_store (db->installed),
			ss_dict_store (db->indices),
			ss_dict_store (db->tags),
			ss_dict_store (db->reverse_rels),
			blob_from_time (db->store, db->update_time));
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
dpm_db_package_count ()
{
  dpm_db db = dyn_get (cur_db);
  return ss_tag_count (db->store, 65);
}

int
dpm_db_version_count ()
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

static ss_val
store_string (dpm_db db, const char *string)
{
  return ss_blob_new (db->store, strlen (string), (void *)string);
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

ss_val
dpm_db_available (dpm_package pkg)
{
  dpm_db db = dyn_get (cur_db);

  return ss_dict_get (db->available, pkg);
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

static dpm_version
find_version (dpm_db db, dpm_package pkg, ss_val version, ss_val architecture)
{
  ss_val versions = ss_dict_get (db->available, pkg);
  if (versions)
    {
      for (int i = 0; i < ss_len (versions); i++)
	{
	  dpm_version ver = ss_ref (versions, i);
	  if (dpm_ver_version (ver) == version
	      && dpm_ver_architecture (ver) == architecture)
	    return ver;
	}
    }

  dpm_version installed = ss_dict_get (db->installed, pkg);
  if (installed
      && dpm_ver_version (installed) == version
      && dpm_ver_architecture (installed) == architecture)
    return installed;

  return NULL;
}

dpm_version
dpm_db_find_version (const char *package, const char *version,
		     const char *architecture)
{
  dpm_db db = dyn_get (cur_db);

  dpm_package pkg = dpm_db_find_package (package);
  ss_val ver_sym = intern_soft (db, version);
  ss_val arch_sym = intern_soft (db, architecture);

  if (pkg == NULL || ver_sym == NULL || arch_sym == NULL)
    return NULL;

  return find_version (db, pkg, ver_sym, arch_sym);
}

/* Updating the available packages.

   There are two ways of updating the available packages: full and
   incremental.

   The full update completely reconstructs the "available", "indices",
   and "tags" dicts from a full set of Packages files.  As a
   optimization, if a Packages file hasn't actually changed, we don't
   parse it again and just take its versions from the old "indices"
   dict.

   The incremental update takes as input a list of packages to add and
   to remove.  Unfortunately, the pdiff files we get from the archive
   are just line-by-line diffs.  So we don't do incremental updates
   just yet, but they should eventually be the norm.
 */

// XXX - remove limits

typedef struct {
  dpm_db db;

  ss_dict *old_indices;

  int index_prefix_len;
  int n_indices;
  struct {
    dpm_release_index release;
    dyn_val path;
    dyn_val sha256;
    int needs_update;
  } index[128];

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

  dpm_package package;
  ss_val version;
  ss_val architecture;
  ss_val shortdesc;

  ss_val pre_depends;
  ss_val depends;
  ss_val conflicts;
  ss_val provides;
  ss_val replaces;
  ss_val breaks;
  ss_val recommends;
  ss_val enhances;
  ss_val suggests;

  int n_alternatives;
  ss_val alternatives[3*64];

  int n_relations;
  ss_val relations[2048];

  int n_tags;
  ss_val tags[64];

  int n_fields;
  ss_val fields[64];

  int n_versions, max_versions, new_versions;
  dpm_version *versions;
} update_data;

ss_val
parse_relations (update_data *ud, const char *value, int value_len)
{
  dyn_input in = dyn_open_string (value, value_len);

  ud->n_relations = 0;
  while (true)
    {
      ud->n_alternatives = 0;
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

	  if (ud->n_alternatives >= 3*64)
	    dyn_error ("Too many alternatives: %r", dpm_pkg_name (ud->package));

	  int i = ud->n_alternatives;
	  ud->alternatives[i++] = ss_from_int (op_code);
	  ud->alternatives[i++] = find_create_package (ud->db, 
						       alt.name, alt.name_len);
	  ud->alternatives[i++] = (alt.version
				   ? ss_tab_intern_blob (ud->db->strings,
							 alt.version_len,
							 (void *)alt.version)
				   : NULL);
	  ud->n_alternatives = i;
	}

      if (ud->n_alternatives > 0)
	{
	  if (ud->n_relations >= 2048)
	    dyn_error ("Too many relations: %r", dpm_pkg_name (ud->package));
      
	  ud->relations[ud->n_relations++] = ss_newv (ud->db->store, 0,
						      ud->n_alternatives, 
						      ud->alternatives);
	}

      if (!dpm_parse_next_relation (in))
	break;
    }

  return ss_newv (ud->db->store, 0, ud->n_relations, ud->relations);
}

static void
record_version (dpm_db db, dpm_version ver)
{
  dpm_package pkg = dpm_ver_package (ver);
  ss_val rels_rec = dpm_ver_relations (ver);
  ss_val tags = dpm_ver_tags (ver);

  ss_dict_add (db->available, pkg, ver);

  for (int i = 0; i < ss_len (rels_rec); i++)
    {
      ss_val rels = ss_ref (rels_rec, i);
      if (rels)
	for (int j = 0; j < ss_len (rels); j++)
	  {
	    ss_val rel = ss_ref (rels, j);
	    for (int k = 0; k < ss_len (rel); k += 3)
	      ss_dict_add (db->reverse_rels, ss_ref (rel, k+1), ver);
	  }
    }

  if (tags)
    for (int i = 0; i < ss_len (tags); i++)
      ss_dict_add (db->tags, ss_ref (tags, i), ver);
}

static bool
parse_package_stanza (update_data *ud, dyn_input in)
{
  dpm_db db = ud->db;

  ud->package = NULL;
  ud->version = NULL;
  ud->architecture = NULL;
  ud->shortdesc = NULL;

  ud->pre_depends = NULL;
  ud->depends = NULL;
  ud->conflicts = NULL;
  ud->provides = NULL;
  ud->replaces = NULL;
  ud->breaks = NULL;
  ud->recommends = NULL;
  ud->enhances = NULL;
  ud->suggests = NULL;

  ud->n_tags = 0;
  ud->n_fields = 0;

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
		  if (ud->n_tags >= 64)
		    dyn_error ("Too many tags");
		  
		  ud->tags[ud->n_tags++] =
		    ss_tab_intern_blob (ud->db->strings,
					f.len, (void *)f.field);
		}
	    }
	}
      else if (key == ud->pre_depends_key)
	ud->pre_depends = parse_relations (ud, f.value, f.value_len);
      else if (key == ud->depends_key)
	ud->depends = parse_relations (ud, f.value, f.value_len);
      else if (key == ud->conflicts_key)
	ud->conflicts = parse_relations (ud, f.value, f.value_len);
      else if (key == ud->provides_key)
	ud->provides = parse_relations (ud, f.value, f.value_len);
      else if (key == ud->replaces_key)
	ud->replaces = parse_relations (ud, f.value, f.value_len);
      else if (key == ud->breaks_key)
	ud->breaks = parse_relations (ud, f.value, f.value_len);
      else if (key == ud->recommends_key)
	ud->recommends = parse_relations (ud, f.value, f.value_len);
      else if (key == ud->enhances_key)
	ud->enhances = parse_relations (ud, f.value, f.value_len);
      else if (key == ud->suggests_key)
	ud->suggests = parse_relations (ud, f.value, f.value_len);
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
	    ud->version = val;
	  else if (key == ud->architecture_key)
	    ud->architecture = val;
	  else
	    {
	      ud->fields[ud->n_fields] = key;
	      ud->fields[ud->n_fields+1] = val;
	      ud->n_fields += 2;
	      if (ud->n_fields > 63)
		dyn_error ("too many fields");
	    }
      
	  if (key == ud->description_key)
	    {
	      char *desc = ss_blob_start (val);
	      char *pos = memchr (desc, '\n', ss_len (val));
	      if (pos)
		ud->shortdesc = ss_tab_intern_blob (db->strings,
						    pos-desc, desc);
	      else
		ud->shortdesc = val;
	    }
	}
    }

  if (ud->package == NULL)
    dyn_error ("Stanza without package");
  if (ud->version == NULL)
    dyn_error ("Package without version: %r",
	       dpm_pkg_name (ud->package));
  if (ud->architecture == NULL)
    dyn_error ("Package without architecture: %r",
	       dpm_pkg_name (ud->package));

  dpm_version ver = find_version (ud->db,
				  ud->package,
				  ud->version, 
				  ud->architecture);

  if (ver == NULL)
    {
      ver = ss_new (db->store, 64, 8,
		    NULL,
		    ud->package,
		    ud->version,
		    ud->architecture,
		    ss_new (db->store, 0, 9,
			    ud->pre_depends,
			    ud->depends,
			    ud->conflicts,
			    ud->provides,
			    ud->replaces,
			    ud->breaks,
			    ud->recommends,
			    ud->enhances,
			    ud->suggests),
		    ss_newv (db->store, 0,
			     ud->n_tags, ud->tags),
		    ud->shortdesc,
		    ss_newv (db->store, 0,
			     ud->n_fields, ud->fields));
      
      record_version (ud->db, ver);
      
      ud->new_versions++;
    }

  if (ud->n_versions >= ud->max_versions)
    {
      ud->max_versions += 1024;
      ud->versions = dyn_realloc (ud->versions,
				  sizeof(dpm_version) * ud->max_versions);
    }
  ud->versions[ud->n_versions++] = ver;

  return true;
}

static void
update_index (update_data *ud, dpm_release_index release,
	      dyn_val path, dyn_val sha256)
{
  dyn_block 
    {
      ud->max_versions = 0;
      ud->new_versions = 0;
      ud->n_versions = 0;
      ud->versions = NULL;

      ss_val interned_path = intern (ud->db, path);

      dyn_input in = dpm_acq_open_local (dyn_to_string (path));
      while (parse_package_stanza (ud, in))
	;
	  
      dyn_print ("%d new versions\n", ud->new_versions);
	  
      dpm_package_index index = ss_new (ud->db->store, 0, 3,
					interned_path,
					release,
					ss_newv (ud->db->store, 0,
						 ud->n_versions,
						 ud->versions));
      free (ud->versions);

      ss_dict_set (ud->db->indices, interned_path, index);
    }
}

static void
reuse_index (update_data *ud, dpm_release_index release,
	     dyn_val path, dyn_val sha256)
{
  ss_val interned_path = intern (ud->db, path);
  dpm_package_index old_index = ss_dict_get (ud->old_indices, interned_path);

  if (old_index == NULL)
    dyn_error ("Can't find old index for %v", path);

  ss_val old_versions = ss_ref (old_index, 2);
  dpm_package_index index = ss_new (ud->db->store, 0, 3,
				    interned_path,
				    release,
				    old_versions);

  for (int i = 0; i < ss_len (old_versions); i++)
    {
      dpm_version ver = ss_ref (old_versions, i);
      dpm_package pkg = dpm_ver_package (ver);
      dpm_package version = dpm_ver_version (ver);
      dpm_package architecture = dpm_ver_architecture (ver);

      if (find_version (ud->db, pkg, version, architecture) == NULL)
	record_version (ud->db, ver);
    }
  
  ss_dict_set (ud->db->indices, interned_path, index);
}

static void
add_release (update_data *ud,
	     dyn_val source, dyn_val dist,
	     dyn_val comps, dyn_val archs)
{
  dyn_begin ();

  dyn_val path = dyn_format ("%v/dists/%v/Release", source, dist);
  dpm_release_index release  = ss_new (ud->db->store, 0, 3,
				       store_string (ud->db,
						     dyn_to_string (path)),
				       intern (ud->db, dyn_to_string (dist)),
				       NULL);

  ud->index_prefix_len = strlen (dyn_to_string (path)) - strlen ("Release");
  int first_index = ud->n_indices;
  for (int i = 0; i < dyn_len (comps); i++)
    for (int j = 0; j < dyn_len (archs); j++)
      {
	int n = ud->n_indices;
	ud->index[n].path =
	  dyn_ref (dyn_format ("%v/dists/%v/%v/binary-%v/Packages.gz",
			       source,
			       dist,
			       dyn_elt (comps, i),
			       dyn_elt (archs, j)));
	ud->index[n].sha256 = NULL;
	ud->index[n].release = release;
	ud->n_indices += 1;
      }

  dyn_input in = dpm_acq_open (path);
  if (in)
    {
      dyn_foreach_iter (f, dpm_parse_control_fields, in)
	{
	  if (strncmp (f.name, "SHA256", f.name_len) == 0)
	    {
	      dyn_input val_in = dyn_open_string (f.value, f.value_len);
	      dyn_foreach_iter (l, dpm_parse_lines, val_in)
		{
		  if (l.n_fields == 3)
		    {
		      for (int i = 0; i < ud->n_indices; i++)
			{
			  const char *p = dyn_to_string (ud->index[i].path);
			  if ((l.field_lens[2] +
			       ud->index_prefix_len) == strlen (p)
			      && strncmp (p + ud->index_prefix_len,
					  l.fields[2], l.field_lens[2]) == 0)
			    {
			      ud->index[i].sha256 =
				dyn_ref (dyn_from_stringn (l.fields[0],
							   l.field_lens[0]));
			      break;
			    }
			}
		    }
		}
	    }
	}
    }

  for (int i = first_index; i < ud->n_indices; i++)
    {
      ss_val interned_path = intern (ud->db, ud->index[i].path);
      dpm_acq_code code = dpm_acquire (dyn_to_string (ud->index[i].path));
      
      int has_changed = (code == DPM_ACQ_CHANGED);
      int is_new = (ss_dict_get (ud->old_indices, interned_path) == NULL);

      if (is_new)
	dyn_print ("%r new\n", interned_path);
      else if (has_changed)
	dyn_print ("%r changed\n", interned_path);
      else
	dyn_print ("%r unchanged\n", interned_path);
      
      ud->index[i].needs_update = has_changed || is_new;
    }

  dyn_end ();
}

void
dpm_db_full_update (dyn_val srcs, dyn_val dists,
		    dyn_val comps, dyn_val archs)
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
  
  ud.n_indices = 0;
  ud.old_indices = ud.db->indices;
  ud.db->indices = ss_dict_init (ud.db->store, NULL, SS_DICT_STRONG);
  ss_dict_abort (ud.db->available);
  ud.db->available = ss_dict_init (ud.db->store, NULL, SS_DICT_WEAK_SETS);

  for (int i = 0; i < dyn_len (srcs); i++)
    for (int j = 0; j < dyn_len (dists); j++)
      add_release (&ud, dyn_elt (srcs, i), dyn_elt (dists, j),
		   comps, archs);

  for (int i = 0; i < ud.n_indices; i++)
    {
      if (ud.index[i].needs_update)
	update_index (&ud, ud.index[i].release, ud.index[i].path,
		      ud.index[i].sha256);
      else
	reuse_index (&ud, ud.index[i].release, ud.index[i].path,
		     ud.index[i].sha256);

      dyn_unref (ud.index[i].path);
      dyn_unref (ud.index[i].sha256);
    }

  ss_dict_finish (ud.old_indices);

  ud.db->update_time = time (NULL);
  dpm_db_checkpoint ();
}

void
dpm_db_maybe_full_update (dyn_val srcs, dyn_val dists,
			  dyn_val comps, dyn_val archs)
{
  dpm_db db = dyn_get (cur_db);

  if (time (NULL) > db->update_time + 5*60)
    dpm_db_full_update (srcs, dists, comps, archs);
}

/* Iterating
 */

void
dpm_db_foreach_package (void (*func) (dpm_package pkg))
{
  dpm_db db = dyn_get (cur_db);

  dyn_foreach_x ((ss_val key, ss_val val),
		 ss_dict_foreach, db->packages)
    {
      func (val);
    }
}

void
dpm_db_foreach_installed (void (*func) (dpm_package pkg, dpm_version ver))
{
  dpm_db db = dyn_get (cur_db);
  
  dyn_foreach_x ((ss_val key, ss_val val),
		 ss_dict_foreach, db->installed)
    {
      func (key, val);
    }
}

void
dpm_db_foreach_installed_package (void (*func) (dpm_package pkg))
{
  dpm_db db = dyn_get (cur_db);
  
  dyn_foreach_x ((ss_val key, ss_val val),
		 ss_dict_foreach, db->installed)
    {
      func (key);
    }
}

void
dpm_db_foreach_package_index (void (*func) (dpm_package_index idx))
{
  dpm_db db = dyn_get (cur_db);
  
  dyn_foreach_x ((ss_val key, ss_val val),
		 ss_dict_foreach, db->indices)
    {
      func (val);
    }
}

void
dpm_db_package_indices_init (dpm_db_package_indices *iter)
{
  dpm_db db = dyn_get (cur_db);
  ss_dict_entries_init (&iter->entries_iter, db->indices);
}

void
dpm_db_package_indices_fini (dpm_db_package_indices *iter)
{
  ss_dict_entries_fini (&iter->entries_iter);
}

void
dpm_db_package_indices_step (dpm_db_package_indices *iter)
{
  ss_dict_entries_step (&iter->entries_iter);
}

bool
dpm_db_package_indices_done (dpm_db_package_indices *iter)
{
  return ss_dict_entries_done (&iter->entries_iter);
}

dpm_package_index
dpm_db_package_indices_elt (dpm_db_package_indices *iter)
{
  return iter->entries_iter.val;
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
dpm_db_version_foreach_pkgindex (void (*func)(dpm_package_index idx),
				 dpm_version ver)
{
  dpm_db db = dyn_get (cur_db);

  dyn_foreach_x ((ss_val path, dpm_package_index idx),
		 ss_dict_foreach, db->indices)
    {
      ss_val versions = dpm_pkgidx_versions (idx);
      for (int i = 0; i < ss_len(versions); i++)
	if (ss_ref (versions, i) == ver)
	  func (idx);
    }
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
  dpm_db db = dyn_get (cur_db);

  int n_packages = 0;
  int n_versions = 0;

  dyn_foreach_x ((ss_val key, ss_val val),
		 ss_dict_foreach, db->available)
    {
      n_packages += 1;
      if (val)
	n_versions += ss_len (val);
    }

  fprintf (stderr, "%d packages, %d versions\n",
	   n_packages, n_versions);
}

