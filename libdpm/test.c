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
control_field (dyn_input in,
	       const char *name, int name_len,
	       const char *value, int value_len,
	       void *data)
{
  printf ("%.*s = >%.*s<\n", name_len, name, value_len, value);
}

void
tar_member (dyn_input in,
	    dpm_tar_member *info,
	    void *data)
{
  printf ("%c %8ld %5o %5d %5d %s -> %s\n", 
	  info->type,
	  info->size, info->mode, info->uid, info->gid,
	  info->name, info->target);
  if (strcmp (info->name, "./control") == 0)
    dpm_parse_control (in, control_field, NULL);
}

void
ar_member (dyn_input in,
	   const char *name,
	   void *data)
{
  if (strcmp (name, "control.tar.gz") == 0
      || strcmp (name, "data.tar.gz") == 0)
    {
      dyn_input inp = dyn_open_zlib (in);
      dpm_parse_tar (inp, tar_member, NULL);
    }
  else if (strcmp (name, "data.tar.bz2") == 0)
    {
      dyn_input inp = dyn_open_bz2 (in);
      dpm_parse_tar (inp, tar_member, NULL);
    }
}

int
main (int argc, char **argv)
{
  dyn_input in;

  if (argc < 2)
    usage ();

  in = dyn_open_file (argv[1]);
  dpm_parse_ar (in, ar_member, NULL);

  return 0;
}
