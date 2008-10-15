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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <struct-store.h>

void
usage ()
{
  fprintf (stderr,
	   "Usage: test add  DB FILE...\n"
	   "       test dump DB\n");
  exit (1);
}

int found = 0;

ss_store *store;
ss_objtab *table;
ss_dict *files_dict;
ss_dict *packages_dict;

void
init (const char *file, int mode)
{
  ss_val r;

  store = ss_open (file, mode, NULL);
  r = ss_get_root (store);

  if (r && ss_len (r) == 3)
    {
      table = ss_objtab_init (store, ss_ref (r, 0));
      files_dict = ss_dict_init (store, ss_ref (r, 1));
      packages_dict = ss_dict_init (store, ss_ref (r, 2));
    }
  else
    {
      table = ss_objtab_init (store, NULL);
      files_dict = ss_dict_init (store, NULL);
      packages_dict = ss_dict_init (store, NULL);
    }
}

void
finish (int mode)
{
  if (mode == SS_WRITE)
    {
      ss_val r = ss_new (store, 0, 3,
			 ss_objtab_finish (table), 
			 ss_dict_finish (files_dict),
			 ss_dict_finish (packages_dict));
      ss_set_root (store, r);
      // ss_close (ss_maybe_gc (store));
      ss_close (store);
    }
  else
    ss_close (store);
}

ss_val
intern (const char *str)
{
  if (str)
    return ss_objtab_intern_blob (table, strlen (str) + 1, (void *)str);
  else
    return NULL;
}

ss_val
intern_soft (const char *str)
{
  if (str)
    return ss_objtab_intern_soft (table, strlen (str) + 1, (void *)str);
  else
    return NULL;
}

char *
string (ss_val blob)
{
  return (char *)ss_blob_start (blob);
}

ss_val
cons (ss_val car, ss_val cdr)
{
  return ss_new (store, 0, 2, car, cdr);
}

ss_val
delq (ss_val val, ss_val list)
{
  if (!list)
    return NULL;
  else if (ss_ref (list, 0) == val)
    return ss_ref (list, 1);
  else
    return cons (ss_ref (list, 0), delq (val, ss_ref (list, 1)));
}

int
memq (ss_val val, ss_val list)
{
  while (list)
    {
      if (ss_ref (list, 0) == val)
	return 1;
      list = ss_ref (list, 1);
    }
  return 0;
}

void
rem_list (ss_val pkg)
{
  ss_val files = ss_dict_get (files_dict, pkg);
  if (files)
    {
      int len = ss_len (files), i;
      for (i = 0; i < len; i++)
	{
	  ss_val file = ss_ref (files, i);
	  ss_dict_set (packages_dict, file, 
		       delq (pkg, ss_dict_get (packages_dict, file)));
	}
      printf ("%s: removed %d files\n", string (pkg), len);
    }

  ss_dict_set (files_dict, pkg, NULL);
}

void
set_list (ss_val pkg, char *file)
{
  FILE *f;
  char *line = NULL;
  size_t len = 0;
  ssize_t l;

  int n;
  ss_val lines[1024];
  
  rem_list (pkg);

  if (file == NULL)
    asprintf (&file, "/var/lib/dpkg/info/%s.list", string (pkg));

  n = 0;
  f = fopen (file, "r");
  if (f)
    {
      while ((l = getline (&line, &len, f)) != -1)
	{
	  if (l > 0 && line[l-1] == '\n')
	    l -= 1;
	  line[l] = 0;

	  if (n < 1024)
	    {
	      ss_val file = intern (line);
	      ss_val pkgs = ss_dict_get (packages_dict, file);
	      lines[n++] = file;
	      if (!memq (pkg, pkgs))
		ss_dict_set (packages_dict, file, cons (pkg, pkgs));
	    }
	}
      free (line);
      fclose (f);
    }

  printf ("%s: added %d files\n", string (pkg), n);
  ss_dict_set (files_dict, pkg, ss_newv (store, 0, n, lines));
}
  
void
dump_entry (ss_val key, ss_val val, void *data)
{
  if (ss_is_blob (val))
    printf ("%.*s: %.*s\n",
 	    ss_len (key), ss_blob_start (key),
	    ss_len (val), ss_blob_start (val));
  else
    printf ("%.*s: %p\n",
 	    ss_len (key), ss_blob_start (key), val);
}

void
dump_packages (ss_val file)
{
  ss_val packages = ss_dict_get (packages_dict, file);
  printf ("%.*s:", ss_len (file), ss_blob_start (file));
  while (packages)
    {
      ss_val pkg = ss_ref (packages, 0);
      printf (" %.*s", ss_len (pkg), ss_blob_start (pkg));
      packages = ss_ref (packages, 1);
    }
  printf ("\n");
}

void
grep_blob (ss_val val, void *data)
{
  if (memmem (ss_blob_start (val), ss_len (val),
	      (char *)data, strlen ((char *)data)))
    dump_packages (val);
}

void
dump_package (ss_val key, ss_val val, void *data)
{
  printf ("%s: %d files\n", string (key), ss_len (val));
}

void
dump_file (ss_val key, ss_val val, void *data)
{
  printf ("%.*s\n", ss_len (key), ss_blob_start (key));
}

int
main (int argc, char **argv)
{
  int i;

  if (argc < 2)
    usage ();

  if (strcmp (argv[1], "add") == 0)
    {
      init (argv[2], SS_WRITE);

      while (argc > 3)
	{
	  set_list (intern (argv[3]), NULL);
	  argc--;
	  argv++;
	}
      
      ss_objtab_dump (table);
      finish (SS_WRITE);
    }
  else if (strcmp (argv[1], "rem") == 0)
    {
      init (argv[2], SS_WRITE);

      while (argc > 3)
	{
	  rem_list (intern (argv[3]));
	  argc--;
	  argv++;
	}
      
      ss_objtab_dump (table);
      finish (SS_WRITE);
    }
  else if (strcmp (argv[1], "set-list") == 0)
    {
      init (argv[2], SS_WRITE);

      if (argc > 4)
	set_list (intern (argv[3]), argv[4]);
      
      ss_objtab_dump (table);
      finish (SS_WRITE);
    }
  else if (strcmp (argv[1], "list") == 0)
    {
      ss_val key, val;

      init (argv[2], SS_READ);

      if (argc > 3)
	{
	  int i;

	  ss_val files = ss_dict_get (files_dict, intern_soft (argv[3]));
	  if (files)
	    {
	      for (i = 0; i < ss_len (files); i++)
		{
		  ss_val b = ss_ref (files, i);
		  printf ("%.*s\n", ss_len (b), ss_blob_start (b));
		}
	    }
	  else
	    printf ("%s not found\n", argv[3]);
	}

      finish (SS_READ);
    }
  else if (strcmp (argv[1], "search") == 0)
    {
      init (argv[2], SS_READ);

      if (argc > 3)
	{
	  ss_val file = intern_soft (argv[3]);
	  if (file)
	    dump_packages (file);
	  else
	    ss_objtab_foreach (table, grep_blob, argv[3]);
	}

      finish (SS_READ);
    }
  else if (strcmp (argv[1], "packages") == 0)
    {
      init (argv[2], SS_READ);

      ss_dict_foreach (files_dict, dump_package, NULL);

      finish (SS_READ);
    }
  else if (strcmp (argv[1], "files") == 0)
    {
      init (argv[2], SS_READ);

      ss_dict_foreach (packages_dict, dump_file, NULL);

      finish (SS_READ);
    }
  else
    usage ();

  return 0;
}
