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

#define _GNU_SOURCE

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <setjmp.h>
#include <errno.h>

#include <sys/fcntl.h>
#include <sys/stat.h>

#include <zlib.h>
#ifdef HAVE_BZLIB
#include <bzlib.h>
#endif

#include "dyn.h"
#include "store.h"

/* Memory */

static void
dyn_oom ()
{
  fprintf (stderr, "Out of memory.\n");
  abort ();
}

void *
dyn_malloc (size_t size)
{
  void *mem = malloc (size);
  if (mem == NULL)
    dyn_oom ();
  return mem;
}

static int
grow_capacity (int c)
{
  if (c == 0)
    return 10;
  else if (c < 10000)
    return c * 2;
  else
    return c += 10000;
}

void *
dyn_calloc (size_t size)
{
  void *mem = calloc (size, 1);
  if (mem == NULL)
    dyn_oom ();
  return mem;
}

void *
dyn_realloc (void *old, size_t size)
{
  void *mem = realloc (old, size);
  if (mem == NULL)
    dyn_oom ();
  return mem;
}

void *
dyn_mgrow (void *ptr, int *capacityp, size_t size, int min_capacity)
{
  int capacity = *capacityp;
  if (capacity < min_capacity)
    {
      while (capacity < min_capacity)
	capacity = grow_capacity (capacity);
      ptr = realloc (ptr, capacity*size);
      if (ptr == NULL)
	dyn_oom ();
      *capacityp = capacity;
    }
  return ptr;
}

void *
dyn_strdup (const char *str)
{
  char *dup;

  if (str == NULL)
    return NULL;

  dup = strdup (str);
  if (dup == NULL)
    dyn_oom ();
  return dup;
}

void *
dyn_memdup (void *mem, int n)
{
  void *dup = dyn_malloc (n);
  memcpy (dup, mem, n);
  return dup;
}

void *
dyn_strndup (const char *str, int n)
{
  char *dup;

  if (str == NULL)
    return NULL;

  dup = strndup (str, n);
  if (dup == NULL)
    dyn_oom ();
  return dup;
}

/* Dynamic values */

#define DYN_HEAD(v)      (((uint32_t *)v)[-1])
#define DYN_REFCOUNT(v)  (DYN_HEAD(v)&0xFFFFFF)
#define DYN_TAG(v)       (DYN_HEAD(v)>>24)

static dyn_type *dyn_types[256];

void
dyn_type_register (dyn_type *type)
{
  static int dyn_next_tag = 0;

  if (type->tag == 0)
    {
      int t = ++dyn_next_tag;
      dyn_types[t] = type;
      type->tag = t;
    }
}

static int n_objects;

dyn_val
dyn_alloc (dyn_type *type, size_t size)
{
  if (type->tag == 0)
    abort();

  uint32_t *mem = dyn_calloc (size + sizeof (uint32_t));
  dyn_val val = (dyn_val)(mem + 1);
  mem[0] = (type->tag << 24) | 1;
  dyn_unref_on_unwind (val);
  n_objects += 1;
  return val;
}

int
dyn_is (dyn_val val, dyn_type *type)
{
  return val && DYN_TAG(val) == type->tag;
}

const char *
dyn_type_name (dyn_val val)
{
  if (val)
    return dyn_types[DYN_TAG(val)]->name;
  else
    return "null";
}

dyn_val
dyn_ref (dyn_val val)
{
  if (val)
    DYN_HEAD(val) += 1;

  return val;
}

void
dyn_unref (dyn_val val)
{
  if (val)
    {
      DYN_HEAD(val) -= 1;
      if (DYN_REFCOUNT(val) == 0)
	{
	  dyn_type *t = dyn_types[DYN_TAG(val)];
	  if (t->unref)
	    t->unref (t, val);
	  free (((uint32_t *)val)-1);
	  n_objects--;
	}
    }
}

struct dyn_string_struct {
  char chars[0];
};

static void
dyn_string_unref (dyn_type *type, void *object)
{
}

static int
dyn_string_equal (void *a, void *b)
{
  return strcmp (a, b) == 0;
}

DYN_DECLARE_TYPE (dyn_string);
DYN_DEFINE_TYPE (dyn_string, "string");

int
dyn_is_string (dyn_val val)
{
  return dyn_is (val, dyn_string_type);
}

const char *
dyn_to_string (dyn_val val)
{
  dyn_string string = val;
  return (const char *)string->chars;
}

dyn_val
dyn_from_string (const char *str)
{
  return dyn_from_stringn (str, strlen (str));
}

dyn_val
dyn_from_stringn (const char *str, int len)
{
  dyn_string string = dyn_alloc (dyn_string_type, len + 1);
  strncpy (string->chars, str, len);
  string->chars[len] = 0;
  return string;
}

/* Functions 
 */

struct dyn_func_struct {
  void (*code) ();
  void *env;
  void (*free_env) (void *env);
};

typedef struct dyn_func_struct dyn_func_struct;

static void
dyn_func_unref (dyn_type *type, void *object)
{
  dyn_func_struct *func = object;
  if (func->free_env)
    func->free_env (func->env);
}

