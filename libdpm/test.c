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
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dlfcn.h>

#include "dpm.h"

// Utilities

#define DEFTEST(x) void test_##x ()
#define EXPECT(expr) expect((expr), #expr, __FILE__, __LINE__)
#define EXPECT_ABORT				\
  if(expect_abort(__FILE__, __LINE__))		\
    for (; true; exit(0))

void
expect (int b, char *msg, char *file, int line)
{
  if (!b)
    {
      fprintf (stderr, "%s:%d: Expected %s\n", file, line, msg);
      exit (1);
    }
}

bool
expect_abort (char *file, int line)
{
  pid_t p = fork ();
  if (p < 0)
    {
      fprintf (stderr, "Can't fork: %m\n");
      exit (1);
    }

  if (p == 0)
    return 1;

  int status;
  if (waitpid (p, &status, 0) < 0)
    {
      fprintf (stderr, "Can't waitpid: %m\n");
      exit (1);
    }

  if (!WIFSIGNALED (status) || WTERMSIG (status) != SIGABRT)
    {
      fprintf (stderr, "%s:%d: Expected abort\n", file, line);
      exit (1);
    }

  return 0;
}

// Tests

DEFTEST (null)
{
}

DEFTEST (dyn_alloc)
{
  // Allocate some memory and access all of it.
  char *mem = dyn_malloc (100);
  for (int i = 0; i < 100; i++)
    mem[i] = 12;

  // Reallocate it.
  mem = dyn_realloc (mem, 200);
  for (int i = 0; i < 100; i++)
    EXPECT (mem[i] == 12);
  for (int i = 100; i < 200; i++)
    mem[i] = 12;

  // Dup it
  char *mem2 = dyn_memdup (mem, 200);
  for (int i = 0; i < 200; i++)
    EXPECT (mem2[i] == 12);
  
  // Free it
  free (mem);
  free (mem2);
}

DEFTEST (dyn_alloc_fail)
{
  EXPECT_ABORT
    {
      dyn_malloc (INT_MAX);
    }

  EXPECT_ABORT
    {
      void *mem = dyn_malloc (10);
      mem = dyn_realloc (mem, INT_MAX);
    }
}

DEFTEST (dyn_strdup)
{
  char *str = dyn_strdup ("foobarbaz");
  EXPECT (strcmp (str, "foobarbaz") == 0);

  char *strn = dyn_strndup ("foobarbaz", 6);
  EXPECT (strcmp (strn, "foobar") == 0);

  free (str);
  free (strn);
}

DYN_DECLARE_STRUCT_ITER (int, range, int start, int stop)
{
  int cur;
  int stop;
};

void
range_init (range *iter, int start, int stop) 
{
  iter->cur = start;
  iter->stop = stop;
}

void range_fini (range *iter) { }
void range_step (range *iter) { iter->cur += 1; }
bool range_done (range *iter) { return iter->cur >= iter->stop; }
int range_elt (range *iter) { return iter->cur; }

DEFTEST (dyn_iter)
{
  int sum = 0;
  dyn_foreach_ (x, range, 0, 10)
    sum += x;
  EXPECT (sum == 0+1+2+3+4+5+6+7+8+9);

  sum = 0;
  dyn_foreach_iter (x, range, 0, 10)
    sum += x.cur;
  EXPECT (sum == 0+1+2+3+4+5+6+7+8+9);
}

int
main (int argc, char **argv)
{
  if (argc != 2)
    {
      fprintf (stderr, "Usage: test TEST\n");
      exit (1);
    }

  char *name = dyn_format ("test_%s", argv[1]);
  void (*func)() = dlsym (NULL, name);

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
