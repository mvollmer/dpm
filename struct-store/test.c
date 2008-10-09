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
	  ss_object *b1, *b2;

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
grep_blob (ss_object *o, void *data)
{
  if (memmem (ss_blob_start (o), ss_len (o),
	      (char *)data, strlen ((char *)data)))
    printf ("%.*s\n", ss_len (o), ss_blob_start (o));
}

int
main (int argc, char **argv)
{
  ss_store *ss;
  ss_objtab *ot;
  ss_object *r;
  int i;

  if (argc < 2)
    usage ();

  if (strcmp (argv[1], "add") == 0)
    {
      ss = ss_open (argv[2], SS_WRITE, NULL);
      ot = ss_objtab_init (ss, ss_get_root (ss));
      
      ss_dump_store (ss, "load");

      while (argc > 3)
	{
	  intern_file (ss, ot, argv[3]);
	  argc--;
	  argv++;
	}
      
      ss_objtab_dump (ot);
      ss_set_root (ss, ss_objtab_finish (ot));
      ss_close (ss_maybe_gc (ss));
      printf ("found %d\n", found);
    }
  else if (strcmp (argv[1], "grep") == 0)
    {
      ss = ss_open (argv[2], SS_READ, NULL);
      ot = ss_objtab_init (ss, ss_get_root (ss));

      ss_objtab_foreach (ot, grep_blob, argv[3]);

      ss_close (ss);
    }
  else
    usage ();

  return 0;
}