static int
dyn_func_equal (void *a, void *b)
{
  return 0;
}

DYN_DEFINE_TYPE (dyn_func, "func");

int
dyn_is_func (dyn_val val)
{
  return dyn_is (val, dyn_func_type);
}

dyn_val
dyn_func (void (*code) (), void *env, void (*free_env) (void *env))
{
  dyn_func_struct *func = dyn_new (dyn_func);
  func->code = code;
  func->env = env;
  func->free_env = free_env;
  return (dyn_val)func;
}

void
(*dyn_func_code (dyn_val val))()
{
  dyn_func_struct *func = val;
  return func->code;
}

void *
dyn_func_env (dyn_val val)
{
  dyn_func_struct *func = val;
  return func->env;
}

/* Equality */

int
dyn_eq (dyn_val val, const char *str)
{
  return dyn_is_string (val) && strcmp (dyn_to_string (val), str) == 0;
}

int
dyn_equal (dyn_val a, dyn_val b)
{
  if (a == b)
    return 1;
  
  if (a == NULL || b == NULL)
    return 0;

  if (DYN_TAG(a) == DYN_TAG(b))
    return dyn_types[DYN_TAG(a)]->equal (a, b);

  return 0;
}

/* Dynamic extents */

typedef enum {
  dyn_kind_extent,
  dyn_kind_func,
  dyn_kind_var,
  dyn_kind_unref,
  dyn_kind_target
} dyn_kind;

typedef struct dyn_item {
  struct dyn_item *up;
  dyn_kind kind;
  union {
    struct {
      void (*func) (int for_throw, void *data);
      void *data;
    } func;
    struct {
      dyn_var *var;
      dyn_val oldval;
    } var;
    struct {
      dyn_val val;
    } unref;
    struct {
      dyn_target *target;
      dyn_val value;
    } target;
  } val;
} dyn_item;

/* XXX - use a thread local variable instead of a global.
 */

static dyn_item *windlist = NULL;

static void
dyn_add_unwind_item (dyn_item *item)
{
  item->up = windlist;
  windlist = item;
}

static void
dyn_unwind (dyn_item *goal, int for_throw)
{
  while (windlist != goal)
    {
      dyn_item *w = windlist;

      switch (w->kind)
	{
	case dyn_kind_extent:
	  break;
	case dyn_kind_func:
	  w->val.func.func (for_throw, w->val.func.data);
	  break;
	case dyn_kind_var:
	  dyn_unref (w->val.var.var->val);
	  w->val.var.var->val = w->val.var.oldval;
	  break;
	case dyn_kind_unref:
	  dyn_unref (w->val.unref.val);
	  break;
	case dyn_kind_target:
	  dyn_unref (w->val.target.value);
	  break;
	}
      windlist = w->up;
      free (w);
    }
}

void
dyn_begin ()
{
  dyn_item *item = dyn_malloc (sizeof (dyn_item));
  item->kind = dyn_kind_extent;
  dyn_add_unwind_item (item);
}

void
dyn_end ()
{
  dyn_item *w = windlist;
  while (w->kind != dyn_kind_extent)
    w = w->up;
  dyn_unwind (w->up, 0);
}

void
dyn_on_unwind (void (*func) (int for_throw, void *data), void *data)
{
  dyn_item *item = dyn_malloc (sizeof (dyn_item));
  item->kind = dyn_kind_func;
  item->val.func.func = func;
  item->val.func.data = data;
  dyn_add_unwind_item (item);
}

dyn_val
dyn_get (dyn_var *var)
{
  return var->val;
}

void
dyn_set (dyn_var *var, dyn_val val)
{
  dyn_ref (val);
  dyn_unref (var->val);
  var->val = val;
}

void
dyn_let (dyn_var *var, dyn_val val)
{
  dyn_item *item = dyn_malloc (sizeof (dyn_item));
  item->kind = dyn_kind_var;
  item->val.var.var = var;
  item->val.var.oldval = var->val;
  dyn_add_unwind_item (item);
  dyn_ref (val);
  var->val = val;
}

void
dyn_unref_on_unwind (dyn_val val)
{
  dyn_item *item = dyn_malloc (sizeof (dyn_item));
  item->kind = dyn_kind_unref;
  item->val.unref.val = val;
  dyn_add_unwind_item (item);  
}

dyn_val
dyn_end_with (dyn_val val)
{
  dyn_ref (val);
  dyn_end ();
  dyn_unref_on_unwind (val);
  return val;
}

static void
unwind_free (int for_throw, void *data)
{
  free (data);
}

void
dyn_on_unwind_free (void *mem)
{
  dyn_on_unwind (unwind_free, mem);
}

/* Catch and throw */

struct dyn_target { jmp_buf jmp; };

