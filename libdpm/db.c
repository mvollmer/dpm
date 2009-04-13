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
   - available versions  (package name -> list of versions, weak sets)
   - installed version   (package name -> something, maybe version, strong)
   - indices             (path -> index, strong)
   - tags                (tag -> versions)

   A version:

   - package             (interned string)
   - version             (string)
   - architecture        (interned string)
   - relations           (list of relations)
   - tags                (list of strings)
   - shortdesc           (string)
   - fields              (field name -> string)

   A relation:

   - type                (interned string)
   - op                  (int)
   - package             (interned string)
   - version             (string)

   A package index:

   - path                (string, "http://ftp.fi.debian.org/.../Packages.bz2")
   - release             (release index or null)
   - checktime           (time_t blob, time of last online check)
   - versions            (list of versions)

   A release index

   - path                (string, "http://ftp.fi.debian.org/.../Release")
   - dist                (interned string, "stable")
   - valid keys          (list of strings, fingerprints)

   All strings used as dictionary keys are interned, of course.
   
   Control field values are interned as well, to save space.  Lot's of
   values are duplicated between different architectures and version.

*/

struct dpm_db_struct {
  ss_store store;
  
  ss_tab *strings;
  ss_dict *available;
  ss_dict *installed;
  ss_dict *indices;
  ss_dict *tags;
};

static void
dpm_db_unref (dyn_type *type, void *object)
{
  struct dpm_db_struct *db = object;

  // XXX - finishing the tables is wasteful, we don't need to store
  //       them.
  if (db->strings)
    ss_tab_finish (db->strings);
  if (db->available)
    ss_dict_finish (db->available);
  if (db->installed)
    ss_dict_finish (db->installed);
  if (db->indices)
    ss_dict_finish (db->indices);
  if (db->tags)
    ss_dict_finish (db->tags);

  dyn_unref (db->store);
}

static int
dpm_db_equal (void *a, void *b)
{
  return 0;
}

DYN_DECLARE_TYPE (dpm_db);
DYN_DEFINE_TYPE (dpm_db, "db");

static dyn_val
dpm_db_make (ss_store store)
{
  dpm_db db = dyn_new (dpm_db);
  db->store = dyn_ref (store);
  db->strings = NULL;
  db->available = NULL;
  db->installed = NULL;
  db->indices = NULL;
  db->tags = NULL;
  return db;
}

static dyn_var cur_db[1];

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
  db->available = ss_dict_init (db->store,
				ss_ref_safely (root, 2), SS_DICT_WEAK_SETS);
  db->installed = ss_dict_init (db->store,
				ss_ref_safely (root, 3), SS_DICT_STRONG);
  db->indices = ss_dict_init (db->store,
			      ss_ref_safely (root, 4), SS_DICT_STRONG);
  db->tags = ss_dict_init (db->store,
			   ss_ref_safely (root, 5), SS_DICT_WEAK_SETS);
}

void
dpm_db_checkpoint ()
{
  dpm_db db = dyn_get (cur_db);

  ss_val root = ss_new (db->store, 0, 6,
			ss_blob_new (db->store, 5, "dpm-0"),
			ss_tab_store (db->strings), 
			ss_dict_store (db->available),
			ss_dict_store (db->installed),
			ss_dict_store (db->indices),
			ss_dict_store (db->tags));
  ss_set_root (db->store, root);
}

void
dpm_db_done ()
{
  dpm_db db = dyn_get (cur_db);

  // XXX - finishing the tables is wasteful, we don't need to store
  //       them.
  if (db->strings)
    ss_tab_finish (db->strings);
  if (db->available)
    ss_dict_finish (db->available);
  if (db->installed)
    ss_dict_finish (db->installed);
  if (db->indices)
    ss_dict_finish (db->indices);
  if (db->tags)
    ss_dict_finish (db->tags);

  ss_maybe_gc (db->store);

  db->store = NULL;
  db->strings = NULL;
  db->available = NULL;
  db->installed = NULL;
  db->indices = NULL;
  db->tags = NULL;

  dyn_set (cur_db, NULL);
}

/* Basic accessors
 */

