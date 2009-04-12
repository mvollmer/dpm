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
}

void
dpm_db_checkpoint ()
{
  dpm_db db = dyn_get (cur_db);

  ss_val root = ss_new (db->store, 0, 5,
			ss_blob_new (db->store, 5, "dpm-0"),
			ss_tab_store (db->strings), 
			ss_dict_store (db->available),
			ss_dict_store (db->installed),
			ss_dict_store (db->indices));
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

  ss_maybe_gc (db->store);

  db->store = NULL;
  db->strings = NULL;
  db->available = NULL;
  db->installed = NULL;
  db->indices = NULL;

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

  int n_versions, max_versions;
  dpm_version *versions;

  int n_new;
} parse_data;

static int
parse_package_stanza (parse_data *pd, dyn_input in)
{
  dpm_db db = pd->db;

  dpm_control_fields fields;

  if (dpm_parse_control_fields (in, &fields))
    {
      char *package = dpm_control_fields_get (&fields, "Package");
      char *version = dpm_control_fields_get (&fields, "Version");
      char *architecture = dpm_control_fields_get (&fields, "Architecture");

      if (package == NULL)
	dyn_error ("Stanza without package");
      if (version == NULL)
	dyn_error ("Package without version: %s", package);
      if (architecture == NULL)
	dyn_error ("Package without architecture: %s", package);

      dpm_version ver = dpm_db_find_version (package, version, architecture);
      
      if (ver == NULL)
	{
	  dpm_package pkg = intern (pd->db, package);
	  ss_val db_fields[2*fields.n];

	  pd->n_new += 1;

	  for (int i = 0; i < fields.n; i++)
	    {
	      db_fields[2*i] = intern (pd->db, fields.names[i]);
	      db_fields[2*i+1] = intern (pd->db, fields.values[i]);
	    }
	  
	  char *desc = dpm_control_fields_get (&fields, "Description");
	  if (desc)
	    {
	      char *pos = strchr (desc, '\n');
	      if (pos)
		*pos = 0;
	    }

	  ver = ss_new (db->store, 0, 7,
			pkg,
			intern (pd->db, version),
			intern (pd->db, architecture),
			NULL,
			NULL,
			desc? intern (pd->db, desc) : NULL,
			ss_newv (db->store, 0,
				 2*fields.n, db_fields));
	  ss_dict_add (pd->db->available, pkg, ver);
	}

      if (pd->n_versions >= pd->max_versions)
	{
	  pd->max_versions += 1024;
	  pd->versions = dyn_realloc (pd->versions,
				       sizeof(dpm_version) * pd->max_versions);
	}
      pd->versions[pd->n_versions++] = ver;

      dpm_control_fields_free (&fields);
      return 1;
    }
  else
    return 0;
}

static void
dpm_db_update_index (dyn_val path, ss_dict *old_indices,
		     dpm_release_index release, dyn_val sha256)
{
  dyn_block 
    {
      parse_data pd;
      pd.db = dyn_get (cur_db);

      pd.max_versions = 0;
      pd.n_versions = 0;
      pd.versions = NULL;

      pd.n_new = 0;

      ss_val interned_path = intern (pd.db, path);

      dpm_acq_code code = dpm_acquire (dyn_to_string (path));
      if (code == DPM_ACQ_UNCHANGED
	  && ss_dict_get (old_indices, interned_path))
	{
	  dyn_print ("%v unchanged\n", path);
	  ss_dict_set (pd.db->indices, interned_path,
		       ss_dict_get (old_indices, interned_path));
	  return;
	}

      dyn_input in = dpm_acq_open_local (dyn_to_string (path));
      while (parse_package_stanza (&pd, in))
	;

      dyn_print ("%v: %d new\n", path, pd.n_new);

      dpm_package_index index = ss_new (pd.db->store, 0, 3,
					interned_path,
					release,
					ss_newv (pd.db->store, 0,
						 pd.n_versions,
						 pd.versions));
      free (pd.versions);

      ss_dict_set (pd.db->indices, interned_path, index);
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
  dpm_parse_control (in, release_field, &pd);

  for (int i = 0; i < pd.n_indices; i++)
    dpm_db_update_index (pd.index_paths[i], old_indices,
			 release, pd.index_sha256[i]);

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

