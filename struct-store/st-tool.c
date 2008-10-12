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
#include <stdlib.h>
#include <struct-store.h>

void
usage ()
{
  fprintf (stderr,
	   "Usage: st-tool gc FILE\n"
	   "       st-tool scan FILE\n"
	   "       st-tool dump FILE\n");
  exit (1);
}

void
cmd_gc (char *file)
{
  ss_store *ss = ss_open (file, SS_WRITE, NULL);
  
  ss = ss_gc (ss);
  ss_close (ss);
}

void
cmd_scan (char *file)
{
  ss_store *ss = ss_open (file, SS_READ, NULL);
  
  ss_scan_store (ss);
}

void
dump_reference (ss_val o)
{
  int i;

  if (o == NULL)
    printf (" nil\n");
  else if (ss_is_int (o))
    printf (" %d\n", ss_to_int (o));
  else if (ss_is_blob (o))
    {
      int l = ss_len (o);
      char *b = ss_blob_start (o);
      printf (" ");
      for (i = 0; i < l; i++)
	printf ("%c", isprint(b[i])? b[i] : '.');
      printf ("\n");
    }
  else
    printf (" %p (%d)\n", o, ss_tag (o));
}

void
dump_object (ss_val o)
{
  int i;

  if (o == NULL)
    printf ("NULL\n");
  else if (ss_is_int (o))
    printf ("%p: (int %d)\n", o, ss_to_int (o));
  else if (ss_is_blob (o))
    {
      int l = ss_len (o);
      char *b = ss_blob_start (o);
      printf ("%p: (blob, %d bytes)\n", o, l);
      dump_reference (o);
    }
  else
    {
      int n = ss_len (o);
      printf ("%p: (tag %d, %d fields)\n", o, ss_tag (o), n);
      for (i = 0; i < n; i++)
	dump_reference (ss_ref (o, i));
      if (n > 0)
	{
	  for (i = 0; i < n; i++)
	    {
	      ss_val r = ss_ref (o, i);
	      if (r && !ss_is_int (r) && !ss_is_blob (r))
		{
		  printf ("\n");
		  dump_object (r);
		}
	    }
	}
    }
}

void
cmd_dump (char *file)
{
  ss_store *ss = ss_open (file, SS_READ, NULL);
  
  dump_object (ss_get_root (ss));
}

int
main (int argc, char **argv)
{
  if (argc < 3)
    usage ();

  if (strcmp (argv[1], "scan") == 0)
    {
      cmd_scan (argv[2]);
    }
  else if (strcmp (argv[1], "dump") == 0)
    {
      cmd_dump (argv[2]);
    }
  else if (strcmp (argv[1], "gc") == 0)
    {
      cmd_gc (argv[2]);
    }
  else
    usage ();
}
