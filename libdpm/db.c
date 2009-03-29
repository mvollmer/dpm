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

DPM_CONF_DECLARE (database, "database", string,
		  "The filename of the database.")

/* The root:

   - format              (string, "dpm-0")
   - strings             (string table)
   - available versions  (package name -> list of versions)
   - installed version   (package name -> something, maybe version)

   A version:

   - package             (interned string)
   - version             (string)
   - architecture        (interned string)
   - index               (interned string)
   - relations           (list of relations)
   - fields              (field name -> string)

   A relation:

   - type                (interned string)
   - op                  (int)
   - package             (interned string)
   - version             (string)

   All strings used as dictionary keys are interned, of course.
*/

struct dpm_db_struct {
  ss_store *store;
  
  ss_tab *strings;
  ss_dict *available;
  ss_dict *installed;
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
dpm_db_make (ss_store *store)
{
  dpm_db db = dyn_new (dpm_db);
  db->store = dyn_ref (store);
  db->strings = NULL;
  db->available = NULL;
  db->installed = NULL;
  return db;
}

static dyn_var cur_db[1];

static ss_val
ss_ref_safely (ss_val obj, int i)
{
  if (obj && !ss_is_blob (obj) && i < ss_len (obj))
    return ss_ref (obj, i);
  return NULL;
}

static int
ss_streq (ss_val obj, const char *str)
{
  return (obj
	  && ss_is_blob (obj)
	  && ss_len (obj) == strlen (str)
	  && strcmp (ss_blob_start (obj), str) == 0);
}

void
dpm_db_open ()
{
  const char *name = dpm_conf_string (database);
  if (name == NULL)
    name = "dpm.db";

  dpm_db db = dpm_db_make (ss_open (name, SS_WRITE));
  dyn_let (cur_db, db);

  ss_val root = ss_get_root (db->store);

  if (root && !ss_streq (ss_ref_safely (root, 0), "dpm-0"))
    dyn_error ("%s is not a dpm database", name);

  db->strings = ss_tab_init (db->store, ss_ref_safely (root, 1));
  db->available = ss_dict_init (db->store,
				ss_ref_safely (root, 2), SS_DICT_STRONG);
  db->installed = ss_dict_init (db->store,
				ss_ref_safely (root, 3), SS_DICT_STRONG);
}

void
dpm_db_checkpoint ()
{
  dpm_db db = dyn_get (cur_db);

  ss_val root = ss_new (db->store, 0, 4,
			ss_blob_new (db->store, 5, "dpm-0"),
			ss_tab_store (db->strings), 
			ss_dict_store (db->available),
			ss_dict_store (db->installed));
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

  ss_maybe_gc (db->store);

  db->store = NULL;
  db->strings = NULL;
  db->available = NULL;
  db->installed = NULL;

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

/* Utils that should be in store.c
 */

struct foreach_member_data {
  void (*func) (ss_val key, ss_val val, void *data);
  void *data;
};

static void
foreach_member (ss_val key, ss_val val, void *data)
{
  struct foreach_member_data *fmd = data;

  if (val)
    {
      for (int i = 0; i < ss_len (val); i++)
	{
	  ss_val member = ss_ref (val, i);
	  if (member)
	    fmd->func (key, member, fmd->data);
	}
    }
}

static void
ss_dict_foreach_member (ss_dict *d, 
			void (*func) (ss_val key, ss_val val, void *data),
			void *data)
{
  struct foreach_member_data fmd;
  fmd.func = func;
  fmd.data = data;
  ss_dict_foreach (d, foreach_member, &fmd);
}

/* Importing
 */

static ss_val
intern (dpm_db db, const char *string)
{
  return ss_tab_intern_blob (db->strings, strlen (string), (void *)string);
}

static void
note_available (dpm_db db, dpm_version version)
{
  dpm_package package = ss_ref (version, 0);
  ss_val avail = ss_dict_get (db->available, package);
  if (avail)
    for (int i = 0; i < ss_len (avail); i++)
      {
	dpm_version v = ss_ref (avail, i);
	if (ss_ref (v, 1) == ss_ref (version, 1)
	    && ss_ref (v, 2) == ss_ref (version, 2))
	  ss_dict_del (db->available, package, v);
      }
  ss_dict_add (db->available, package, version);
}

typedef struct {
  dpm_db db;
  ss_val package_key;
  ss_val version_key;
  ss_val architecture_key;
  ss_val index;

  ss_val package;
  ss_val version;
  ss_val architecture;
  ss_val fields[512];
  int n;
} parse_data;


void
remove_index_1 (ss_val key, ss_val member, void *data)
{
  parse_data *pd = data;
  if (ss_ref (member, 3) == pd->index)
    ss_dict_del (pd->db->available, key, member);
}

void
remove_index (parse_data *pd)
{
  ss_dict_foreach_member (pd->db->available, remove_index_1, pd);
}

void
header (dyn_input in,
	const char *name, int name_len,
	const char *value, int value_len,
	void *data)
{
  parse_data *pd = (parse_data *)data;
  dpm_db db = pd->db;

  ss_val key = ss_tab_intern_blob (db->strings, name_len, (void *)name);
  ss_val val = ss_blob_new (db->store, value_len, (void *)value);

  if (key == pd->package_key)
    pd->package = ss_tab_intern (db->strings, val);
  else if (key == pd->version_key)
    pd->version = ss_tab_intern (db->strings, val);
  else if (key == pd->architecture_key)
    pd->architecture = ss_tab_intern (db->strings, val);
  else
    {
      pd->fields[pd->n] = key;
      pd->fields[pd->n+1] = val;
      pd->n += 2;
      if (pd->n > 511)
	dyn_error ("too many fields");
    }
}

static int
parse_package_stanza (parse_data *pd, dyn_input in)
{
  dpm_db db = pd->db;

  pd->package = NULL;
  pd->version = NULL;
  pd->architecture = NULL;
  pd->n = 0;

  if (dpm_parse_control (in, header, pd))
    {
      if (pd->package == NULL)
	dyn_error ("Stanza without package");
      if (pd->version == NULL)
	dyn_error ("Package without version: %r", pd->package);
      if (pd->architecture == NULL)
	dyn_error ("Package without architecture: %r", pd->package);

      // dyn_print ("(adding %r %r)\n", pd->package, pd->version);
      ss_val version = ss_new (db->store, 0, 6,
			       pd->package,
			       pd->version,
			       pd->architecture,
			       pd->index,
			       NULL,
			       ss_newv (db->store, 0,
					pd->n, pd->fields));
      ss_dict_add (pd->db->available, pd->package, version);
      return 1;
    }
  else
    return 0;
}

void
dpm_db_update_packages (const char *file)
{
  dyn_block 
    {
      parse_data pd;
      pd.db = dyn_get (cur_db);
      pd.package_key = intern (pd.db, "Package");
      pd.version_key = intern (pd.db, "Version");
      pd.architecture_key = intern (pd.db, "Architecture");
      pd.index = intern (pd.db, basename (file));

      dyn_input in = dyn_open_file (file);
      remove_index (&pd);
      while (parse_package_stanza (&pd, in))
	;
    }
}

/* Dumping
 */

static void
dump_available (ss_val key, ss_val val, void *data)
{
  dyn_print ("%r:", key);
  for (int i = 0; i < ss_len (val); i++)
    dyn_print (" %r", ss_ref (ss_ref (val, i), 3));
  dyn_print ("\n");
}

void
dpm_db_dump ()
{
  dpm_db db = dyn_get (cur_db);

  ss_dict_foreach (db->available, dump_available, NULL);
}