dyn_val
dyn_catch (void (*func) (dyn_target *target, void *data), void *data)
{
  dyn_target target;

  dyn_item *item = dyn_malloc (sizeof (dyn_item));
  item->kind = dyn_kind_target;
  item->val.target.target = &target;
  item->val.target.value = NULL;
  dyn_add_unwind_item (item);

  /* If we caught something, we leave our entry in the windlist so
     that the value is freed later.  When we didn't catch anything, we
     have to remove our entry so that dyn_throw doesn't think it is
     still active.
  */

  if (setjmp (target.jmp) == 0)
    {
      func (&target, data);
      dyn_unwind (item->up, 0);
      return NULL;
    }
  else
    return item->val.target.value;
}

void
dyn_throw (dyn_target *target, dyn_val value)
{
  dyn_item *w = windlist;
  while (w && (w->kind != dyn_kind_target
	       || w->val.target.target != target
	       || w->val.target.value != NULL))
    w = w->up;

  if (w)
    {
      w->val.target.value = dyn_ref (value);
      dyn_unwind (w, 1);
      longjmp (target->jmp, 1);
    }
  else
    {
      fprintf (stderr, "Uncaught throw.\n");
      exit (1);
    }
}

/* Conditions */

void
dyn_signal (dyn_condition *condition, dyn_val value)
{
  dyn_val handler = dyn_get (&(condition->handler));
  if (dyn_is_func (handler))
    dyn_func_code (handler) (value, dyn_func_env (handler));
  else if (condition->unhandled)
    condition->unhandled (value);

  fprintf (stderr, "Unhandled condition '%s'\n", condition->name);
  exit (1);
}

void
dyn_let_handler (dyn_condition *condition, dyn_val handler)
{
  dyn_let (&(condition->handler), handler);
}

struct dyn_cc_data {
  dyn_condition *cond;
  void (*func) (void *);
  void *data;
};

static void
cond_handler (dyn_val val, void *data)
{
  dyn_target *target = data;
  dyn_throw (target, val);
}

static void
call_with_cond_handler (dyn_target *target, void *data)
{
  struct dyn_cc_data *d = data;
  dyn_let_handler (d->cond,
		   dyn_func (cond_handler, target, NULL));
  d->func (d->data);
}

dyn_val
dyn_catch_condition (dyn_condition *condition,
		     void (*func) (void *), void *data)
{
  struct dyn_cc_data d = { condition, func, data };
  return dyn_catch (call_with_cond_handler, &d);
}

static void
dyn_unhandled_error (dyn_val val)
{
  fprintf (stderr, "%s\n", dyn_to_string (val));
  exit (1);
}

dyn_condition dyn_condition_error = {
  .name = "error",
  .unhandled = dyn_unhandled_error
};

void
dyn_errorv (const char *fmt, va_list ap)
{
  dyn_val message = dyn_formatv (fmt, ap);

#if 0
  if (context)
    {
      char *outer = NULL;

      if (dyn_is (context, dpm_stream_type))
	outer = dpm_sprintf ("%s:%d: %s",
			      dpm_stream_filename (context),
			      dpm_stream_lineno (context),
			      message);
      else
	{
	  dpm_print ("context: %V\n", context);
	  outer = dpm_sprintf ("In unknown context: %s", message);
	}

      free (message);
      message = outer;
    }
#endif

  dyn_signal (&dyn_condition_error, message);
}

void
dyn_error (const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  dyn_errorv (fmt, ap);
}

dyn_val
dyn_catch_error (void (*func) (void *data), void *data)
{
  return dyn_catch_condition (&dyn_condition_error, func, data);
}

/* Input streams */

int
dyn_file_exists (const char *filename)
{
  struct stat buf;
  return stat (filename, &buf) == 0;
}

static void dyn_input_unref (dyn_type *, void *);

static int
dyn_input_equal (void *a, void *b)
{
  return 0;
}

DYN_DEFINE_TYPE (dyn_input, "input");

struct dyn_input_struct {
  char *filename;
  int lineno;

  void *handle;
  int (*read) (void *handle, char *buf, int n);
  void (*close) (void *handle);

  int bufstatic;
  char *buf, *bufend, *buflimit;
  int bufsize;

  char *mark;
  char *pos;
};

static dyn_input
dyn_input_new ()
{
  dyn_input in;

  in = dyn_new (dyn_input);
  in->filename = NULL;
  in->lineno = 0;

  in->handle = NULL;
  in->read = NULL;
  in->close = NULL;

  in->bufstatic = 0;
  in->buf = NULL;
  in->bufsize = 0;
  in->bufend = in->buf;
  in->buflimit = NULL;

  in->mark = in->buf;
  in->pos = in->mark;

  return in;
}

static void
dyn_input_set_static_buffer (dyn_input in, const char *buf, int len)
{
  in->bufstatic = 1;
  in->buf = (char *)buf;
  in->bufsize = len;
  in->bufend = in->buf + in->bufsize;

  in->mark = in->buf;
  in->pos = in->mark;
}

static void
dyn_input_make_buffer_mutable (dyn_input in)
{
  if (in->bufstatic)
    {
      // XXX - be more clever for very big static buffers.
      char *buf = dyn_memdup (in->buf, in->bufsize);
      in->mark = buf + (in->mark - in->buf);
      in->pos = buf + (in->pos - in->buf);
      
      in->bufstatic = 0;
      in->buf = buf;
      in->bufend = in->buf + in->bufsize;
    }
}

