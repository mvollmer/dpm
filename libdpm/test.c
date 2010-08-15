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
#include <dlfcn.h>

#include "dpm.h"

#define DEFTEST(x) void x ()

DEFTEST (null)
{
}

DEFTEST (dyn_frames)
{
}

int
main (int argc, char **argv)
{
  if (argc != 2)
    {
      fprintf (stderr, "Usage: test TEST\n");
      exit (1);
    }

  void (*func)() = dlsym (NULL, argv[1]);

  if (func)
    {
      func ();
      exit (0);
    }
  else
    {
      fprintf (stderr, "Test %s is not defined.\n", argv[1]);
      exit (1);
    }
}
