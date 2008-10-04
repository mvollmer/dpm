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
#include <struct-store.h>

void
dump_record (ss_record *r)
{
  int n = ss_record_field_count (r);
  int l = ss_record_blob_len (r);

  printf ("%p:\n", r);
  for (i = 0; i < n; i++)
    printf (" %p\n", ss_record_ref (r, i));
  if (l > 0)
    {
      char *b = ss_record_blob (r);
      printf (" ");
      for (i = 0; i < l; i++)
	printf ("%c", isprint (b[l])? b[l] : '.');
    }

  if (n > 0)
    {
      for (i = 0; i < n; i++)
	{
	  printf ("\n");
	  dump_record (ss_record_ref (r, i));
	}
    }
}

void
main ()
{
  ss_store *ss;
  ss_record *r;
  
  ss = ss_open ("foo", SS_READ_WRITE, NULL);
  r = ss_blob_new (4, "foo");
  ss_set_root (ss, r);
  ss_close (ss);
}
