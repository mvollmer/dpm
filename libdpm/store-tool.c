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
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "store.h"

void
usage ()
{
  fprintf (stderr,
	   "Usage: store-tool gc FILE\n"
	   "       store-tool scan FILE\n"
	   "       store-tool info FILE\n"
	   "       store-tool dump FILE\n");
  exit (1);
}

void
cmd_gc (char *file)
{
  ss_store ss = ss_open (file, SS_WRITE);
  
  ss = ss_gc (ss);
}

void
cmd_scan (char *file)
{
  ss_store ss = ss_open (file, SS_READ);
  
  ss_scan_store (ss);
}

void
dump_reference (ss_store ss, ss_val o)
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
      printf (" (b%d) ", ss_id (ss, o));
      for (i = 0; i < l; i++)
	printf ("%c", isprint(b[i])? b[i] : '.');
      printf ("\n");
    }
  else
    printf (" r%d (%d)\n", ss_id (ss, o), ss_tag (o));
}

void
dump_object (ss_store ss, ss_val o)
{
  int i;

  if (o == NULL)
    printf ("NULL\n");
  else if (ss_is_int (o))
    printf ("%d\n", ss_to_int (o));
  else if (ss_is_blob (o))
    {
      int l = ss_len (o);
      printf ("b%d: (blob, %d bytes)\n", ss_id (ss, o), l);
      dump_reference (ss, o);
    }
  else
    {
      int n = ss_len (o);
      printf ("r%d: (tag %d, %d fields)\n", ss_id (ss, o), ss_tag (o), n);
      for (i = 0; i < n; i++)
	dump_reference (ss, ss_ref (o, i));
      if (n > 0)
	{
	  for (i = 0; i < n; i++)
	    {
	      ss_val r = ss_ref (o, i);
	      if (r && !ss_is_int (r) && !ss_is_blob (r))
		{
		  printf ("\n");
		  dump_object (ss, r);
		}
	    }
	}
    }
}

void
cmd_dump (char *file)
{
  ss_store ss = ss_open (file, SS_READ);
  
  dump_object (ss, ss_get_root (ss));
}

void
cmd_info (char *file)
{
  ss_store ss = ss_open (file, SS_READ);
  
  ss_dump_store (ss, file);
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
  else if (strcmp (argv[1], "info") == 0)
    {
      cmd_info (argv[2]);
    }
  else
    usage ();

  return 0;
}