void
dyn_input_push_limit (dyn_input in, int len)
{
  /* XXX - Allow more than one limit.
   */
  if (in->buflimit)
    dyn_error ("limit already set");
  in->buflimit = in->pos + len;
}

void
dyn_input_pop_limit (dyn_input in)
{
  if (!in->buflimit)
    dyn_error ("limit not set");
  dyn_input_advance (in, in->buflimit - in->pos);
  in->buflimit = NULL;
}

void
dyn_input_count_lines (dyn_input in)
{
  in->lineno = 1;
}

int
dyn_input_lineno (dyn_input in)
{
  return in->lineno;
}

//#define BUFMASK 0xF
#define BUFMASK 0xFFFF
#define BUFSIZE (BUFMASK+1)

static int
dyn_fd_read (void *handle, char *buf, int n)
{
  int fd = (int) handle;
  return read (fd, buf, n);
}

static void
dyn_fd_close (void *handle)
{
  int fd = (int) handle;
  close (fd);
}

static int
has_suffix (const char *str, const char *suffix)
{
  int len = strlen (str), suflen = strlen (suffix);
  return len >= suflen && strcmp (str+len-suflen, suffix) == 0;
}

dyn_input
dyn_open_file (const char *filename)
{
  int fd;
  dyn_input in = dyn_input_new ();

  in->filename = dyn_strdup (filename);
  in->read = dyn_fd_read;
  in->close = dyn_fd_close;

  fd = open (filename, O_RDONLY);
  in->handle = (void *)fd;
  if (fd < 0)
    dyn_error ("%m");

  if (has_suffix (filename, ".gz"))
    in = dyn_open_zlib (in);
#ifdef HAVE_BZLIB
  else if (has_suffix (filename, ".bz2"))
    in = dyn_open_bz2 (in);
#endif

  /* Read a bit already so that dyn_input_mark does not return NULL
     when it is the first thing being called.
  */
  dyn_input_grow (in, 1);
  return in;
}

dyn_input
dyn_open_string (const char *str, int len)
{
  dyn_input in = dyn_input_new (NULL);
  dyn_input_set_static_buffer (in, str, (len < 0)? strlen (str) : len);
  return in;
}

static const char *
zerrfmt (int ret)
{
  switch (ret) 
    {
    case Z_ERRNO:
      return "%m";
    case Z_STREAM_ERROR:
      return "invalid compression level";
    case Z_DATA_ERROR:
      return "invalid or incomplete deflate data";
    case Z_MEM_ERROR:
      return "out of memory";
    case Z_VERSION_ERROR:
      return "zlib version mismatch";
    default:
      return "zlib error %d";
    }
}

struct dyn_zlib_handle {
  dyn_input source;
  z_stream stream;
};

static int
dyn_zlib_read (void *handle, char *buf, int n)
{
  struct dyn_zlib_handle *z = handle;
  int ret;

  z->stream.next_out = (unsigned char *)buf;
  z->stream.avail_out = n;

  /* Loop until we have produced some output */
  while (z->stream.avail_out == n)
    {
      /* Get more input if needed. */
      if (z->stream.avail_in == 0)
	{
	  dyn_input_set_mark (z->source);
	  dyn_input_advance (z->source, dyn_input_grow (z->source, 1));
	  z->stream.next_in = (unsigned char *)dyn_input_mark (z->source);
	  z->stream.avail_in =
	    dyn_input_pos (z->source) - dyn_input_mark (z->source);
	}

      /* Make some progress */
      ret = inflate (&(z->stream), Z_NO_FLUSH);
      if (ret != Z_OK && ret != Z_STREAM_END)
	dyn_error (zerrfmt (ret), ret);

      if (ret == Z_STREAM_END)
	break;
    }

  return n - z->stream.avail_out;
}

static void
dyn_zlib_close (void *handle)
{
  struct dyn_zlib_handle *z = handle;
  inflateEnd (&(z->stream));
  dyn_unref (z->source);
  free (z);
}

dyn_input
dyn_open_zlib (dyn_input source)
{
  dyn_input in = dyn_input_new ();
  struct dyn_zlib_handle *z = dyn_malloc (sizeof (struct dyn_zlib_handle));
  int ret;

  z->stream.zalloc = Z_NULL;
  z->stream.zfree = Z_NULL;
  z->stream.opaque = Z_NULL;
  z->stream.avail_in = 0;
  z->stream.next_in = NULL;
  z->source = dyn_ref (source);

  in->handle = z;
  in->read = dyn_zlib_read;
  in->close = dyn_zlib_close;

  ret = inflateInit2 (&(z->stream), 32+15);
  if (ret != Z_OK)
    dyn_error (zerrfmt (ret), ret);

  return in;
}

#ifdef HAVE_BZLIB

static const char *
bzerrfmt (int ret)
{
  switch (ret) 
    {
    default:
      return "bz2 error %d";
    }
}

struct dyn_bzlib_handle {
  dyn_input source;
  bz_stream stream;
  int end_of_stream;
};

