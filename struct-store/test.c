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

ss_store *ss;
ss_objtab *ot;
ss_dict *d;

void
init (const char *file, int mode)
{
  ss_val r;

  ss = ss_open (file, mode, NULL);
  r = ss_get_root (ss);

  if (r)
    {
      ot = ss_objtab_init (ss, ss_ref (r, 0));
      d = ss_dict_init (ss, ss_ref (r, 1));
    }
  else
    {
      ot = ss_objtab_init (ss, NULL);
      d = ss_dict_init (ss, NULL);
    }
  
  // ss_dump_store (ss, "load");
}

void
finish (int mode)
{
  if (mode == SS_WRITE)
    {
      ss_val r = ss_new (ss, 0, 2,
			 ss_objtab_finish (ot), 
			 ss_dict_finish (d));
      ss_set_root (ss, r);
      // ss_close (ss_maybe_gc (ss));
      ss_close (ss);
    }
  else
    ss_close (ss);
}

ss_val
intern (const char *str)
{
  if (str)
    return ss_objtab_intern_blob (ot, strlen (str), (void *)str);
  else
    return NULL;
}

void
intern_file (ss_store *ss, ss_objtab *ot, char *file)
{
  FILE *f;
  char *line = NULL;
  size_t len = 0;
  ssize_t n;

  f = fopen (file, "r");
  if (f)
    {
      while ((n = getline (&line, &len, f)) != -1)
	{
	  ss_val b1, b2;

	  if (n > 0 && line[n-1] == '\n')
	    n -= 1;
	  
	  b1 = ss_blob_new (NULL, n, line);
	  b2 = ss_objtab_intern (ot, b1);

	  if (b1 != b2)
	    found++;
	  else
	    printf ("not found: %.*s\n", n, line);
	}
      free (line);
      fclose (f);
    }
}
  
void
grep_blob (ss_val o, void *data)
{
  if (memmem (ss_blob_start (o), ss_len (o),
	      (char *)data, strlen ((char *)data)))
    printf ("%.*s\n", ss_len (o), ss_blob_start (o));
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
	  intern_file (ss, ot, argv[3]);
	  argc--;
	  argv++;
	}
      
      ss_objtab_dump (ot);
      printf ("found %d\n", found);

      finish (SS_WRITE);
    }
  else if (strcmp (argv[1], "grep") == 0)
    {
      init (argv[2], SS_READ);

      ss_objtab_foreach (ot, grep_blob, argv[3]);

      finish (SS_READ);
    }
  else if (strcmp (argv[1], "set") == 0)
    {
      ss_val key, val;

      init (argv[2], SS_WRITE);

      if (argc > 4)
	{
	  key = intern (argv[3]);
	  val = ss_blob_new (ss, strlen (argv[4]), argv[4]);

	  ss_dict_set (d, key, val);
	}

      finish (SS_WRITE);
    }
  else if (strcmp (argv[1], "get") == 0)
    {
      ss_val key, val;

      init (argv[2], SS_READ);

      if (argc > 3)
	{
	  key = intern (argv[3]);
	  val = ss_dict_get (d, key);
	  
	  if (val && !ss_is_int (val) && ss_is_blob (val))
	    printf ("%.*s\n", ss_len (val), ss_blob_start (val));
	  else
	    printf ("%p\n", val);
	}

      finish (SS_READ);
    }
  else
    usage ();

  return 0;
}
