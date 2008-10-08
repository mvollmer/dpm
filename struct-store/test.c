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


ss_object *
store_file (ss_store *ss, const char *file)
{
  int c;
  ss_object *p = NULL;

  FILE *f = fopen (file, "r");
  if (f == NULL)
    {
      fprintf (stderr, "%s: %m\n", file);
      exit (1);
    }

  while ((c = fgetc (f)) != EOF)
    {
      char cc = c;
      p = ss_new (ss, 0, 2,
		  ss_from_int (c),
		  p);
    }

  fclose (f);
  return p;
}

void
dump_file (ss_object *o)
{
  ss_object *b;

  while (o)
    {
      ss_assert (o, 0, 2);

      printf ("%c", ss_ref_int (o, 0));
      o = ss_ref (o, 1);
    }
}

void
intern_file (ss_store *ss, ss_objtab *ot, char *file)
{
  FILE *f;
  char *line = NULL;
  size_t len = 0;
  ssize_t n;
  int found = 0;

  f = fopen (file, "r");
  if (f)
    {
      while ((n = getline (&line, &len, f)) != -1)
	{
	  ss_object *b1, *b2;

	  if (n > 0 && line[n-1] == '\n')
	    n -= 1;
	  
	  b1 = ss_blob_new (ss, n, line);
	  b2 = ss_objtab_intern (ot, b1);

	  if (b1 != b2)
	    found++;
	}
    }
  
  free (line);
  fclose (f);

  printf ("found %d\n", found);
}

int
main (int argc, char **argv)
{
  ss_store *ss;
  ss_object *r;
  int i;

  ss = ss_open ("foo", SS_WRITE, NULL);

  // ss_dump_store (ss, "load");

#if 0  
  if (argc > 1)
    {
      r = store_file (ss, argv[1]);
      ss_set_root (ss, r);
    }
  else
    {
      // ss_scan_store (ss);
      r = ss_get_root (ss);
      dump_file (r);
    }
#else
  
  {
    ss_objtab *ot = ss_objtab_init (ss, ss_get_root (ss));
    while (argc > 1)
      {
	intern_file (ss, ot, argv[1]);
	argc--;
	argv++;
      }
    ss_objtab_dump (ot);
    ss_set_root (ss, ss_objtab_finish (ot));
  }
#endif

  ss_close (ss);
  return 0;
}