static int
dyn_bz2_read (void *handle, char *buf, int n)
{
  struct dyn_bzlib_handle *z = handle;
  int ret;

  z->stream.next_out = buf;
  z->stream.avail_out = n;

  /* Loop until we have produced some output */
  while (!z->end_of_stream && z->stream.avail_out == n)
    {
      /* Get more input if needed. */
      if (z->stream.avail_in == 0)
	{
	  dyn_input_set_mark (z->source);
	  dyn_input_advance (z->source, dyn_input_grow (z->source, 1));
	  z->stream.next_in = (char *)dyn_input_mark (z->source);
	  z->stream.avail_in =
	    dyn_input_pos (z->source) - dyn_input_mark (z->source);
	}

      /* Make some progress */
      ret = BZ2_bzDecompress (&(z->stream));
      if (ret != BZ_OK && ret != BZ_STREAM_END)
	dyn_error (bzerrfmt (ret), ret);

      if (ret == BZ_STREAM_END)
	{
	  z->end_of_stream = 1;
	  break;
	}
    }

  return n - z->stream.avail_out;
}

static void
dyn_bz2_close (void *handle)
{
  struct dyn_bzlib_handle *z = handle;
  BZ2_bzDecompressEnd (&(z->stream));
  dyn_unref (z->source);
  free (z);
}

dyn_input
dyn_open_bz2 (dyn_input source)
{
  dyn_input in = dyn_input_new ();
  struct dyn_bzlib_handle *z = dyn_malloc (sizeof (struct dyn_bzlib_handle));
  int ret;

  z->source = dyn_ref (source);
  z->stream.bzalloc = NULL;
  z->stream.bzfree = NULL;
  z->stream.opaque = NULL;

  z->stream.avail_in = 0;
  z->end_of_stream = 0;

  in->handle = z;
  in->read = dyn_bz2_read;
  in->close = dyn_bz2_close;

  ret = BZ2_bzDecompressInit (&(z->stream), 0, 0);
  if (ret != BZ_OK)
    dyn_error (bzerrfmt (ret), ret);

  return in;
}
#endif

void
dyn_input_unref (dyn_type *type, void *object)
{
  dyn_input in = object;

  if (in->close)
    in->close (in->handle);

  if (!in->bufstatic)
    free (in->buf);
  free (in->filename);
}

int
dyn_input_grow (dyn_input in, int min)
{
#if 0
  fprintf (stderr, "GROW min %d, mark %d, pos %d, end %d\n",
	   min,
	   in->mark - in->buf,
	   in->pos - in->buf,
	   in->bufend - in->buf);
#endif

  if (in->read && in->pos + min > in->bufend)
    {
      /* Need to read more input
       */

      if (in->pos + min - in->mark > in->bufsize)
	{
	  /* Need a bigger buffer
	   */
	  int newsize = ((in->pos + min - in->mark) + BUFMASK) & ~BUFMASK;
	  char *newbuf = dyn_malloc (newsize);
	  memcpy (newbuf, in->mark, in->bufend - in->mark);
	  in->bufend = newbuf + (in->bufend - in->mark);
	  if (in->buflimit)
	    in->buflimit = newbuf + (in->buflimit - in->mark);
	  in->pos = newbuf + (in->pos - in->mark);
	  in->mark = newbuf;
	  free (in->buf);
	  in->buf = newbuf;
	  in->bufsize = newsize;
	}
      else if (in->pos + min > in->buf + in->bufsize)
	{
	  /* Need to slide down mark to front of buffer
	   */
	  int d = in->mark - in->buf;
	  memcpy (in->buf, in->mark, in->bufend - in->mark);
	  in->bufend -= d;
	  if (in->buflimit)
	    in->buflimit -= d;
	  in->mark -= d;
	  in->pos -= d;
	}

      /* Fill buffer */
      while (in->pos + min > in->bufend)
	{
	  int l;
	  l = in->read (in->handle,
			in->bufend, in->buf + in->bufsize - in->bufend);
#if 0
	  fprintf (stderr, "READ %d of %d\n", l,
		   in->buf + in->bufsize - in->bufend);
#endif
	  if (l < 0)
	    dyn_error ("%m");
	  if (l == 0)
	    break;
	  in->bufend += l;
	}
    }

#if 0
  fprintf (stderr, "NOW  n %d, mark %d, pos %d, end %d\n",
	   in->bufend - in->pos,
	   in->mark - in->buf,
	   in->pos - in->buf,
	   in->bufend - in->buf);
#endif

  {
    char *end = in->bufend;
    if (in->buflimit && in->buflimit < in->bufend)
      end = in->buflimit;
    return end - in->pos;
  }
}

int
dyn_input_must_grow (dyn_input in, int n)
{
  int l = dyn_input_grow (in, n);
  if (l < n)
    dyn_error ("Unexpected end of file.");
  return l;
}

void
dyn_input_set_mark (dyn_input in)
{
  in->mark = in->pos;
}

const char *
dyn_input_mark (dyn_input in)
{
  return in->mark;
}

char *
dyn_input_mutable_mark (dyn_input in)
{
  dyn_input_make_buffer_mutable (in);
  return in->mark;
}

