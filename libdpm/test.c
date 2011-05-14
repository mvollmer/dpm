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
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

#include "dpm.h"

#include "testlib.h"

// Utilities

#define S(x) dyn_from_string (x)
#define Q(x) dyn_read_string (#x)
#define I(x) dyn_open_string (x, -1)
#define L(x) #x "\n"

static void
failure_printer (const char *file, int line, const char *fmt, va_list ap)
{
  dyn_print ("%s:%d: %L\n", file, line, fmt, ap);
}

SET_FAILURE_PRINTER (failure_printer);

dyn_val
testsrc (const char *name)
{
  const char *dir = getenv ("TESTDATA");
  if (dir == NULL)
    dir = "./test-data";
  return dyn_format ("%s/%s", dir, name);
}

dyn_val
testdst (const char *name)
{
  if (mkdir ("./test-data", 0777) < 0 && errno != EEXIST)
    dyn_error ("Can't create ./test-data: %m");
  dyn_val n = dyn_format ("./test-data/%s", name);
  if (unlink (dyn_to_string (n)) < 0
      && errno != ENOENT)
    dyn_error ("Can't remove %v: %m", n);
  return n;
}

bool
streq (const char *a, const char *b)
{
  return strcmp (a, b) == 0;
}

bool
streqn (const char *a, const char *b, int b_len)
{
  return strncmp (a, b, b_len) == 0 && a[b_len] == '\0';
}

// Main

