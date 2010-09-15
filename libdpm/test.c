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

#define S(x) dyn_from_string(x)
#define Q(x) dyn_read_string(#x)

#define DEFTEST(x) void test_##x ()
#define EXPECT(expr) expect((expr), #expr, __FILE__, __LINE__)
#define EXPECT_STATUS(check)			\
  if(expect_status (check, __FILE__, __LINE__))	\
    for (; true; exit(0))

#define EXPECT_ABORT EXPECT_STATUS (check_status_abort)
#define EXPECT_EXIT  EXPECT_STATUS (check_status_exit)

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
expect_status (void (*check) (int status, char *file, int line),
	       char *file, int line)
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

  check (status, file, line);

  return 0;
}

void
check_status_abort (int status, char *file, int line)
{
  if (!WIFSIGNALED (status) || WTERMSIG (status) != SIGABRT)
    {
      fprintf (stderr, "%s:%d: Expected abort\n", file, line);
      exit (1);
    }
}

void
check_status_exit (int status, char *file, int line)
{
  if (!WIFEXITED (status) || WEXITSTATUS (status) != 1)
    {
      fprintf (stderr, "%s:%d: Expected exit(1)\n", file, line);
      exit (1);
    }
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

  char *null = dyn_strdup (NULL);
  EXPECT (null == NULL);

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

void
unwind (int for_throw, void *data)
{
  *(int *)data = 1;
}

DEFTEST (dyn_unwind)
{
  int i = 0;

  dyn_begin ();
  dyn_on_unwind (unwind, &i);
  dyn_end ();

  EXPECT (i == 1);
}

DEFTEST (dyn_block)
{
  int i = 0;

  dyn_block
    {
      dyn_on_unwind (unwind, &i);
    }

  EXPECT (i == 1);

  dyn_block
    {
      i = 0;
      break;
      dyn_on_unwind (unwind, &i);
    }

  EXPECT (i == 0);
}

DYN_DECLARE_TYPE (container);

struct container_struct {
  int i;
  double d;
};

int containers_alive = 0;

void
container_unref (dyn_type *t, void *c)
{
  containers_alive -= 1;
}

int
container_equal (void *a, void *b)
{
  container ac = a, bc = b;
  return ac->i == bc->i && ac->d == bc->d;
}

DYN_DEFINE_TYPE (container, "container");

DEFTEST (dyn_type)
{
  dyn_block
    {
      container c = dyn_new (container);
      EXPECT (dyn_is (c, container_type));
      containers_alive = 1;

      EXPECT (strcmp (dyn_type_name (c), "container") == 0);

      dyn_val cc = dyn_ref (c);
      EXPECT (cc == c);

      dyn_unref (c);
      EXPECT (containers_alive == 1);
    }
  EXPECT (containers_alive == 0);

  dyn_block
    {
      {
	dyn_begin ();
	container c = dyn_new (container);
	EXPECT (dyn_is (c, container_type));
	containers_alive = 1;
	c = dyn_end_with (c);
      }
      EXPECT (containers_alive == 1);    
    }
  EXPECT (containers_alive == 0);
}

DEFTEST (dyn_string)
{
  dyn_block
    {
      dyn_val s = dyn_from_string ("hi");
      EXPECT (dyn_is_string (s));
      EXPECT (strcmp (dyn_to_string (s), "hi") == 0);
      EXPECT (dyn_eq (s, "hi"));

      dyn_val n = dyn_from_stringn ("hi1234", 2);
      EXPECT (dyn_is_string (n));
      EXPECT (strcmp (dyn_to_string (n), "hi") == 0);
      EXPECT (dyn_eq (n, "hi"));
    }
}

DEFTEST (dyn_pair)
{
  dyn_block
    {
      dyn_val p = dyn_pair (S("1st"), S("2nd"));
      EXPECT (dyn_is_pair (p));
      EXPECT (dyn_eq (dyn_first (p), "1st"));
      EXPECT (dyn_eq (dyn_second (p), "2nd"));

      dyn_val p2 = dyn_pair (S("1st"), S("2nd"));
      EXPECT (dyn_is_pair (p2));
      EXPECT (dyn_equal (p, p2));
    }
}

DEFTEST (dyn_seq)
{
  dyn_block
    {
      dyn_seq_builder b1;
      dyn_seq_start (b1);
      dyn_seq_append (b1, S("pre"));
      dyn_val pre = dyn_seq_finish (b1);

      dyn_seq_builder b3;
      dyn_seq_start (b3);
      dyn_seq_append (b3, S("post"));
      dyn_val post = dyn_seq_finish (b3);

      dyn_seq_builder b2;
      dyn_seq_start (b2);
      dyn_seq_append (b2, S("two"));
      dyn_seq_append (b2, S("three"));
      dyn_seq_prepend (b2, S("one"));
      dyn_seq_concat_front (b2, pre);
      dyn_seq_concat_back (b2, post);
      dyn_val s = dyn_seq_finish (b2);

      EXPECT (dyn_is_seq (s));
      EXPECT (dyn_len (s) == 5);
      EXPECT (dyn_eq (dyn_elt (s, 0), "pre"));
      EXPECT (dyn_eq (dyn_elt (s, 1), "one"));
      EXPECT (dyn_eq (dyn_elt (s, 2), "two"));
      EXPECT (dyn_eq (dyn_elt (s, 3), "three"));
      EXPECT (dyn_eq (dyn_elt (s, 4), "post"));
    }
}

DEFTEST (dyn_assoc)
{
  dyn_block
    {
      dyn_val a = NULL;

      a = dyn_assoc (S("key"), S("value-1"), a);
      a = dyn_assoc (S("key"), S("value-2"), a);
      a = dyn_assoc (S("key-2"), S("value"), a);

      EXPECT (dyn_eq (dyn_lookup (S("key"), a), "value-2"));
      EXPECT (dyn_eq (dyn_lookup (S("key-2"), a), "value"));
      EXPECT (dyn_lookup (S("key-3"), a) == NULL);
    }
}

void
func ()
{
}

void
func_free (void *data)
{
  *(int *)data = 1;
}

DEFTEST (dyn_func)
{
  int flag = 0;
  dyn_block
    {
      dyn_val f = dyn_func (func, &flag, func_free);
      EXPECT (dyn_is_func (f));
      EXPECT (dyn_func_code (f) == func);
      EXPECT (dyn_func_env (f) == &flag);
    }
  EXPECT (flag == 1);
}

DEFTEST (dyn_equal)
{
  dyn_block
    {
      dyn_val a = S("foo");

      dyn_seq_builder b1;
      dyn_seq_start (b1);
      dyn_seq_append (b1, S("foo"));
      dyn_seq_append (b1, S("bar"));
      dyn_seq_append (b1, S("baz"));
      dyn_val b = dyn_seq_finish (b1);

      dyn_seq_builder b2;
      dyn_seq_start (b2);
      dyn_seq_append (b2, S("foo"));
      dyn_seq_append (b2, S("bar"));
      dyn_seq_append (b2, S("baz"));
      dyn_val c = dyn_seq_finish (b2);
      
      EXPECT (!dyn_equal (a, b));
      EXPECT (dyn_equal (b, c));
    }
}

DYN_DEFINE_SCHEMA (maybe_string, (or string null));

DEFTEST (dyn_schema)
{
  dyn_block
    {
      dyn_val a = dyn_apply_schema (S("foo"),
				    Q(maybe_string));
      EXPECT (dyn_eq (a, "foo"));

      dyn_val b = dyn_apply_schema (NULL,
				    Q(maybe_string));
      EXPECT (b == NULL);

      dyn_val c = dyn_apply_schema (NULL,
				    Q((defaulted string foo)));
      EXPECT (dyn_eq (c, "foo"));

      dyn_val d = dyn_apply_schema (Q((foo bar)),
				    Q(any));
      EXPECT (dyn_equal (d, Q((foo bar))));

      dyn_val e = dyn_apply_schema (Q((foo bar)),
				    Q(seq));
      EXPECT (dyn_equal (e, Q((foo bar))));

      EXPECT_EXIT
	{
	  dyn_apply_schema (Q((foo bar)),
			    Q(pair));
	}

      
    }
}

DEFTEST (dyn_input)
{
  dyn_block
    {
      dyn_input in = dyn_open_file ("./test-data/numbers.txt");
      dyn_input_count_lines (in);

      for (int i = 0; i < 10000; i++)
	{
	  char *tail;

	  EXPECT (dyn_input_lineno (in) == i+1);

	  dyn_input_set_mark (in);
	  EXPECT (dyn_input_find (in, "\n"));
	  int ii = strtol (dyn_input_mark (in), &tail, 10);
	  EXPECT (tail == dyn_input_pos (in));
	  EXPECT (ii == i);
	  
	  dyn_input_advance (in, 1);
	}
    }
}

DEFTEST (dyn_output)
{
  dyn_block
    {
      dyn_output out = dyn_create_file ("./test-data/output.txt");
      for (int i = 0; i < 10000; i++)
	dyn_write (out, "%d\n", i);
      dyn_output_commit (out);

      dyn_output out2 = dyn_create_file ("./test-data/output.txt");
      dyn_write (out2, "boo!\n");
      dyn_output_abort (out2);

      dyn_input in = dyn_open_file ("./test-data/output.txt");

      for (int i = 0; i < 10000; i++)
	{
	  char *tail;

	  dyn_input_set_mark (in);
	  EXPECT (dyn_input_find (in, "\n"));
	  int ii = strtol (dyn_input_mark (in), &tail, 10);
	  EXPECT (tail == dyn_input_pos (in));
	  EXPECT (ii == i);
	  
	  dyn_input_advance (in, 1);
	}
    }
}

DEFTEST (dyn_read)
{
  dyn_block
    {
      dyn_val x;

      x = dyn_read_string ("foo");
      EXPECT (dyn_eq (x, "foo"));

      x = dyn_read_string ("  foo");
      EXPECT (dyn_eq (x, "foo"));

      x = dyn_read_string ("\"foo\"");
      EXPECT (dyn_eq (x, "foo"));

      x = dyn_read_string ("  \"foo\"");
      EXPECT (dyn_eq (x, "foo"));

      x = dyn_read_string ("  \"\\\\\"");
      EXPECT (dyn_eq (x, "\\"));

      x = dyn_read_string ("  \"\\n\\t\\v\"");
      EXPECT (dyn_eq (x, "\n\t\v"));

      x = dyn_read_string ("# comment\nfoo");
      EXPECT (dyn_eq (x, "foo"));

      x = dyn_read_string ("foo: bar");
      EXPECT (dyn_is_pair (x));
      EXPECT (dyn_eq (dyn_first (x), "foo"));
      EXPECT (dyn_eq (dyn_second (x), "bar"));

      x = dyn_read_string ("(foo bar)");
      EXPECT (dyn_is_seq (x));
      EXPECT (dyn_eq (dyn_elt (x, 0), "foo"));
      EXPECT (dyn_eq (dyn_elt (x, 1), "bar"));

      x = dyn_read_string ("(foo: bar)");
      EXPECT (dyn_is_seq (x));
      dyn_val y = dyn_elt (x, 0);
      EXPECT (dyn_is_pair (y));
      EXPECT (dyn_eq (dyn_first (y), "foo"));
      EXPECT (dyn_eq (dyn_second (y), "bar"));

      x = dyn_read_string ("");
      EXPECT (dyn_is_eof (x));
    }
}

dyn_var var_1[1];

DEFTEST (dyn_var)
{
  dyn_set (var_1, S("foo"));

  dyn_block
    {
      EXPECT (dyn_eq (dyn_get (var_1), "foo"));
      dyn_let (var_1, S("bar"));
      EXPECT (dyn_eq (dyn_get (var_1), "bar"));
      dyn_set (var_1, S("baz"));
      EXPECT (dyn_eq (dyn_get (var_1), "baz"));
    }
  
  EXPECT (dyn_eq (dyn_get (var_1), "foo"));
}

static void
throw_test (dyn_target *target, void *data)
{
  dyn_block
    {
      int *i = data;
      dyn_on_unwind (unwind, i);
      dyn_throw (target, S("test"));
      *i = 12;
    }
}

static void
dont_throw (dyn_target *target, void *data)
{
  int *i = data;
  *i = 12;
  return;
}

DEFTEST (dyn_catch)
{
  dyn_block
    {
      dyn_val x;
      int i;

      i = 0;
      x = dyn_catch (throw_test, &i);
      EXPECT (dyn_eq (x, "test"));
      EXPECT (i == 1);

      i = 0;
      x = dyn_catch (dont_throw, &i);
      EXPECT (x == NULL);
      EXPECT (i == 12);
    }
}

static void
unhandled_test (dyn_val val)
{
  fprintf (stderr, "unhandled test condition: %s\n", dyn_to_string (val));
  exit (1);
}

dyn_condition condition_test = {
  .name = "test",
  .unhandled = unhandled_test
};

void
signal_test (void *data)
{
  dyn_signal (&condition_test, S("foo"));
}

DEFTEST (dyn_signal)
{
  EXPECT_EXIT
    {
      dyn_signal (&condition_test, S("foo"));
    }

  dyn_block
    {
      dyn_val x;
      x = dyn_catch_condition (&condition_test, signal_test, NULL);
      EXPECT (dyn_eq (x, "foo"));
    }
}

void
signal_error (void *data)
{
  dyn_error ("foo: %s", "bar");
}

DEFTEST (dyn_error)
{
  EXPECT_EXIT
    {
      dyn_error ("foo");
    }

  dyn_block
    {
      dyn_val x;
      x = dyn_catch_error (signal_error, NULL);
      EXPECT (dyn_eq (x, "foo: bar"));
    }
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