int
dyn_input_off (dyn_input in)
{
  return in->pos - in->mark;
}

const char *
dyn_input_pos (dyn_input in)
{
  return in->pos;
}

void
dyn_input_set_pos (dyn_input in, const char *pos)
{
  if (in->lineno > 0)
    {
      if (in->pos < pos)
	{
	  for (char *p = in->pos; p < pos; p++)
	    if (*p == '\n')
	      in->lineno++;
	}
      else
	{
	  for (char *p = in->pos; p >= pos; p--)
	    if (*p == '\n')
	      in->lineno--;
	}
    }

  in->pos = (char *)pos;
}

void
dyn_input_advance (dyn_input in, int n)
{
  dyn_input_must_grow (in, n);
  dyn_input_set_pos (in, in->pos + n);
}

int
dyn_input_looking_at (dyn_input in, const char *str)
{
  int n = strlen (str);
  if (dyn_input_grow (in, n) >= n)
    return memcmp (dyn_input_pos (in), str, n) == 0;
  else
    return 0;
}

int
dyn_input_find (dyn_input in, const char *delims)
{
  while (1)
    {
      const char *ptr, *end;
      int n = dyn_input_grow (in, 1);
      
      if (n == 0)
	return 0;

      for (ptr = dyn_input_pos (in), end = ptr + n; ptr < end; ptr++)
	if (strchr (delims, *ptr))
	  break;

      dyn_input_set_pos (in, ptr);
      if (ptr < end)
	return 1;
    }
}

int
dyn_input_find_after (dyn_input in, const char *delims)
{
  if (dyn_input_find (in, delims))
    {
      dyn_input_advance (in, 1);
      return 1;
    }
  return 0;
}

void
dyn_input_skip (dyn_input in, const char *chars)
{
  while (1)
    {
      const char *ptr, *end;
      int n = dyn_input_grow (in, 1);
      
      if (n == 0)
	return;

      for (ptr = dyn_input_pos (in), end = ptr + n; ptr < end; ptr++)
	if (!strchr (chars, *ptr))
	  break;

      dyn_input_set_pos (in, ptr);
      if (ptr < end)
	return;
    }
}

/* Output streams */

static void dyn_output_unref (dyn_type *, void *);

static int
dyn_output_equal (void *a, void *b)
{
  return 0;
}

DYN_DEFINE_TYPE (dyn_output, "output");

struct dyn_output_struct {
  void *handle;
  int (*write) (void *handle, char *buf, int n);
  void (*abort) (void *handle);
  dyn_val (*commit) (void *handle);

  char *bufstart;
  char *bufend;

  char *pos;
};

static dyn_output
dyn_output_new ()
{
  dyn_output out;

  out = dyn_new (dyn_output);

  out->handle = NULL;
  out->write = NULL;
  out->abort = NULL;
  out->commit = NULL;

  out->bufstart = NULL;
  out->bufend = NULL;
  out->pos = out->bufstart;
  
  return out;
}

void
dyn_output_unref (dyn_type *type, void *object)
{
  dyn_output out = object;

  if (out->handle)
    out->abort (out->handle);

  free (out->bufstart);
}

void
dyn_output_abort (dyn_output out)
{
  if (out->handle)
    {
      out->abort (out->handle);
      out->handle = NULL;
    }
}

dyn_val
dyn_output_commit (dyn_output out)
{
  dyn_output_flush (out);
  if (out->handle)
    {
      dyn_val val = out->commit (out->handle);
      out->handle = NULL;
      return val;
    }
  else
    return NULL;
}

void
dyn_output_flush (dyn_output out)
{
  if (out->write)
    {
      char *start = out->bufstart;
      while (start < out->pos)
	{
	  int n = out->write (out->handle, start, out->pos - start);
	  if (n < 0)
	    dyn_error ("%m");
	  if (n == 0)
	    dyn_error ("can't write");
	  start += n;
	}
      out->pos = out->bufstart;
    }
}

int
dyn_output_grow (dyn_output out, int min)
{
  /* Make sure that there are at least MIN bytes between POS and END.
   */

  if (out->pos + min > out->bufend)
    {
      /* Need to find room.  See if flushing helps.
       */

      dyn_output_flush (out);

      if (out->pos + min > out->bufend)
	{
	  /* Need to enlarge buffer.
	   */
	  int newsize = (out->pos + min - out->bufstart + BUFMASK) & ~BUFMASK;
	  char *newbuf = dyn_malloc (newsize);
	  memcpy (newbuf, out->bufstart, out->pos - out->bufstart);
	  free (out->bufstart);
	  out->pos = newbuf + (out->pos - out->bufstart);
	  out->bufstart = newbuf;
	  out->bufend = newbuf + newsize;
	}
    }

  return out->bufend - out->pos;
}

char *
dyn_output_pos (dyn_output out)
{
  return out->pos;
}

void
dyn_output_advance (dyn_output out, int n)
{
  out->pos += n;
}

static dyn_val
dyn_output_string_commit (void *handle)
{
  dyn_output out = handle;
  return dyn_from_stringn (out->bufstart, out->pos - out->bufstart);
}