int
main (int argc, char **argv)
{
  return test_main (argc, argv);
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
  EXPECT_ABORT ("Out of memory.\n")
    {
      dyn_malloc (INT_MAX);
    }

  EXPECT_ABORT ("Out of memory.\n")
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
  dyn_foreach (x, range, 0, 10)
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

void
expect_numbers (dyn_input in)
{
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

DEFTEST (dyn_input)
{
  dyn_block
    {
      dyn_val name = testsrc ("numbers.txt");

      EXPECT (dyn_file_exists (name));

      dyn_input in = dyn_open_file (name);
      expect_numbers (in);

      dyn_input inz = dyn_open_file (testsrc ("numbers.gz"));
      expect_numbers (inz);

#ifdef HAVE_BZIP
      dyn_input in2 = dyn_open_file (testsrc ("numbers.bz2"));
      expect_numbers (in2);
#endif

    }
}

DEFTEST (dyn_output)
{
  dyn_block
    {
      dyn_val name = testdst ("output.txt");
      dyn_output out = dyn_create_file (name);
      for (int i = 0; i < 10000; i++)
	dyn_write (out, "%d\n", i);
      dyn_output_commit (out);

      dyn_output out2 = dyn_create_file (name);
      dyn_write (out2, "boo!\n");
      dyn_output_abort (out2);

      dyn_input in = dyn_open_file (name);

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
  EXPECT_STDERR (1, "unhandled test condition: foo\n")
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
  EXPECT_STDERR (1, "foo\n")
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

DEFTEST (store_basic)
{
  dyn_block
    {
      dyn_val name = testdst ("store.db");

      EXPECT_STDERR (1,
		     "Can't open non-existing.db: "
		     "No such file or directory\n")
	{
	  ss_open ("non-existing.db", SS_READ);
	}

      dyn_val s = ss_open (name, SS_TRUNC);
      EXPECT (ss_get_root (s) == NULL);

      dyn_val exp =
	dyn_format ("Can't lock %s: Resource temporarily unavailable\n",
		    name);

      EXPECT_STDERR (1, exp)
	{
	  ss_open (name, SS_WRITE);
	}

      ss_val x = ss_blob_new (s, 3, "foo");
      EXPECT (ss_is_blob (x));
      EXPECT (strncmp (ss_blob_start (x), "foo", 3) == 0);

      ss_set_root (s, x);
      EXPECT (ss_get_root (s) == x);

      s = ss_gc (s);
      EXPECT (ss_get_root (s) != x);
      
      x = ss_get_root (s);
      EXPECT (ss_is_blob (x));
      EXPECT (strncmp (ss_blob_start (x), "foo", 3) == 0);      
    }
}

DYN_DECLARE_STRUCT_ITER (const char *, sgb_words)
{
  dyn_input in;
  const char *cur;
};

void
sgb_words_init (sgb_words *iter)
{
  iter->in = dyn_ref (dyn_open_file (testsrc ("sgb-words.txt")));
  sgb_words_step (iter);
}

void
sgb_words_fini (sgb_words *iter)
{
  dyn_unref (iter->in); 
}

void
sgb_words_step (sgb_words *iter)
{
  dyn_input_set_mark (iter->in);
  if (dyn_input_find (iter->in, "\n"))
    {
      char *m = dyn_input_mutable_mark (iter->in);
      m[dyn_input_off (iter->in)] = '\0';
      dyn_input_advance (iter->in, 1);
      iter->cur = m;
    }
  else
    iter->cur = NULL;
}

bool
sgb_words_done (sgb_words *iter)
{
  return iter->cur == NULL;
}

const char *
sgb_words_elt (sgb_words *iter)
{ 
  return iter->cur; 
}

DYN_DECLARE_STRUCT_ITER (void, contiguous_usa)
{
  dyn_input in;
  const char *a;
  const char *b;
};

void
contiguous_usa_init (contiguous_usa *iter)
{
  iter->in = dyn_ref (dyn_open_file (testsrc ("contiguous-usa.dat")));
  contiguous_usa_step (iter);
}

void
contiguous_usa_fini (contiguous_usa *iter)
{
  dyn_unref (iter->in); 
}

void
contiguous_usa_step (contiguous_usa *iter)
{
  dyn_input_set_mark (iter->in);
  if (dyn_input_find (iter->in, " "))
    {
      char *m = dyn_input_mutable_mark (iter->in);
      m[dyn_input_off (iter->in)] = '\0';
      dyn_input_advance (iter->in, 1);
      iter->a = m;
    }
  else
    iter->a = NULL;

  dyn_input_set_mark (iter->in);
  if (dyn_input_find (iter->in, "\n"))
    {
      char *m = dyn_input_mutable_mark (iter->in);
      m[dyn_input_off (iter->in)] = '\0';
      dyn_input_advance (iter->in, 1);
      iter->b = m;
    }
  else
    iter->b = NULL;
}

bool
contiguous_usa_done (contiguous_usa *iter)
{
  return iter->a == NULL;
}

void
contiguous_usa_elt (contiguous_usa *iter)
{ 
}

DEFTEST (store_blob_vector)
{
  dyn_block
    {
      dyn_val s = ss_open (testdst ("store.db"), SS_TRUNC);
      ss_val v = ss_new (NULL, 0, 0);
      int i = 0;

      dyn_foreach (w, sgb_words)
	{
	  ss_val b = ss_blob_new (s, strlen (w), (void *)w);
	  EXPECT (ss_is_blob (b));
	  EXPECT (ss_len (b) == strlen (w));
	  EXPECT (strncmp (w, ss_blob_start (b), strlen (w)) == 0);

	  v = ss_insert (NULL, v, i++, b);
	}

      v = ss_copy (s, v);
      ss_set_root (s, v);
      s = ss_gc (s);
      v = ss_get_root (s);

      i = 0;
      dyn_foreach (w, sgb_words)
	{
	  EXPECT (ss_len (v) > i);

	  ss_val b = ss_ref (v, i++);
	  EXPECT (ss_is_blob (b));
	  EXPECT (ss_len (b) == strlen (w));
	  EXPECT (strncmp (w, ss_blob_start (b), strlen (w)) == 0);
	}
    }
}

DEFTEST (store_table)
{
  dyn_block
    {
      dyn_val s = ss_open (testdst ("store.db"), SS_TRUNC);
      ss_tab *t = ss_tab_init (s, NULL);

      ss_val blobs[6000];
      int i;

      // Put all words into the table.

      i = 0;
      dyn_foreach (w, sgb_words)
	{
	  ss_val b = ss_tab_intern_blob (t, strlen (w), (void *)w);

	  EXPECT (ss_is_blob (b));
	  EXPECT (ss_len (b) == strlen (w));
	  EXPECT (strncmp (w, ss_blob_start (b), strlen (w)) == 0);

	  EXPECT (i < 6000);
	  blobs[i++] = b;
	}

      // Check that they are in it.

      i = 0;
      dyn_foreach (w, sgb_words)
	{
	  ss_val b = ss_tab_intern_soft (t, strlen (w), (void *)w);
	  EXPECT (blobs[i] == b);
	  i += 1;
	}

      // Store the table and GC the store.  The table should
      // disappear since nothing references its entries.

      ss_set_root (s, ss_tab_finish (t));
      s = ss_gc (s);
      t = ss_tab_init (s, ss_get_root (s));

      i = 0;
      dyn_foreach (w, sgb_words)
	{
	  ss_val b = ss_tab_intern_soft (t, strlen (w), (void *)w);
	  EXPECT (b == NULL);
	  i += 1;
	}

      // Store the words again.

      i = 0;
      dyn_foreach (w, sgb_words)
	{
	  ss_val b = ss_tab_intern_blob (t, strlen (w), (void *)w);
	  blobs[i++] = b;
	}

      // Remember the first 200 words in a record and GC the rest
      // away.

      ss_val v = ss_newv (s, 0, 200, blobs);
      ss_val r = ss_new (s, 0, 2,
			 ss_tab_finish (t),
			 v);

      ss_set_root (s, r);
      s = ss_gc (s);
      r = ss_get_root (s);
      t = ss_tab_init (s, ss_ref (r, 0));
      v = ss_ref (r, 1);

      // Check that the first 200 are still there, and the rest have
      // disappeared.

      i = 0;
      dyn_foreach (w, sgb_words)
	{
	  ss_val b = ss_tab_intern_soft (t, strlen (w), (void *)w);
	  if (i < 200)
	    EXPECT (b == ss_ref (v, i));
	  else
	    EXPECT (b == NULL);
	  i++;
	}
    }
}

DEFTEST (store_table_foreach)
{
  dyn_block
    {
      dyn_val s = ss_open (testdst ("store.db"), SS_TRUNC);
      ss_tab *t = ss_tab_init (s, NULL);

      int count = 0;
      dyn_foreach (w, sgb_words)
	{
	  ss_tab_intern_blob (t, strlen (w), (void*)w);
	  count++;
	}

      ss_val tt = ss_tab_finish (t);
      t = ss_tab_init (s, tt);

      dyn_foreach (w, ss_tab_entries, t)
	{
	  EXPECT (ss_len (w) == 5);
	  count--;
	}

      EXPECT (count == 0);
    }
}

DEFTEST (store_dict_strong)
{
  dyn_block
    {
      dyn_val s = ss_open (testdst ("store.db"), SS_TRUNC);

      ss_tab *t = ss_tab_init (s, NULL);
      ss_dict *d = ss_dict_init (s, NULL, SS_DICT_STRONG);
      
      int i = 0;
      dyn_foreach (w, sgb_words)
	{
	  ss_val b = ss_tab_intern_blob (t, strlen(w), (void *)w);
	  ss_dict_set (d, b, ss_from_int (i));
	  i += 1;
	}

      ss_set_root (s, ss_new (s, 0, 2,
			      ss_tab_finish (t),
			      ss_dict_finish (d)));
      s = ss_gc (s);
      ss_val r = ss_get_root (s);
      t = ss_tab_init (s, ss_ref (r, 0));
      d = ss_dict_init (s, ss_ref (r, 1), SS_DICT_STRONG);

      i = 0;
      dyn_foreach (w, sgb_words)
	{
	  ss_val b = ss_tab_intern_soft (t, strlen(w), (void *)w);
	  EXPECT (b != NULL);

	  ss_val ii = ss_dict_get (d, b);
	  EXPECT (ss_is_int (ii));
	  EXPECT (ss_to_int (ii) == i);
	  i += 1;
	}

      ss_tab_abort (t);
      ss_dict_abort (d);
    }
}

DEFTEST (store_dict_strong_set)
{
  dyn_block
    {
      dyn_val s = ss_open (testdst ("store.db"), SS_TRUNC);

      ss_tab *t = ss_tab_init (s, NULL);
      ss_dict *d = ss_dict_init (s, NULL, SS_DICT_STRONG);
      
      dyn_foreach_iter (u, contiguous_usa)
	{
	  ss_val a = ss_tab_intern_blob (t, strlen(u.a), (void *)u.a);
	  ss_val b = ss_tab_intern_blob (t, strlen(u.b), (void *)u.b);
	  ss_dict_add (d, a, b);
	}

      ss_set_root (s, ss_new (s, 0, 2,
			      ss_tab_finish (t),
			      ss_dict_finish (d)));
      s = ss_gc (s);
      ss_val r = ss_get_root (s);
      t = ss_tab_init (s, ss_ref (r, 0));
      d = ss_dict_init (s, ss_ref (r, 1), SS_DICT_STRONG);

      dyn_foreach_iter (u, contiguous_usa)
	{
	  ss_val a = ss_tab_intern_blob (t, strlen(u.a), (void *)u.a);
	  ss_val b = ss_tab_intern_blob (t, strlen(u.b), (void *)u.b);

	  EXPECT (a != NULL);
	  EXPECT (b != NULL);

	  ss_val v = ss_dict_get (d, a);
	  bool found = false;
	  for (int i = 0; i < ss_len (v); i++)
	    {
	      if (ss_ref (v, i) == b)
		found = true;
	    }
	  EXPECT (found);
	}

      ss_tab_abort (t);
      ss_dict_abort (d);
    }
}

DEFTEST (store_dict_weak)
{
  dyn_block
    {
      dyn_val s = ss_open (testdst ("store.db"), SS_TRUNC);

      ss_tab *t = ss_tab_init (s, NULL);
      ss_dict *d = ss_dict_init (s, NULL, SS_DICT_WEAK_KEYS);
      
      int i = 0;
      dyn_foreach (w, sgb_words)
	{
	  ss_val b = ss_tab_intern_blob (t, strlen(w), (void *)w);
	  ss_dict_set (d, b, ss_from_int (i));
	  i += 1;
	}

      ss_set_root (s, ss_new (s, 0, 2,
			      ss_tab_finish (t),
			      ss_dict_finish (d)));
      s = ss_gc (s);
      ss_val r = ss_get_root (s);

      EXPECT (ss_ref (r, 0) == NULL);
      EXPECT (ss_ref (r, 1) == NULL);

      t = ss_tab_init (s, NULL);
      d = ss_dict_init (s, NULL, SS_DICT_WEAK_KEYS);
      ss_val v[20];

      i = 0;
      dyn_foreach (w, sgb_words)
	{
	  ss_val b = ss_tab_intern_blob (t, strlen(w), (void *)w);
	  ss_dict_set (d, b, ss_from_int (i));
	  if (i < 20)
	    v[i] = b;
	  i += 1;
	}
      
      ss_set_root (s, ss_new (s, 0, 3,
			      ss_tab_finish (t),
			      ss_dict_finish (d),
			      ss_newv (s, 0, 20, v)));
      s = ss_gc (s);
      r = ss_get_root (s);

      EXPECT (ss_ref (r, 0) != NULL);
      EXPECT (ss_ref (r, 1) != NULL);

      ss_val vv = ss_ref (r, 2);
      t = ss_tab_init (s, ss_ref (r, 0));
      d = ss_dict_init (s, ss_ref (r, 1), SS_DICT_WEAK_KEYS);
      
      i = 0;
      dyn_foreach (w, sgb_words)
	{
	  ss_val b = ss_tab_intern_soft (t, strlen(w), (void *)w);
	  if (i < 20)
	    {
	      ss_val ii = ss_dict_get (d, b);
	      EXPECT (b == ss_ref (vv, i));
	      EXPECT (ss_is_int (ii));
	      EXPECT (ss_to_int (ii) == i);
	    }
	  i += 1;
	}
    }
}

DEFTEST (store_dict_weak_set)
{
  dyn_block
    {
      dyn_val s = ss_open (testdst ("store.db"), SS_TRUNC);

      ss_tab *t = ss_tab_init (s, NULL);
      ss_dict *d = ss_dict_init (s, NULL, SS_DICT_WEAK_SETS);
      
      dyn_foreach_iter (u, contiguous_usa)
	{
	  ss_val a = ss_tab_intern_blob (t, strlen(u.a), (void *)u.a);
	  ss_val b = ss_tab_intern_blob (t, strlen(u.b), (void *)u.b);
	  ss_dict_add (d, a, b);
	}

      ss_set_root (s, ss_new (s, 0, 2,
			      ss_tab_finish (t),
			      ss_dict_finish (d)));
      s = ss_gc (s);
      ss_val r = ss_get_root (s);

      EXPECT (ss_ref (r, 0) == NULL);
      EXPECT (ss_ref (r, 1) == NULL);

      t = ss_tab_init (s, NULL);
      d = ss_dict_init (s, NULL, SS_DICT_WEAK_SETS);
      ss_val v[20];

      int i = 0;
      dyn_foreach_iter (u, contiguous_usa)
	{
	  ss_val a = ss_tab_intern_blob (t, strlen(u.a), (void *)u.a);
	  ss_val b = ss_tab_intern_blob (t, strlen(u.b), (void *)u.b);
	  ss_dict_add (d, a, b);
	  if (i < 20)
	    v[i] = b;
	  i++;
	}

      ss_set_root (s, ss_new (s, 0, 3,
			      ss_tab_finish (t),
			      ss_dict_finish (d),
			      ss_newv (s, 0, 20, v)));
      s = ss_gc (s);
      r = ss_get_root (s);
      
      EXPECT (ss_ref (r, 0) != NULL);
      EXPECT (ss_ref (r, 1) != NULL);

      ss_val vv = ss_ref (r, 2);
      t = ss_tab_init (s, ss_ref (r, 0));
      d = ss_dict_init (s, ss_ref (r, 1), SS_DICT_WEAK_SETS);

      i = 0;
      dyn_foreach_iter (u, contiguous_usa)
	{
	  ss_val a = ss_tab_intern_soft (t, strlen(u.a), (void *)u.a);
	  ss_val b = ss_tab_intern_soft (t, strlen(u.b), (void *)u.b);

	  if (i < 20)
	    {
	      EXPECT (a != NULL);
	      EXPECT (b == ss_ref (vv, i));
	      ss_val bb = ss_dict_get (d, a);
	      EXPECT (bb != NULL);
	      int found = 0;
	      for (int j = 0; j < ss_len (bb); j++)
		if (ss_ref (bb, j) == b)
		  found = 1;
	      EXPECT (found);
	    }
	  i++;
	}

    }
}

DEFTEST (store_dict_foreach)
{
  dyn_block
    {
      dyn_val s = ss_open (testdst ("store.db"), SS_TRUNC);

      ss_tab *t = ss_tab_init (s, NULL);
      ss_dict *d = ss_dict_init (s, NULL, SS_DICT_WEAK_SETS);
      
      int count = 0;
      dyn_foreach_iter (u, contiguous_usa)
	{
	  ss_val a = ss_tab_intern_blob (t, strlen(u.a), (void *)u.a);
	  ss_val b = ss_tab_intern_blob (t, strlen(u.b), (void *)u.b);
	  ss_dict_add (d, a, b);
	  count++;
	}

      int count2 = 0;
      dyn_foreach_iter (kv, ss_dict_entries, d)
	{
	  count2 += ss_len (kv.val);
	}
      EXPECT (count == count2);

      int count3 = 0;
      dyn_foreach_iter (km, ss_dict_entry_members, d)
	{
	  count3++;
	}
      EXPECT (count == count3);
    }
}

DEFTEST (parse_comma_fields)
{
  dyn_block
    {
      dyn_val fields[4];
      int i = 0;
      dyn_input in = dyn_open_string ("  foo   ,bar,,x y\tz\n z\ny  ", -1);
      dyn_foreach_iter (f, dpm_parse_comma_fields, in)
	{
	  EXPECT (i < 4);
	  fields[i++] = dyn_from_stringn (f.field, f.len);
	}

      EXPECT (i == 4);
      EXPECT (dyn_eq (fields[0], "foo"));
      EXPECT (dyn_eq (fields[1], "bar"));
      EXPECT (dyn_eq (fields[2], ""));
      EXPECT (dyn_eq (fields[3], "x y\tz\n z\ny"));
    }
}

DEFTEST (parse_relations)
{
  dyn_block
    {
      dyn_input in = dyn_open_string ("foo | bar (>= 1.0)", -1);
      int i = 0;
      dyn_foreach_iter (r, dpm_parse_relation_alternatives, in)
	{
	  EXPECT (i < 2);
	  switch (i)
	    {
	    case 0:
	      EXPECT (streqn ("foo", r.name, r.name_len));
	      EXPECT (r.op_len == 0);
	      EXPECT (r.version_len == 0);
	      break;
	    case 1:
	      EXPECT (streqn ("bar", r.name, r.name_len));
	      EXPECT (streqn (">=", r.op, r.op_len));
	      EXPECT (streqn ("1.0", r.version, r.version_len));
	      break;
	    }
	  i++;
	}
    }
}

DEFTEST (parse_lines)
{
  dyn_block
    {
      dyn_input in = dyn_open_file (testsrc ("lines.txt"));
      int i = 0;
      dyn_foreach_iter (l, dpm_parse_lines, in)
	{
	  EXPECT (i < 3);
	  switch (i)
	    {
	    case 0:
	      EXPECT (l.n_fields == 3);
	      EXPECT (streqn ("1", l.fields[0], l.field_lens[0]));
	      EXPECT (streqn ("2", l.fields[1], l.field_lens[1]));
	      EXPECT (streqn ("3", l.fields[2], l.field_lens[2]));
	      break;
	    case 1:
	      EXPECT (l.n_fields == 3);
	      EXPECT (streqn ("one", l.fields[0], l.field_lens[0]));
	      EXPECT (streqn ("two", l.fields[1], l.field_lens[1]));
	      EXPECT (streqn ("three", l.fields[2], l.field_lens[2]));
	      break;
	    case 2:
	      EXPECT (l.n_fields == 5);
	      EXPECT (streqn ("and,", l.fields[0], l.field_lens[0]));
	      EXPECT (streqn ("a,", l.fields[1], l.field_lens[1]));
	      EXPECT (streqn ("hundred", l.fields[2], l.field_lens[2]));
	      EXPECT (streqn (",thousand", l.fields[3], l.field_lens[3]));
	      EXPECT (streqn ("more", l.fields[4], l.field_lens[4]));
	      break;
	    }
	  i++;
	}
    }
}

DEFTEST (parse_control_fields)
{
  dyn_input in = dyn_open_file (testsrc ("control.txt"));

  int i, j;

  i = 0;
  while (dpm_parse_looking_at_control (in))
    {
      j = 0;
      dyn_foreach_iter (f, dpm_parse_control_fields, in)
	{
	  EXPECT (i < 2);
	  switch (i)
	    {
	    case 0:
	      switch (j)
		{
		case 0:
		  EXPECT (streqn ("Package", f.name, f.name_len));
		  EXPECT (streqn ("test", f.value, f.value_len));
		  break;
		case 1:
		  EXPECT (streqn ("Field", f.name, f.name_len));
		  EXPECT (streqn ("one   two three", f.value, f.value_len));
		  break;
		}
	      break;
	    case 1:
	      if (streqn ("Package", f.name, f.name_len))
		EXPECT (streqn ("xterm", f.value, f.value_len));
	      else if (streqn ("Version", f.name, f.name_len))
		EXPECT (streqn ("266-1", f.value, f.value_len));
	      else if (streqn ("Description", f.name, f.name_len))
		EXPECT (f.value_len == 1110);
	      break;
	    }
	  j++;
	}
      i++;
    }
}

DEFTEST (parse_ar_members)
{
  dyn_block
    {
      dyn_input in = dyn_open_file (testsrc ("pkg.deb"));

      int i = 0;
      dyn_foreach_iter (m, dpm_parse_ar_members, in)
	{
	  EXPECT (i < 3);
	  switch (i) 
	    {
	    case 0:
	      EXPECT (strcmp (m.name, "debian-binary") == 0);
	      EXPECT (m.size == 4);
	      EXPECT (dyn_input_looking_at (in, "2.0\n"));
	      break;
	    case 1:
	      EXPECT (strcmp (m.name, "control.tar.gz") == 0);
	      EXPECT (m.size == 910);
	      break;
	    case 2:
	      EXPECT (strcmp (m.name, "data.tar.gz") == 0);
	      EXPECT (m.size == 14932);
	      break;
	    }
	  i++;
	}
    }
}

DEFTEST (parse_tar_members)
{
  dyn_block
    {
      const char *lorem =
	"./neque-porro-quisquam-est-qui-dolorem-ipsum-quia-dolor-"
	"sit-amet-consectetur-adipisci-velit.txt";

      dyn_input in = dyn_open_file (testsrc ("src.tar"));

      int i = 0;
      dyn_foreach_iter (m, dpm_parse_tar_members, in)
	{
	  EXPECT (i < 5);
	  switch (i)
	    {
	    case 0:
	      EXPECT (m.type == DPM_TAR_DIRECTORY);
	      EXPECT (streq ("./", m.name));
	      EXPECT (m.size == 0);
	      break;
	    case 1:
	      EXPECT (m.type == DPM_TAR_SYMLINK);
	      EXPECT (streq (lorem, m.name));
	      EXPECT (streq ("hello.txt", m.target));
	      EXPECT (m.size == 0);
	      break;
	    case 2:
	      EXPECT (m.type == DPM_TAR_SYMLINK);
	      EXPECT (streq ("./hi.txt", m.name));
	      EXPECT (streq (lorem+2, m.target));
	      EXPECT (m.size == 0);
	      break;
	    case 3:
	      EXPECT (m.type == DPM_TAR_FILE);
	      EXPECT (streq ("./README", m.name));
	      EXPECT (m.size == 2099);
	      break;
	    case 4:
	      EXPECT (m.type == DPM_TAR_FILE);
	      EXPECT (streq ("./hello.txt", m.name));
	      EXPECT (m.size == 14);
	      EXPECT (dyn_input_looking_at (in, "Hello, World!\n"));
	      break;
	    }
	  i++;
	}
    }
}

DEFTEST (db_init)
{
  dyn_block
    {
      EXPECT (dpm_db_current () == NULL);

      dyn_let (dpm_database_name, testdst ("test.db"));
      dpm_db_open ();
      EXPECT (dpm_db_current () != NULL);
      EXPECT (dpm_db_package_id_limit () == 0);
      EXPECT (dpm_db_version_id_limit () == 0);
      dpm_db_done ();

      EXPECT (dpm_db_current () == NULL);
    }
}

#define UPDATE (o, x) dyn_db_origin_update (o, I(x))

DEFTEST (db_simple)
{
  dyn_block
    {
      dyn_let (dpm_database_name, testdst ("test.db"));
      dpm_db_open ();

      dpm_origin o = dpm_db_origin_find ("o");
      dpm_db_origin_update 
	(o, I(L(Package: foo           )
	      L(Version: 1.0           )
	      L(Architecture: all      )
	      L(Depends: bar (>= 1.0)  )
	      L(Conflicts: baz (<< 10) )));

      dyn_foreach_iter (p, dpm_db_origin_packages, o)
        {
          dyn_foreach (v, ss_elts, p.versions)
	    {
	      EXPECT (ss_streq (dpm_pkg_name (dpm_ver_package (v)), "foo"));
	      EXPECT (ss_streq (dpm_ver_version (v), "1.0"));
	      EXPECT (ss_streq (dpm_ver_architecture (v), "all"));
	      EXPECT (dpm_ver_tags (v) == NULL); 
	      EXPECT (dpm_ver_shortdesc (v) == NULL); 
	      EXPECT (dpm_ver_fields (v) == NULL);

	      dpm_relations rels = dpm_ver_relations (v);
	      EXPECT (dpm_rels_pre_depends (rels) == NULL);
	      EXPECT (dpm_rels_provides (rels) == NULL);
	      EXPECT (dpm_rels_replaces (rels) == NULL);
	      EXPECT (dpm_rels_breaks (rels) == NULL);
	      EXPECT (dpm_rels_recommends (rels) == NULL);
	      EXPECT (dpm_rels_enhances (rels) == NULL);
	      EXPECT (dpm_rels_suggests (rels) == NULL);

	      ss_val deps = dpm_rels_depends (rels);
	      EXPECT (ss_len (deps) == 1);
	      
	      dpm_relation dep = ss_ref (deps, 0);
	      EXPECT (ss_len (dep) == 3);
	      EXPECT (dpm_rel_op (dep, 0) == DPM_GREATEREQ);
	      EXPECT (ss_streq (dpm_pkg_name (dpm_rel_package (dep, 0)),
				"bar"));
	      EXPECT (ss_streq (dpm_rel_version (dep, 0), "1.0"));

	      ss_val confs = dpm_rels_conflicts (rels);
	      EXPECT (ss_len (confs) == 1);
	      
	      dpm_relation conf = ss_ref (confs, 0);
	      EXPECT (ss_len (conf) == 3);
	      EXPECT (dpm_rel_op (conf, 0) == DPM_LESS);
	      EXPECT (ss_streq (dpm_pkg_name (dpm_rel_package (conf, 0)),
				"baz"));
	      EXPECT (ss_streq (dpm_rel_version (conf, 0), "10"));
	    }
        }

      dpm_db_checkpoint ();
      dpm_db_done ();
    }
}

void
check_packages (dpm_origin origin, const char *pkg, ...)
{
  struct {
    const char *name;
    const char *version;
    bool seen;
  } packages[20];
  int n_packages = 0;

  va_list ap;
  va_start (ap, pkg);
  while (pkg)
    {
      packages[n_packages].name = pkg;
      packages[n_packages].version = va_arg (ap, const char *);
      packages[n_packages].seen = false;
      n_packages++;
      pkg = va_arg (ap, const char *);
    }
  va_end (ap);

  dyn_foreach_iter (p, dpm_db_origin_packages, origin)
    {
      dyn_foreach (v, ss_elts, p.versions)
	{
	  int i;
	  for (i = 0; i < n_packages; i++)
	    {
	      if (ss_streq (dpm_pkg_name (p.package),
			    packages[i].name)
		  && ss_streq (dpm_ver_version (v),
			       packages[i].version))
		{
		  EXPECT (packages[i].seen == false,
			  "%s seen twice", packages[i].name);
		  packages[i].seen = true;
		  break;
		}
	    }
	  EXPECT (i < n_packages, "unexpected package %r",
		  dpm_pkg_name (p.package));
	}
    }

  for (int i = 0; i < n_packages; i++)
    EXPECT (packages[i].seen,
	    "package %s not seen", packages[i].seen);
}

DEFTEST (db_unique_versions)
{
  dyn_block
    {
      const char *meta = 
	L(Package: foo                                  )
	L(Version: 1.0                                  )
	L(Architecture: all                             )
	L(SHA1: 1234567890123456789012345678901234567890)
	L(                                              )
	L(Package: bar                                  )
	L(Version: 1.0                                  )
	L(Architecture: all                             );

      dyn_let (dpm_database_name, testdst ("test.db"));
      dpm_db_open ();

      dpm_origin o1 = dpm_db_origin_find ("o1");
      dpm_db_origin_update (o1, I(meta));

      dpm_origin o2 = dpm_db_origin_find ("o2");
      dpm_db_origin_update (o2, I(meta));

      dpm_version foo_ver = NULL;
      dpm_version bar_ver = NULL;

      dyn_foreach_iter (p, dpm_db_origin_packages, o1)
	{
          dyn_foreach (v, ss_elts, p.versions)
	    {
	      if (ss_streq (dpm_pkg_name (p.package), "foo"))
		{
		  EXPECT (foo_ver == NULL);
		  foo_ver = v;
		}
	      else if (ss_streq (dpm_pkg_name (p.package), "bar"))
		{
		  EXPECT (bar_ver == NULL);
		  bar_ver = v;
		}
	      else
		EXPECT (0);
	    }
	}
	    
      dyn_foreach_iter (p, dpm_db_origin_packages, o2)
	{
          dyn_foreach (v, ss_elts, p.versions)
	    {
	      if (ss_streq (dpm_pkg_name (p.package), "foo"))
		EXPECT (foo_ver == v);
	      else if (ss_streq (dpm_pkg_name (p.package), "bar"))
		EXPECT (bar_ver != v);
	      else
		EXPECT (0);
	    }
	}
    }
}

DEFTEST (db_remove)
{
  dyn_block
    {
      const char *meta = 
	L(Package: foo                                  )
	L(Version: 1.0                                  )
	L(Architecture: all                             )
	L(                                              )
	L(Package: foo                                  )
	L(Version: 2.0                                  )
	L(Architecture: all                             )
	L(                                              )
	L(Package: bar                                  )
	L(Version: 1.0                                  )
	L(Architecture: all                             )
	L(                                              )
	L(Package: bar                                  )
	L(Version: 2.0                                  )
	L(Architecture: all                             )
	L(                                              )
	L(Package: baz                                  )
	L(Version: 1.0                                  )
	L(Architecture: all                             );

      dyn_let (dpm_database_name, testdst ("test.db"));
      dpm_db_open ();

      dpm_origin o = dpm_db_origin_find ("o");

      dpm_db_origin_update (o, I(meta));
      check_packages (o,
		      "foo", "1.0",
		      "foo", "2.0",
		      "bar", "1.0",
		      "bar", "2.0",
		      "baz", "1.0",
		      NULL);

      dpm_db_origin_update (o, I(L(Remove: foo 1.0)));
      check_packages (o,
		      "foo", "2.0",
		      "bar", "1.0",
		      "bar", "2.0",
		      "baz", "1.0",
		      NULL);

      dpm_db_origin_update (o, I(L(Remove: bar)));
      check_packages (o,
		      "foo", "2.0",
		      "baz", "1.0",
		      NULL);

      dpm_db_origin_update (o, I(L(Remove:)));
      check_packages (o,
		      NULL);
    }
}

void
setup_db (const char *origin, ...)
{
  va_list ap;
  va_start (ap, origin);

  dyn_let (dpm_database_name, testdst ("test.db"));

  dpm_db_open ();
  while (origin)
    {
      const char *meta = va_arg (ap, const char *);
      dyn_block
	{
	  dpm_origin o = dpm_db_origin_find (origin);
	  dpm_db_origin_update (o, I(meta));
	}
      origin = va_arg (ap, const char *);
    }
  dpm_db_checkpoint ();
}

void
setup_ws (const char *meta)
{
  dyn_let (dpm_database_name, testdst ("test.db"));

  dpm_db_open ();
  dpm_origin o = dpm_db_origin_find ("origin");

  dyn_block
    {
      dpm_db_origin_update (o, I(meta));
      dpm_db_checkpoint ();
    }

  dpm_ws_create (1);
  dyn_foreach_iter (p, dpm_db_origin_packages, o)
    {
      dyn_foreach (v, ss_elts, p.versions)
	dpm_ws_add_cand (v);
    }
  dpm_ws_start ();
}

void
setup_ws_1 (const char *meta, const char *cand)
{
  dyn_let (dpm_database_name, testdst ("test.db"));

  dpm_db_open ();
  dpm_origin o = dpm_db_origin_find ("origin");

  dyn_block
    {
      dpm_db_origin_update (o, I(meta));
      dpm_db_checkpoint ();
    }

  dpm_ws_create (1);
  dyn_foreach_iter (p, dpm_db_origin_packages, o)
    {
      if (ss_streq (dpm_pkg_name (p.package), cand))
	dyn_foreach (v, ss_elts, p.versions)
	  dpm_ws_add_cand_and_deps (v);
    }
  dpm_ws_start ();
}

dpm_cand
try_cand (const char *id)
{
  char p[200];
  strcpy (p, id);
  char *v = strchr (p, '_');
  if (v)
    {
      *v++ = '\0';
      
      dpm_package pkg = dpm_db_package_find (p);
      if (pkg)
	dyn_foreach (s, dpm_ws_seats, pkg)
	  dyn_foreach (c, dpm_seat_cands, s)
	    {
	      dpm_version ver = dpm_cand_version (c);
	      if ((ver && ss_streq (dpm_ver_version (ver), v))
		  || (!ver && strcmp (v, "null") == 0))
		return c;
	    }
    }

  return NULL;
}

dpm_cand
find_cand (const char *id)
{
  dpm_cand c = try_cand (id);
  EXPECT (c != NULL, "cand %s not found", id);
  return c;
}

void
check_deps (const char *from, ...)
{
  dpm_cand deps[10][20];
  int n_alts[10];
  int n_deps = 0;

  bool dep_found[10] = { 0, };

  va_list ap;
  va_start (ap, from);
  const char *to = va_arg (ap, const char *);
  while (to)
    {
      int i = 0;
      while (to)
	{
	  deps[n_deps][i++] = find_cand (to);
	  to = va_arg (ap, const char *);
	}
      n_alts[n_deps++] = i;
      to = va_arg (ap, const char *);
    }

  bool dep_eq (dpm_dep d, int i)
  {
    int n_alts_found = 0;
    dyn_foreach (a, dpm_dep_alts, d)
      for (int j = 0; deps[i][j]; j++)
	if (deps[i][j] == a)
	  {
	    n_alts_found++;
	    break;
	  }
    return n_alts_found == n_alts[i];
  }

  bool find_dep (dpm_dep d)
  {
    for (int i = 0; i < n_deps; i++)
      if (dep_eq (d, i))
	{
	  dep_found[i] = true;
	  return true;
	}
    return false;
  }

  dpm_cand f = find_cand (from);
  dyn_foreach (d, dpm_cand_deps, f)
    if (!find_dep (d))
      goto wrong;

  for (int i = 0; i < n_deps; i++)
    if (!dep_found[i])
      goto wrong;

  return;

 wrong:
  dyn_print ("expected deps:\n");
  for (int i = 0; i < n_deps; i++)
    {
      for (int j = 0; j < n_alts[i]; j++)
	{
	  dyn_print (" ");
	  dpm_cand_print_id (deps[i][j]);
	}
      dyn_print ("\n");
    }

  dyn_print ("actual deps:\n");
  dyn_foreach (d, dpm_cand_deps, f)
    {
      dyn_foreach (a, dpm_dep_alts, d)
	{
	  dyn_print (" ");
	  dpm_cand_print_id (a);
	}
      dyn_print ("\n");
    }

  EXPECT (false, "unexpected deps");
}

DEFTEST (ws_cands)
{
  dyn_block
    {
      setup_ws (L(Package: foo            )
		L(Version: 1.0            )
		L(Architecture: all       )
		L()
		L(Package: foo            )
		L(Version: 1.1            )
		L(Architecture: all       )
		L()
		L(Package: bar            )
		L(Version: 1.0            )
		L(Architecture: all       ));
		
      dpm_package p = dpm_db_package_find ("foo");

      int n = 0;
      dyn_foreach (s, dpm_ws_seats, p)
	dyn_foreach (c, dpm_seat_cands, s)
	  {
	    dpm_package pp = dpm_seat_package (dpm_cand_seat (c));
	    EXPECT (p == pp);
	    dpm_version v = dpm_cand_version (c);
	    if (v)
	      EXPECT (ss_streq (dpm_ver_version (v), "1.0")
		      || ss_streq (dpm_ver_version (v), "1.1"));
	    n++;
	  }
      EXPECT (n == 3);
    }
}

DEFTEST (ws_deps)
{
  dyn_block
    {
      setup_ws (L(Package: foo            )
		L(Version: 1.0            )
		L(Architecture: all       )
		L(Depends: bar (>= 1.1)   )
		L(Conflicts: not-there    )
		L()
		L(Package: bar            )
		L(Version: 1.0            )
		L(Architecture: all       )
		L()
		L(Package: bar            )
		L(Version: 1.1            )
		L(Architecture: all       )
		L(Conflicts: baz          )
		L()
		L(Package: baz            )
		L(Version: 1.0            )
		L(Architecture: all       )
		L(Provides: bar           ));

      check_deps ("foo_1.0",
		  "bar_1.1", "baz_1.0", NULL,
		  NULL);

      check_deps ("bar_1.1",
		  "baz_null", NULL,
		  NULL);
    }
}

DEFTEST (ws_cands_and_deps)
{
  dyn_block
    {
      setup_ws_1 (L(Package: foo            )
		  L(Version: 1.0            )
		  L(Architecture: all       )
		  "Depends: bar (>= 1.1), two (= 1.0)\n"
		  L(Conflicts: not-there    )
		  L()
		  L(Package: bar            )
		  L(Version: 1.0            )
		  L(Architecture: all       )
		  L()
		  L(Package: bar            )
		  L(Version: 1.1            )
		  L(Architecture: all       )
		  L(Conflicts: baz          )
		  L()
		  L(Package: baz            )
		  L(Version: 1.0            )
		  L(Architecture: all       )
		  L(Provides: bar           )
		  L()
		  L(Package: two            )
		  L(Version: 1.0            )
		  L(Architecture: all       )
		  L()
		  L(Package: two            )
		  L(Version: 2.0            )
		  L(Architecture: all       ),
		  "foo");

      EXPECT (try_cand ("foo_1.0") != NULL);
      EXPECT (try_cand ("bar_1.1") != NULL);
      EXPECT (try_cand ("bar_1.0") == NULL);
      EXPECT (try_cand ("baz_1.0") != NULL);
      EXPECT (try_cand ("two_1.0") != NULL);
      EXPECT (try_cand ("two_2.0") == NULL);
    }
}

DEFTEST (ws_select)
{
  dyn_block
    {
      setup_ws (L(Package: foo            )
		L(Version: 1.0            )
		L(Architecture: all       )
		L(Depends: bar (>= 1.1)   )
		L(Conflicts: not-there    )
		L()
		L(Package: bar            )
		L(Version: 1.0            )
		L(Architecture: all       )
		L()
		L(Package: bar            )
		L(Version: 1.1            )
		L(Architecture: all       )
		L(Conflicts: baz          )
		L()
		L(Package: baz            )
		L(Version: 1.0            )
		L(Architecture: all       )
		L(Provides: bar           ));

      dpm_cand foo_10 = find_cand ("foo_1.0");
      dpm_cand bar_10 = find_cand ("bar_1.0");
      dpm_cand bar_11 = find_cand ("bar_1.1");
      dpm_cand bar_null = find_cand ("bar_null");
      dpm_cand baz_10 = find_cand ("baz_1.0");
      dpm_cand baz_null = find_cand ("baz_null");

      EXPECT (!dpm_cand_satisfied (foo_10, 0));
      EXPECT (dpm_cand_satisfied (bar_11, 0));
      EXPECT (dpm_cand_satisfied (bar_10, 0));
      EXPECT (dpm_cand_satisfied (baz_10, 0));

      dpm_ws_select (bar_11, 0);
      EXPECT (dpm_cand_satisfied (foo_10, 0));
      dpm_ws_select (bar_10, 0);
      EXPECT (!dpm_cand_satisfied (foo_10, 0));
      dpm_ws_select (bar_null, 0);
      EXPECT (!dpm_cand_satisfied (foo_10, 0));

      dpm_ws_select (baz_10, 0);
      EXPECT (dpm_cand_satisfied (foo_10, 0));
      EXPECT (!dpm_cand_satisfied (bar_11, 0));
      dpm_ws_select (baz_null, 0);
      EXPECT (!dpm_cand_satisfied (foo_10, 0));
      EXPECT (dpm_cand_satisfied (bar_11, 0));
    }
}

DEFTEST (ws_goal_cand)
{
  dyn_block
    {
      const char *meta =
	L(Package: foo            )
	L(Version: 1.0            )
	L(Architecture: all       )
	"Depends: bar (>= 1.1), two (= 1.0)\n"
	L(Conflicts: not-there    )
	L()
	L(Package: bar            )
	L(Version: 1.0            )
	L(Architecture: all       )
	L()
	L(Package: bar            )
	L(Version: 1.1            )
	L(Architecture: all       )
	L(Conflicts: baz          )
	L()
	L(Package: baz            )
	L(Version: 1.0            )
	L(Architecture: all       )
	L(Provides: bar           )
	L()
	L(Package: two            )
	L(Version: 1.0            )
	L(Architecture: all       )
	L()
	L(Package: two            )
	L(Version: 2.0            )
	L(Architecture: all       )
	L(                        )
	L(Package: irrelevant     )
	L(Version: 1.0            )
	L(Architecture: all       );

      dyn_let (dpm_database_name, testdst ("test.db"));

      dpm_db_open ();
      dpm_origin o = dpm_db_origin_find ("origin");
      
      dyn_block
	{
	  dpm_db_origin_update (o, I(meta));
	  dpm_db_checkpoint ();
	}

      dpm_ws_create (1);

      dpm_candspec spec = dpm_candspec_new ();
      dpm_candspec_begin_rel (spec, false);
      dpm_candspec_add_alt (spec,
			    dpm_db_package_find ("foo"),
			    DPM_ANY, NULL);
      dpm_ws_set_goal_candspec (spec);

      dpm_cand goal = dpm_ws_get_goal_cand ();
      dpm_ws_add_cand_deps (goal);
      dpm_ws_start ();

      int n_deps = 0, n_alts = 0;
      dyn_foreach (d, dpm_cand_deps, goal)
	{
	  n_deps++;
	  dyn_foreach (a, dpm_dep_alts, d)
	    {
	      n_alts++;
	      EXPECT (a == find_cand ("foo_1.0"));
	    }
	}
      EXPECT (n_deps == 1);
      EXPECT (n_alts == 1);

      EXPECT (try_cand ("foo_1.0") != NULL);
      EXPECT (try_cand ("bar_1.1") != NULL);
      EXPECT (try_cand ("bar_1.0") == NULL);
      EXPECT (try_cand ("baz_1.0") != NULL);
      EXPECT (try_cand ("two_1.0") != NULL);
      EXPECT (try_cand ("two_2.0") == NULL);
    }
}

void
next_permutation (int *p, int n)
{
  int j = n-1;
  while (p[j] >= p[j+1])
    j--;
  if (j == 0)
    return;
  int l = n;
  while (p[j] >= p[l])
    l--;
  int t = p[j]; p[j] = p[l]; p[l] = t;
  int k = j+1;
  l = n;
  while (k < l)
    {
      int t = p[k]; p[k] = p[l]; p[l] = t;
      l -= 1;
      k += 1;
    }
}

DEFTEST (alg_queue)
{
  dyn_block
    {
      setup_ws (L(Package: foo          )
		L(Version: 1            )
		L(Architecture: all     )
		L()
		L(Package: foo          )
		L(Version: 2            )
		L(Architecture: all     )
		L()
		L(Package: foo          )
		L(Version: 3            )
		L(Architecture: all     )
		L()
		L(Package: foo          )
		L(Version: 4            )
		L(Architecture: all     )
		L()
		L(Package: foo          )
		L(Version: 5            )
		L(Architecture: all     )
		L()
		L(Package: foo          )
		L(Version: 6            )
		L(Architecture: all     )
		L()
		L(Package: foo          )
		L(Version: 7            )
		L(Architecture: all     )
		L()
		L(Package: foo          )
		L(Version: 8            )
		L(Architecture: all     )
		L());

      dpm_candpq q = dpm_candpq_new ();

      dpm_cand c[9];
      for (int i = 1; i < 9; i++)
	c[i] = find_cand (dyn_format ("foo_%d", i));

      int p[9] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };

      for (int m = 0; m < 1000; m++)
	{
	  for (int i = 1; i < 9; i++)
	    dpm_candpq_set (q, c[p[i]], p[i]);

	  for (int i = 8; i > 0; i--)
	    {
	      dpm_cand cand;
	      int prio;
	      EXPECT (dpm_candpq_pop_x (q, &cand, &prio));
	      EXPECT (cand == c[i]);
	      EXPECT (prio == i);
	    }
	  
	  for (int i = 0; i < 37; i++)
	    next_permutation (p, 8);
	}

      for (int m = 0; m < 1000; m++)
	{
	  for (int i = 1; i < 9; i++)
	    dpm_candpq_set (q, c[p[i]], p[i] + m*10);

	  for (int i = 0; i < 37; i++)
	    next_permutation (p, 8);
	}

      for (int i = 8; i > 0; i--)
	{
	  dpm_cand cand;
	  int prio;
	  EXPECT (dpm_candpq_pop_x (q, &cand, &prio));
	  EXPECT (cand == c[i]);
	  EXPECT (prio == i + 9990);
	}
    }
}
