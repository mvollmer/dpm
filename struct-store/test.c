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
#include <dpm.h>

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
ss_tab *table;
ss_dict *package_files_dict;
ss_dict *file_packages_dict;
ss_dict *package_info_dict;

void
init (const char *file, int mode)
{
  ss_val r;
  int len;

  store = ss_open (file, mode, NULL);
  r = ss_get_root (store);
  len = r? ss_len (r) : 0;

  table = ss_tab_init (store, (len > 0)? ss_ref (r, 0) : NULL);
  package_files_dict = ss_dict_init (store, (len > 1)? ss_ref (r, 1) : NULL,
				     SS_DICT_STRONG);
  file_packages_dict = ss_dict_init (store, (len > 2)? ss_ref (r, 2) : NULL,
				     SS_DICT_STRONG);
  package_info_dict = ss_dict_init (store, (len > 2)? ss_ref (r, 3) : NULL,
				    SS_DICT_STRONG);
}

void
finish (int mode)
{
  if (mode == SS_WRITE)
    {
      ss_val r = ss_new (store, 0, 4,
			 ss_tab_finish (table), 
			 ss_dict_finish (package_files_dict),
			 ss_dict_finish (file_packages_dict),
			 ss_dict_finish (package_info_dict));
      ss_set_root (store, r);
      ss_close (ss_maybe_gc (store));
    }
  else
    ss_close (store);
}

ss_val
intern (const char *str)
{
  if (str)
    return ss_tab_intern_blob (table, strlen (str), (void *)str);
  else
    return NULL;
}

ss_val
intern_soft (const char *str)
{
  if (str)
    return ss_tab_intern_soft (table, strlen (str), (void *)str);
  else
    return NULL;
}

void
print_blob (ss_val v)
{
  printf ("%.*s", ss_len (v), ss_blob_start (v));
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
  ss_val files = ss_dict_get (package_files_dict, pkg);
  if (files)
    {
      int len = ss_len (files), i;
      for (i = 0; i < len; i++)
	{
	  ss_val file = ss_ref (files, i);
	  ss_dict_del (file_packages_dict, file, pkg);
	}
#if 0
      print_blob (pkg);
      printf (": removed %d files\n", len);
#endif
    }

  ss_dict_set (package_files_dict, pkg, NULL);
}

void
set_list (ss_val pkg, char *file)
{
  FILE *f;
  char *line = NULL;
  size_t len = 0;
  ssize_t l;

  int n;
  ss_val lines[10240];
  
  rem_list (pkg);

  if (file == NULL)
    asprintf (&file, "/var/lib/dpkg/info/%.*s.list",
	      ss_len (pkg), ss_blob_start (pkg));

  n = 0;
  f = fopen (file, "r");
  if (f)
    {
      while ((l = getline (&line, &len, f)) != -1)
	{
	  if (l > 0 && line[l-1] == '\n')
	    l -= 1;
	  line[l] = 0;

	  if (n < 10240)
	    {
	      ss_val file = intern (line);
	      lines[n++] = file;
	      ss_dict_add (file_packages_dict, file, pkg);
	    }
	}
      free (line);
      fclose (f);
    }

#if 0
  print_blob (pkg);
  printf (": added %d files\n", n);
#endif

  ss_dict_set (package_files_dict, pkg, ss_newv (store, 0, n, lines));
}

void
set_info (ss_val pkg, ss_val info)
{
  ss_dict_set (package_info_dict, pkg, info);
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
  ss_val packages = ss_dict_get (file_packages_dict, file);
  int len = packages? ss_len (packages) : 0, i;

  printf ("%.*s:", ss_len (file), ss_blob_start (file));
  for (i = 0; i < len; i++)
    {
      ss_val pkg = ss_ref (packages, i);
      printf (" %.*s", ss_len (pkg), ss_blob_start (pkg));
    }
  printf ("\n");
}

void
grep_blob (ss_val val, void *data)
{
  if (strstr (ss_blob_start (val), (char *)data))
    dump_packages (val);
}

void
dump_package (ss_val key, ss_val val, void *data)
{
  print_blob (key);
  printf (": %d files\n", ss_len (val));
}