dyn_output
dyn_create_output_string ()
{
  dyn_output out = dyn_output_new ();
  
  out->handle = out;
  out->write = NULL;
  out->abort = NULL;
  out->commit = dyn_output_string_commit;

  return out;
}

struct dyn_fd_handle {
  int fd;
};

static int
dyn_fd_write (void *handle, char *buf, int n)
{
  struct dyn_fd_handle *f = handle;
  return write (f->fd, buf, n);
}

static void
dyn_fd_abort (void *handle)
{
  free (handle);
}

static dyn_val
dyn_fd_commit (void *handle)
{
  dyn_fd_abort (handle);
  return NULL;
}

dyn_output
dyn_create_output_fd (int fd)
{
  struct dyn_fd_handle *f = dyn_malloc (sizeof (struct dyn_fd_handle));
  dyn_output out = dyn_output_new ();

  f->fd = fd;

  out->write = dyn_fd_write;
  out->abort = dyn_fd_abort;
  out->commit = dyn_fd_commit;
  out->handle = f;

  return out;
}

struct dyn_outfile_handle {
  char *name;
  char *tmpname;
  int fd;
};

static int
dyn_outfile_write (void *handle, char *buf, int n)
{
  struct dyn_outfile_handle *f = handle;
  return write (f->fd, buf, n);
}

static void
dyn_outfile_abort (void *handle)
{
  struct dyn_outfile_handle *f = handle;

  if (f->fd >= 0)
    {
      close (f->fd);
      unlink (f->tmpname);
    }

  free (f->name);
  free (f->tmpname);
  free (f);
}

static dyn_val
dyn_outfile_commit (void *handle)
{
  struct dyn_outfile_handle *f = handle;

  if (f->fd >= 0)
    {
      if (close (f->fd) < 0)
	dyn_error ("error closing %s: %m", f->tmpname);
      if (rename (f->tmpname, f->name) < 0)
	dyn_error ("can't rename %s to %s: %m", f->tmpname, f->name);
    }

  free (f->name);
  free (f->tmpname);
  free (f);

  return NULL;
}

dyn_output
dyn_create_file (const char *name)
{
  struct dyn_outfile_handle *f =
    dyn_malloc (sizeof (struct dyn_outfile_handle));
  
  f->fd = -1;
  f->name = dyn_strdup (name);
  f->tmpname = dyn_malloc (strlen (name) + 7);
  strcpy (f->tmpname, name);
  strcat (f->tmpname, ".XXXXXX");

  dyn_output out = dyn_output_new ();

  out->write = dyn_outfile_write;
  out->abort = dyn_outfile_abort;
  out->commit = dyn_outfile_commit;
  out->handle = f;

  f->fd = mkstemp (f->tmpname);
  if (f->fd < 0)
    dyn_error ("can't create %s: %m", f->name);

  return out;
}

dyn_output dyn_stdout;

dyn_val
dyn_formatv (const char *fmt, va_list ap)
{
  dyn_begin ();
  dyn_output out = dyn_create_output_string ();
  dyn_writev (out, fmt, ap);
  return dyn_end_with (dyn_output_commit (out));
}

dyn_val
dyn_format (const char *fmt, ...)
{
  dyn_val val;
  va_list ap;
  va_start (ap, fmt);
  val = dyn_formatv (fmt, ap);
  va_end (ap);
  return val;
}

void
dyn_print  (const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  dyn_writev (dyn_stdout, fmt, ap);
  va_end (ap);
  dyn_output_flush (dyn_stdout);
}

void
dyn_write (dyn_output out, const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  dyn_writev (out, fmt, ap);
  va_end (ap);
}

static void
dyn_write_string (dyn_output out, const char *str, int len)
{
  dyn_output_grow (out, len);
  memcpy (dyn_output_pos (out), str, len);
  dyn_output_advance (out, len);
}

static void
dyn_write_quoted (dyn_output out, const char *str, int len)
{
  int avail = dyn_output_grow (out, len + 2);
  char *pos = dyn_output_pos (out);

  *pos++ = '"';
  while (len > 0)
    {
      if (avail < 3)
	{
	  dyn_output_advance (out, pos - dyn_output_pos (out));
	  avail = dyn_output_grow (out, 1);
	  pos = dyn_output_pos (out);
	}

      switch (*str)
	{
	case '\0':
	  *pos++ = '\\';
	  *pos++ = '0';
	  break;
	case '\n':
	  *pos++ = '\\';
	  *pos++ = 'n';
	  break;
	case '\t':
	  *pos++ = '\\';
	  *pos++ = 't';
	  break;
	case '"':
	  *pos++ = '\\';
	  *pos++ = '"';
	  break;
	default:
	  *pos++ = *str;
	  break;
	}
      str++;
      len--;
    }
  *pos++ = '"';
  dyn_output_advance (out, pos - dyn_output_pos (out));
}