dpm_package
dpm_db_find_package (const char *name)
{
  dpm_db db = dyn_get (cur_db);

  return ss_tab_intern_soft (db->strings, strlen (name), (void *)name);
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

static ss_val
intern_soft (dpm_db db, const char *string)
{
  return ss_tab_intern_soft (db->strings, strlen (string), (void *)string);
}

dpm_package
dpm_db_find_version (const char *package, const char *version,
		     const char *architecture)
{
  dpm_db db = dyn_get (cur_db);

  dpm_package pkg_sym = intern_soft (db, package);
  dpm_package ver_sym = intern_soft (db, version);
  dpm_package arch_sym = intern_soft (db, architecture);

  if (pkg_sym == NULL || ver_sym == NULL || arch_sym == NULL)
    return NULL;

  ss_val versions = dpm_db_available (pkg_sym);
  if (versions)
    {
      for (int i = 0; i < ss_len (versions); i++)
	{
	  dpm_version ver = ss_ref (versions, i);
	  if (ss_ref (ver, 1) == ver_sym && ss_ref (ver, 2) == arch_sym)
	    return ver;
	}
    }

  dpm_version installed = dpm_db_installed (pkg_sym);
  if (installed
      && ss_ref (installed, 1) == ver_sym
      && ss_ref (installed, 2) == arch_sym)
    return installed;

  return NULL;
}

/* Importing
 */

typedef ss_val dpm_release_index;
typedef ss_val dpm_package_index;

static ss_val
store_string (dpm_db db, const char *string)
{
  return ss_blob_new (db->store, strlen (string), (void *)string);
}

static ss_val
intern (dpm_db db, const char *string)
{
  return ss_tab_intern_blob (db->strings, strlen (string), (void *)string);
}

typedef struct {
  dpm_db db;

  ss_val package_key;
  ss_val version_key;
  ss_val architecture_key;
  ss_val description_key;
  ss_val tag_key;

  ss_val package;
  ss_val version;
  ss_val architecture;
  ss_val shortdesc;

  int n_tags;
  ss_val tags[64];

  int n_fields;
  ss_val fields[64];

  int n_versions, max_versions;
  dpm_version *versions;
} package_stanza_data;

void
package_stanza_tag (dyn_input in,
		    const char *field, int field_len,
		    void *data)
{
  package_stanza_data *pd = data;

  if (pd->n_tags >= 64)
    dyn_error ("Too many tags");

  pd->tags[pd->n_tags++] = ss_tab_intern_blob (pd->db->strings,
					       field_len, (void *)field);
}

void
package_stanza_field (dyn_input in,
		      const char *name, int name_len,
		      const char *value, int value_len,
		      void *data)
{
  package_stanza_data *pd = data;
  dpm_db db = pd->db;

  ss_val key = ss_tab_intern_blob (db->strings, name_len, (void *)name);

  if (key == pd->tag_key)
    {
      dyn_block
	{
	  dyn_input t = dyn_open_string (value, value_len);
	  dpm_parse_comma_fields (t, package_stanza_tag, pd);
	}
    }
  else
    {
      ss_val val = ss_tab_intern_blob (db->strings, value_len, (void *)value);

      if (key == pd->package_key)
	pd->package = val;
      else if (key == pd->version_key)
	pd->version = val;
      else if (key == pd->architecture_key)
	pd->architecture = val;
      else
	{
	  pd->fields[pd->n_fields] = key;
	  pd->fields[pd->n_fields+1] = val;
	  pd->n_fields += 2;
	  if (pd->n_fields > 63)
	    dyn_error ("too many fields");
	}
      
      if (key == pd->description_key)
	{
	  char *desc = ss_blob_start (val);
	  char *pos = memchr (desc, '\n', ss_len (val));
	  if (pos)
	    pd->shortdesc = ss_tab_intern_blob (db->strings, pos-desc, desc);
	  else
	    pd->shortdesc = val;
	}
    }
}

static int
parse_package_stanza (package_stanza_data *pd, dyn_input in)
{
  dpm_db db = pd->db;

  /* We could check for an existing version record with the same
     package/version/architecture triple here and avoid inserting the
     current one.
     
     But that is not always correct since overrides in the
     repository can change without changing the triple.  In
     essence, the version of a package does not really apply to
     its control section.
     
     Thus, we always import a new version record here without
     trying to skip those we might already have.  To keep churn
     low anyway, we rely on external mechanisms to only feed us
     stanzas that have actually changed.  This needs to happen
     with cooperation from the repo, unfortunately, since the
     current pdiff files are line-by-line diffs, not
     stanza-by-stanza.
  */

  pd->package = NULL;
  pd->version = NULL;
  pd->architecture = NULL;
  pd->shortdesc = NULL;
  pd->n_tags = 0;
  pd->n_fields = 0;

  if (dpm_parse_control (in, package_stanza_field, pd))
    {
      if (pd->package == NULL)
	dyn_error ("Stanza without package");
      if (pd->version == NULL)
	dyn_error ("Package without version: %r", pd->package);
      if (pd->architecture == NULL)
	dyn_error ("Package without architecture: %r", pd->package);

      dpm_version ver = ss_new (db->store, 0, 7,
				pd->package,
				pd->version,
				pd->architecture,
				NULL,
				ss_newv (db->store, 0,
					 pd->n_tags, pd->tags),
				pd->shortdesc,
				ss_newv (db->store, 0,
					 pd->n_fields, pd->fields));

      ss_dict_add (pd->db->available, pd->package, ver);
      for (int i = 0; i < pd->n_tags; i++)
	ss_dict_add (pd->db->tags, pd->tags[i], ver);

      if (pd->n_versions >= pd->max_versions)
	{
	  pd->max_versions += 1024;
	  pd->versions = dyn_realloc (pd->versions,
				       sizeof(dpm_version) * pd->max_versions);
	}
      pd->versions[pd->n_versions++] = ver;

      return 1;
    }
  else
    return 0;
}

static time_t
blob_to_time (ss_val val)
{
  return *(time_t *)ss_blob_start (val);
}

static ss_val
blob_from_time (ss_store ss, time_t t)
{
  return ss_blob_new (ss, sizeof (t), &t);
}

static void
dpm_db_update_index (dyn_val path, ss_dict *old_indices,
		     dpm_release_index release, dyn_val sha256)
{
  dyn_block 
    {
      package_stanza_data pd;
      pd.db = dyn_get (cur_db);

      pd.package_key = intern (pd.db, "Package");
      pd.version_key = intern (pd.db, "Version");
      pd.architecture_key = intern (pd.db, "Architecture");
      pd.description_key = intern (pd.db, "Description");
      pd.tag_key = intern (pd.db, "Tag");

      pd.max_versions = 0;
      pd.n_versions = 0;
      pd.versions = NULL;

      ss_val interned_path = intern (pd.db, path);
      dpm_package_index old_index = ss_dict_get (old_indices, interned_path);
      dpm_package_index new_index = NULL;

      if (old_index
	  && blob_to_time (ss_ref (old_index, 2)) > time(NULL) - 5*60)
	{
	  dyn_print ("%v not checked\n", path);
	  // XXX - hmm, the release index might need updating.
	  new_index = old_index;
	}
      else
	{
	  ss_val now = blob_from_time (pd.db->store, time(NULL));

	  dpm_acq_code code = dpm_acquire (dyn_to_string (path));
	  if (code == DPM_ACQ_NOT_FOUND)
	    {
	      dyn_print ("%v not found\n", path);
	    }	
	  else if (code == DPM_ACQ_UNCHANGED && old_index)
	    {
	      dyn_print ("%v unchanged\n", path);
	      new_index = ss_new (pd.db->store, 0, 4,
				  interned_path,
				  release,
				  now,
				  ss_ref (old_index, 3));
	    }
	  else
	    {
	      if (old_index)
		dyn_print ("%v changed\n", path);
	      else
		dyn_print ("%v new\n", path);

	      dyn_input in = dpm_acq_open_local (dyn_to_string (path));
	      while (parse_package_stanza (&pd, in))
		;

	      dyn_print ("%d versions\n", pd.n_versions);

	      new_index = ss_new (pd.db->store, 0, 4,
				  interned_path,
				  release,
				  now,
				  ss_newv (pd.db->store, 0,
					   pd.n_versions,
					   pd.versions));
	      free (pd.versions);
	    }
	}
      
      ss_dict_set (pd.db->indices, interned_path, new_index);
    }
}

/* Release index
 */

typedef struct {
  dpm_db db;

  dpm_release_index release;

  int index_prefix_len;
  int n_indices;
  dyn_val index_paths[128];
  dyn_val index_sha256[128];
} release_parse_data;

static void
sha256_line (dyn_input in,
	     int n_fields, const char **fields, int *field_lens,
	     void *data)
{
  release_parse_data *pd = data;

  if (n_fields == 3)
    {
      for (int i = 0; i < pd->n_indices; i++)
	{
	  const char *p = dyn_to_string (pd->index_paths[i]);
	  if (field_lens[2] + pd->index_prefix_len == strlen (p)
	      && strncmp (p + pd->index_prefix_len,
			  fields[2], field_lens[2]) == 0)
	    {
	      pd->index_sha256[i] = dyn_from_stringn (fields[0], field_lens[0]);
	      break;
	    }
	}
    }
}

static void
release_field (dyn_input in,
	       const char *name, int name_len,
	       const char *value, int value_len,
	       void *data)
{
  release_parse_data *pd = data;

  if (strncmp (name, "SHA256", name_len) == 0)
    {
      dyn_input val_in = dyn_open_string (value, value_len);
      dpm_parse_lines (val_in, sha256_line, pd);
    }
}

static dpm_release_index
dpm_db_update_release (dyn_val source, dyn_val dist,
		       dyn_val comps, dyn_val archs,
		       ss_dict *old_indices)
{
  dyn_begin ();

  release_parse_data pd;
  pd.db = dyn_get (cur_db);
  
  dyn_val path = dyn_format ("%v/dists/%v/Release", source, dist);
  ss_val release = ss_new (pd.db->store, 0, 3,
			   store_string (pd.db, dyn_to_string (path)),
			   intern (pd.db, dyn_to_string (path)),
			   NULL);
  
  pd.release = release;

  pd.index_prefix_len = strlen (dyn_to_string (path)) - strlen ("Release");
  pd.n_indices = 0;
  for (int i = 0; i < dyn_len (comps); i++)
    for (int j = 0; j < dyn_len (archs); j++)
      {
	pd.index_paths[pd.n_indices] =
	  dyn_format ("%v/dists/%v/%v/binary-%v/Packages.gz",
		      source, dist, dyn_elt (comps, i), dyn_elt (archs, j));
	pd.index_sha256[pd.n_indices] = NULL;
	pd.n_indices += 1;
      }

  dyn_input in = dpm_acq_open (path);
  if (in)
    {
      dpm_parse_control (in, release_field, &pd);

      for (int i = 0; i < pd.n_indices; i++)
	dpm_db_update_index (pd.index_paths[i], old_indices,
			     release, pd.index_sha256[i]);
    }

  dyn_end ();

  return release;
}

void
dpm_db_update (dyn_val srcs, dyn_val dists,
	       dyn_val comps, dyn_val archs)
{
  dpm_db db = dyn_get (cur_db);
  ss_dict *old_indices = db->indices;
  db->indices = ss_dict_init (db->store, NULL, SS_DICT_STRONG);

  for (int i = 0; i < dyn_len (srcs); i++)
    for (int j = 0; j < dyn_len (dists); j++)
      {
	dpm_db_update_release (dyn_elt (srcs, i), dyn_elt (dists, j),
			       comps, archs, old_indices);
	dpm_db_checkpoint ();
      }

  ss_dict_finish (old_indices);
}

/* Iterating
 */

typedef struct {
  void (*func) (dpm_package pkg, void *data);
  void *data;
} foreach_package_data;

static void
foreach_available_package (ss_val key, ss_val val, void *data)
{
  foreach_package_data *d = data;

  if (val)
    d->func (key, d->data);
}

void
dpm_db_foreach_package (void (*func) (dpm_package pkg, void *data),
			void *data)
{
  dpm_db db = dyn_get (cur_db);
  foreach_package_data d;

  d.func = func;
  d.data = data;
  ss_dict_foreach (db->available, foreach_available_package, &d);
}

ss_val
dpm_db_version_get (dpm_version ver, const char *field)
{
  ss_val fields = ss_ref (ver, 6);
  if (fields)
    for (int i = 0; i < ss_len (fields); i += 2)
      if (ss_streq (ss_ref (fields, i), field))
	return ss_ref (fields, i+1);
  return NULL;
}

ss_val
dpm_db_version_shortdesc (dpm_version ver)
{
  return ss_ref (ver, 5);
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

void
dpm_db_show_version (dpm_version ver)
{
  dyn_print ("Package: %r\n", ss_ref (ver, 0));
  dyn_print ("Version: %r\n", ss_ref (ver, 1));
  dyn_print ("Architecture: %r\n", ss_ref (ver, 2));

  ss_val fields = ss_ref (ver, 6);
  for (int i = 0; i < ss_len (fields); i += 2)
    dyn_print ("%r: %r\n", ss_ref (fields, i), ss_ref (fields, i+1));

  ss_val tags = ss_ref (ver, 4);
  if (tags)
    {
      int len = ss_len (tags);
      dyn_print ("Tags:");
      for (int i = 0; i < len; i++)
	dyn_print (" %r%s", ss_ref (tags, i), (i < len-1)? ",":"");
      dyn_print ("\n");
    }
}

/* Dumping
 */

static void
dump_available (ss_val key, ss_val val, void *data)
{
  dyn_print ("%r:", key);
  for (int i = 0; i < ss_len (val); i++)
    {
      ss_val v = ss_ref (val, i);
      if (v)
	dyn_print (" %r", ss_ref (v, 3));
      else
	dyn_print (" <null>");
    }
  dyn_print ("\n");
}

void
dpm_db_dump ()
{
  dpm_db db = dyn_get (cur_db);

  ss_dict_foreach (db->available, dump_available, NULL);
}

/* Stats
 */

typedef struct {
  int n_packages;
  int n_versions;
} stats;

static void
collect_stats (ss_val key, ss_val val, void *data)
{
  stats *s = data;
  
  s->n_packages += 1;
  if (val)
    s->n_versions += ss_len (val);
}

void
dpm_db_stats ()
{
  dpm_db db = dyn_get (cur_db);

  stats s;
  s.n_packages = 0;
  s.n_versions = 0;

  ss_dict_foreach (db->available, collect_stats, &s);

  fprintf (stderr, "%d packages, %d versions\n",
	   s.n_packages, s.n_versions);
}