void
dump_package_info (ss_val pkg, const char *field)
{
  ss_val info = ss_dict_get (package_info_dict, pkg);
  if (info)
    {
      if (field)
	{
	  ss_val key = intern_soft (field);
	  int n = ss_len (info), i;
	  for (i = 0; i < n; i += 2)
	    {
	      if (ss_ref (info, i) == key)
		{
		  print_blob (ss_ref (info, i+1));
		  printf ("\n");
		}
	    }
	}
      else
	{
	  int n = ss_len (info), i;
	  for (i = 0; i < n; i += 2)
	    {
	      print_blob (ss_ref (info, i));
	      printf (": ");
	      print_blob (ss_ref (info, i+1));
	      printf ("\n");
	    }
	}
    }
}

void
dump_file (ss_val key, ss_val val, void *data)
{
  printf ("%.*s\n", ss_len (key), ss_blob_start (key));
}

ss_val
parse_intern (dpm_parse_state *ps)
{
  return ss_tab_intern_blob (table,
			     dpm_parse_len (ps), (void *)dpm_parse_start (ps));
}

ss_val package_key = NULL;

typedef struct {
  ss_val pkg;
  ss_val info[512];
  int n;
} parse_data;

void
header (dpm_parse_state *ps,
	const char *name, int name_len,
	const char *value, int value_len,
	void *data)
{
  parse_data *pd = (parse_data *)data;

  pd->info[pd->n]   = ss_tab_intern_blob (table, name_len, (void *)name);
  pd->info[pd->n+1] = ss_blob_new (store, value_len, (void *)value);

  if (pd->info[pd->n] == package_key)
    pd->pkg = ss_tab_intern (table, pd->info[pd->n+1]);

  pd->n += 2;
  if (pd->n > 511)
    dpm_parse_abort (ps, "too many fields");
}

int
parse_package_stanza (dpm_parse_state *ps, ss_val *pkg, ss_val *pkg_info)
{
  parse_data pd;

  if (package_key == NULL)
    package_key = intern ("Package");

  pd.n = 0;
  pd.pkg = NULL;

  if (dpm_parse_header (ps, header, &pd))
    {
      if (pd.pkg == NULL)
	dpm_parse_abort (ps, "stanza without package");

      *pkg = pd.pkg;
      *pkg_info = ss_newv (store, 0, pd.n, pd.info);
      return 1;
    }
  else
    return 0;
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
      
      ss_tab_dump (table);
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
      
      ss_tab_dump (table);
      finish (SS_WRITE);
    }
  else if (strcmp (argv[1], "set-list") == 0)
    {
      init (argv[2], SS_WRITE);

      if (argc > 4)
	set_list (intern (argv[3]), argv[4]);
      
      ss_tab_dump (table);
      finish (SS_WRITE);
    }
  else if (strcmp (argv[1], "list") == 0)
    {
      ss_val key, val;

      init (argv[2], SS_READ);

      if (argc > 3)
	{
	  int i;

	  ss_val files = ss_dict_get (package_files_dict, intern_soft (argv[3]));
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
	    ss_tab_foreach (table, grep_blob, argv[3]);
	}

      finish (SS_READ);
    }
  else if (strcmp (argv[1], "info") == 0)
    {
      init (argv[2], SS_READ);

      if (argc > 3)
	{
	  ss_val pkg = intern_soft (argv[3]);
	  if (pkg)
	    dump_package_info (pkg, argv[4]);
	  else
	    printf ("%s not found\n", argv[3]);
	}

      finish (SS_READ);
    }
  else if (strcmp (argv[1], "packages") == 0)
    {
      init (argv[2], SS_READ);

      ss_dict_foreach (package_files_dict, dump_package, NULL);

      finish (SS_READ);
    }
  else if (strcmp (argv[1], "files") == 0)
    {
      init (argv[2], SS_READ);

      ss_dict_foreach (file_packages_dict, dump_file, NULL);

      finish (SS_READ);
    }
  else if (strcmp (argv[1], "import") == 0)
    {
      init (argv[2], SS_WRITE);

      if (argc > 2)
	{
	  ss_val pkg, info;
	  dpm_parse_state *ps = dpm_parse_open_file (argv[3], NULL);
	  while (parse_package_stanza (ps, &pkg, &info))
	    {
	      set_info (pkg, info);
	      set_list (pkg, NULL);
	    }
	  dpm_parse_close (ps);
	}

      finish (SS_WRITE);
    }
  else
    usage ();

  return 0;
}