static void
dyn_write_val (dyn_output out, dyn_val val, int quoted)
{
  if (val == NULL)
    {
      dyn_write (out, "<nil>");
    }
  else if (dyn_is_string (val))
    {
      const char *str = dyn_to_string (val);
      if (quoted
	  && (strchr (str, '(')
	      || strchr (str, ')')
	      || strchr (str, '"')
	      || strchr (str, ' ')
	      || strchr (str, '%')
	      || strchr (str, '\t')
	      || strchr (str, '\n')))
	dyn_write (out, "%S", str);
      else
	dyn_write (out, "%s", str);
    }
  else
    dyn_write (out, "<%s>", dyn_type_name (val));
}

static void
dyn_write_ss_val (dyn_output out, ss_val val, int quoted)
{
  if (val == NULL)
    dyn_write (out, "<nil>");
  else if (ss_is_int (val))
    dyn_write (out, "%d", ss_to_int (val));
  else if (ss_is_blob (val))
    {
      if (quoted)
	dyn_write_quoted (out, (char *)ss_blob_start (val), ss_len (val));
      else
	dyn_write_string (out, (char *)ss_blob_start (val), ss_len (val));
    }
  else
    dyn_write (out, "<record>");
}

void
dyn_writev (dyn_output out, const char *fmt, va_list ap)
{
  int err = errno;

  while (*fmt)
    {
      if (*fmt == '%')
	{
	  fmt++;
	  bool l_mod = false;
	  if (*fmt == 'l')
	    {
	      l_mod = true;
	      fmt++;
	    }
	  switch (*fmt)
	    {
	    case '\0':
	      return;
	    case 's':
	      {
		char *str = va_arg (ap, char *);
		int len;
		if (l_mod)
		  len = va_arg (ap, int);
		else
		  len = strlen (str);
		dyn_write_string (out, str, len);
	      }
	      break;
	    case 'S':
	      {
		char *str = va_arg (ap, char *);
		int len;
		if (l_mod)
		  len = va_arg (ap, int);
		else
		  len = strlen (str);
		dyn_write_quoted (out, str, len);
	      }
	      break;
	    case 'v':
	      {
		dyn_val val = va_arg (ap, dyn_val);
		dyn_write_val (out, val, 0);
	      }
	      break;
	    case 'V':
	      {
		dyn_val val = va_arg (ap, dyn_val);
		dyn_write_val (out, val, 1);
	      }
	      break;
	    case 'r':
	      {
		ss_val val = va_arg (ap, ss_val);
		dyn_write_ss_val (out, val, 0);
	      }
	      break;
	    case 'R':
	      {
		ss_val val = va_arg (ap, ss_val);
		dyn_write_ss_val (out, val, 1);
	      }
	      break;
	    case 'm':
	      {
		char *msg = strerror (err);
		dyn_write_string (out, msg, strlen (msg));
	      }
	      break;
	    case 'd':
	      {
		char buf[40];
		sprintf (buf, "%d", va_arg (ap, int));
		dyn_write_string (out, buf, strlen (buf));
	      }
	      break;
	    case 'x':
	      {
		char buf[40];
		sprintf (buf, "%x", va_arg (ap, int));
		dyn_write_string (out, buf, strlen (buf));
	      }
	      break;
	    case 'f':
	      {
		char buf[80];
		sprintf (buf, "%g", va_arg (ap, double));
		dyn_write_string (out, buf, strlen(buf));
	      }
	      break;
	    case 'c':
	      {
		char buf[80];
		sprintf (buf, "%c", va_arg (ap, int));
		dyn_write_string (out, buf, strlen(buf));
	      }
	      break;
	    case 'I':
	      {
		dyn_input in = va_arg (ap, dyn_input);
		dyn_write_string (out,
				  dyn_input_mark (in), 
				  dyn_input_off (in));
	      }
	      break;
	    case 'B':
	      {
		const char *str = va_arg (ap, const char *);
		int len = va_arg (ap, int);
		dyn_write_string (out, str, len);
	      }
	      break;
	    case 'L':
	      {
		const char *sub_fmt = va_arg (ap, const char *);
		va_list sub_ap = va_arg (ap, va_list);
		dyn_writev (out, sub_fmt, sub_ap);
	      }
	      break;
	    case '%':
	      {
		dyn_write_string (out, "%", 1);
	      }
	      break;
	    default:
	      {
		dyn_write_string (out, "%", 1);
		dyn_write_string (out, fmt, 1);
	      }
	      break;
	    }
	}
      else
	dyn_write_string (out, fmt, 1);
      fmt++;
    }
}

static void
dyn_report ()
{
  dyn_unwind (NULL, 0);
  fprintf (stderr, "%d living objects\n", n_objects);
}

__attribute__ ((constructor))
void dyn_ensure_init ()
{
  static int initialized = 0;

  if (!initialized)
    {
      initialized = 1;

      DYN_ENSURE_TYPE (dyn_string);
      DYN_ENSURE_TYPE (dyn_func);
      DYN_ENSURE_TYPE (dyn_input);
      DYN_ENSURE_TYPE (dyn_output);

      dyn_stdout = dyn_create_output_fd (1);

      // atexit (dyn_report);
      (void) dyn_report;
    }
}
