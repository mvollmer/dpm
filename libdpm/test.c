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

#include <dpm.h>

void
usage ()
{
  fprintf (stderr, "Usage: test ...\n");
  exit (1);
}

void
header (dpm_parse_state *ps,
	const char *name, int name_len,
	const char *value, int value_len,
	void *data)
{
  printf ("%.*s = >%.*s<\n", name_len, name, value_len, value);
}

int
main (int argc, char **argv)
{
  dpm_parse_state *ps;

  if (argc < 2)
    usage ();

  ps = dpm_parse_open_file (argv[1], NULL);
  while (dpm_parse_header (ps, header, NULL))
    printf ("- - - - - -\n");
  dpm_parse_close (ps);

  return 0;
}
