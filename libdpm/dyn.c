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
#include <bzlib.h>

#include "dyn.h"

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

void *
dyn_realloc (void *old, size_t size)
{
  void *mem = realloc (old, size);
  if (mem == NULL)
    dyn_oom ();
  return mem;
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
  uint32_t *mem = dyn_malloc (size + sizeof (uint32_t));
  dyn_val val = (dyn_val)(mem + 1);
  mem[0] = (type->tag << 24) | 1;
  // fprintf (stderr, "%p * %s\n", val, type->name);
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
    {
      DYN_HEAD(val) += 1;
      // fprintf (stderr, "%p + = %d ", val, DYN_REFCOUNT (val));
      // dpm_write (stderr, "%V\n", val);
    }

  return val;
}

void
dyn_unref (dyn_val val)
{
  if (val)
    {
      DYN_HEAD(val) -= 1;
      // fprintf (stderr, "%p - = %d ", val, DYN_REFCOUNT (val));
      // dpm_write (stderr, "%V\n", val);
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

static void
dyn_report ()
{
  fprintf (stderr, "%d living objects\n", n_objects);
}

__attribute__ ((constructor))
static void
dyn_report_init ()
{
  atexit (dyn_report);
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
  // fprintf (stderr, "%p = %.*s\n", val, len, str);
  return string;
}

struct dyn_pair_struct {
  dyn_val first;
  dyn_val rest;
};

static void
dyn_pair_unref (dyn_type *type, void *object)
{
  dyn_pair pair = object;
  dyn_unref (pair->first);
  dyn_unref (pair->rest);
}

static int
dyn_pair_equal (void *a, void *b)
{
  dyn_pair pair_a = a;
  dyn_pair pair_b = b;
  return (dyn_equal (pair_a->first, pair_b->first)
	  && dyn_equal (pair_a->rest, pair_b->rest));
}

DYN_DEFINE_TYPE (dyn_pair, "pair");

int
dyn_is_pair (dyn_val val)
{
  return dyn_is (val, dyn_pair_type);
}

dyn_val
dyn_cons (dyn_val first, dyn_val rest)
{
  dyn_pair pair = dyn_new (dyn_pair);
  pair->first = dyn_ref (first);
  pair->rest = dyn_ref (rest);
  return pair;
}

dyn_val
dyn_first (dyn_val val)
{
  dyn_pair pair = val;
  return pair->first;
}

dyn_val
dyn_rest (dyn_val val)
{
  dyn_pair pair = val;
  return pair->rest;
}

int
dyn_is_list (dyn_val val)
{
  return val == NULL || dyn_is_pair (val);
}

int
dyn_length (dyn_val val)
{
  int n = 0;
  while (dyn_is_pair (val))
    {
      n++;
      val = dyn_rest (val);
    }
  return n;
}

struct dyn_dict_struct {
  dyn_val key;
  dyn_val value;
  dyn_val rest;
};

static void
dyn_dict_unref (dyn_type *type, void *object)
{
  dyn_dict dict = object;
  dyn_unref (dict->key);
  dyn_unref (dict->value);
  dyn_unref (dict->rest);
}

static int
dyn_dict_is_subset (dyn_val a, dyn_val b)
{
  while (a)
    {
      dyn_dict dict_a = a;
      if (!dyn_equal (dict_a->value, dyn_lookup (b, dict_a->key)))
	return 0;
      a = dict_a->rest;
    }
  return 1;
}

static int
dyn_dict_equal (void *a, void *b)
{
  /* cough */
  return (dyn_dict_is_subset (a, b)
	  && dyn_dict_is_subset (b, a));
}

DYN_DEFINE_TYPE (dyn_dict, "dict");

int
dyn_is_dict (dyn_val val)
{
  return dyn_is (val, dyn_dict_type);
}

dyn_val
dyn_lookup (dyn_val dict, dyn_val key)
{
  dyn_dict d = dict;

  if (key == NULL)
    return NULL;
  
  while (d)
    {
      if (dyn_equal (d->key, key))
	return d->value;
      d = d->rest;
    }
  return NULL;
}

dyn_val
dyn_assoc (dyn_val dict, dyn_val key, dyn_val value)
{
  dyn_dict d = dyn_new (dyn_dict);
  d->key = dyn_ref (key);
  d->value = dyn_ref (value);
  d->rest = dyn_ref (dict);
  return d;
}

struct dyn_func_struct {
  void (*code) ();
  void *env;
  void (*free_env) (void *env);
};

static void
dyn_func_unref (dyn_type *type, void *object)
{
  dyn_func func = object;
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
dyn_lambda (void (*code) (), void *env, void (*free_env) (void *env))
{
  dyn_func func = dyn_new (dyn_func);
  func->code = code;
  func->env = env;
  func->free_env = free_env;
  return (dyn_val)func;
}

void
(*dyn_func_code (dyn_val val))()
{
  dyn_func func = val;
  return func->code;
}

void *
dyn_func_env (dyn_val val)
{
  dyn_func func = val;
  return func->env;
}

/* List builder */

void
dyn_list_start (dyn_list_builder builder)
{
  builder[0].opaque[0] = NULL;
  builder[0].opaque[1] = &(builder[0].opaque[0]);
}

void
dyn_list_append (dyn_list_builder builder, dyn_val val)
{
  *((dyn_pair *)builder[0].opaque[1]) = dyn_ref (dyn_cons (val, NULL));
  builder[0].opaque[1] = &((*(dyn_pair *)builder[0].opaque[1])->rest);
}

dyn_val
dyn_list_finish (dyn_list_builder builder)
{
  return builder[0].opaque[0];
}

dyn_val
dyn_list (dyn_val first, ...)
{
  dyn_list_builder builder;

  va_list ap;
  va_start (ap, first);

  dyn_list_start (builder);
  while (first != DYN_EOL)
    {
      dyn_list_append (builder, first);
      first = va_arg (ap, dyn_val);
    }
  return dyn_list_finish (builder);
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
  dyn_kind_catch
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
      jmp_buf *target;
      dyn_condition *condition;
      dyn_val value;
    } catch;
  } val;
} dyn_item;

/* XXX - use a thread local variable instead of a global.
 */

static dyn_item *windlist;

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
	case dyn_kind_catch:
	  free (w->val.catch.target);
	  dyn_unref (w->val.catch.value);
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

/* Conditions */

dyn_val
dyn_catch (dyn_condition *condition,
	   void (*func) (void *data), void *data)
{
  dyn_item *item = dyn_malloc (sizeof (dyn_item));

  item->kind = dyn_kind_catch;
  item->val.catch.target = dyn_malloc (sizeof (jmp_buf));
  item->val.catch.condition = condition;
  item->val.catch.value = NULL;
  dyn_add_unwind_item (item);

  /* If we caught something, we leave our entry in the windlist so
     that the value is freed later.  When we didn't catch anything, we
     have to remove our entry so that dyn_throw doesn't think it is
     still active.
  */

  if (setjmp (*(item->val.catch.target)) == 0)
    {
      func (data);
      dyn_unwind (item->up, 0);
      return NULL;
    }
  else
    return item->val.catch.value;
}

void
dyn_throw (dyn_condition *condition, dyn_val value)
{
  dyn_item *w = windlist;
  while (w && (w->kind != dyn_kind_catch
	       || w->val.catch.condition != condition
	       || w->val.catch.value != NULL))
    w = w->up;

  if (w)
    {
      w->val.catch.value = dyn_ref (value);
      dyn_unwind (w, 1);
      longjmp (*(w->val.catch.target), 1);
    }
  else if (condition->uncaught)
    {
      condition->uncaught (value);
      exit (1);
    }
  else
    {
      fprintf (stderr, "Uncaught condition '%s'\n", condition->name);
      exit (1);
    }
}

void
dyn_signal (dyn_condition *condition, dyn_val value)
{
  dyn_val handler = dyn_get (&(condition->handler));
  if (dyn_is_func (handler))
    dyn_func_code (handler) (value, dyn_func_env (handler));
  else if (condition->unhandled)
    condition->unhandled (value);

  dyn_throw (condition, value);
}

void
dyn_let_handler (dyn_condition *condition, dyn_val handler)
{
  dyn_let (&(condition->handler), handler);
}

static void
dyn_uncaught_error (dyn_val val)
{
  fprintf (stderr, "%s\n", dyn_to_string (val));
  exit (1);
}

dyn_condition dyn_condition_error = {
  .name = "error",
  .uncaught = dyn_uncaught_error
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
dyn_catch_error (void (*func) (void *), void *data)
{
  return dyn_catch (&dyn_condition_error, func, data);
}

static int
dyn_schema_mismatch (dyn_val schema, dyn_val val, int soft)
{
  if (soft)
    return 0;
  else
    dyn_error ("value does not match schema, expecting %V: %V", schema, val);
}

static int
dyn_apply_schema_with_env (dyn_val val, dyn_val schema,
			   dyn_val *result, int soft,
			   dyn_val env)
{
#if 0
  dyn_write (dyn_stdout, "applying %V to %V\n", schema, val);
  dyn_output_flush (dyn_stdout);
#endif

  if (dyn_is_string (schema))
    {
      if (dyn_eq (schema, "any")
	  || strcmp (dyn_type_name (val), dyn_to_string (schema)) == 0)
	{
	  *result = val;
	  return 1;
	}
      else
	return dyn_schema_mismatch (schema, val, soft);
    }
  else if (dyn_is_pair (schema))
    {
      dyn_val op = dyn_first (schema);

      if (dyn_eq (op, "value"))
	{
	  if (dyn_length (schema) != 2)
	    dyn_error ("invalid schema: %V", schema);

	  dyn_val schema_value = dyn_first (dyn_rest (schema));

	  if (dyn_equal (val, schema_value))
	    {
	      *result = val;
	      return 1;
	    }
	  else 
	    return dyn_schema_mismatch (schema, val, soft);
	}
      else if (dyn_eq (op, "list"))
	{
	  dyn_val cur_list = val;
	  dyn_val schemas = dyn_rest (schema);
	  dyn_val prev_schemas = NULL;
	  dyn_list_builder result_builder;

	  dyn_list_start (result_builder);
	  while (dyn_is_pair (schemas))
	    {
	      if (cur_list && !dyn_is_pair (cur_list))
		dyn_schema_mismatch (schema, val, soft);

	      dyn_val cur_schema = dyn_first (schemas);
	      dyn_val cur_value = cur_list? dyn_first (cur_list) : NULL;
	      
	      if (dyn_eq (cur_schema, "..."))
		{
		  if (dyn_rest (schemas))
		    dyn_error ("ellipsis must be last in list schema: %V", 
			       schema);

		  if (prev_schemas == NULL)
		    dyn_error ("ellipsis must not be first in list schema: %V",
			       schema);
		    
		  /* It's OK to end the list at any time.
		   */
		  if (!dyn_is_pair (cur_list))
		    break;

		  /* Go back to previous schema
		   */
		  schemas = prev_schemas;
		}
	      else
		{
		  dyn_val result_value;
		  if (!dyn_apply_schema_with_env (cur_value, cur_schema,
						  &result_value, soft,
						  env))
		    return 0;
		  else
		    dyn_list_append (result_builder, result_value);
		  
		  prev_schemas = schemas;
		  schemas = dyn_rest (schemas);
		  if (cur_list)
		    cur_list = dyn_rest (cur_list);
		}
	    }

	  /* We made it through all the schemas, the list is valid
	     when there isn't anything left.
	   */
	  if (cur_list == NULL)
	    {
	      *result = dyn_list_finish (result_builder);
	      return 1;
	    }
	  else
	    return dyn_schema_mismatch (schema, val, soft);
	}
      else if (dyn_eq (op, "dict"))
	{
	  return dyn_schema_mismatch (schema, val, soft);
	}
      else if (dyn_eq (op, "defaulted"))
	{
	  if (dyn_length (schema) != 3)
	    dyn_error ("invalid schema: %V", schema);
	  
	  if (val == NULL)
	    {
	      *result = dyn_first (dyn_rest (dyn_rest (schema)));
	      return 1;
	    }
	  else 
	    return dyn_apply_schema_with_env (val,
					      dyn_first (dyn_rest (schema)),
					      result, soft, env);
	}
      else if (dyn_eq (op, "not"))
	{
	  if (dyn_length (schema) != 2)
	    dyn_error ("invalid schema: %V", schema);

	  dyn_val schema = dyn_first (dyn_rest (schema));
	  dyn_val unused_result;
	  if (dyn_apply_schema_with_env (val, schema, &unused_result, 1, env))
	    dyn_schema_mismatch (schema, val, soft);
	  else
	    {
	      *result = val;
	      return 1;
	    }
	}
      else if (dyn_eq (op, "or"))
	{
	  dyn_val schemas = dyn_rest (schema);
	  while (dyn_is_pair (schemas))
	    {
	      dyn_val cur_schema = dyn_first (schemas);
	      
	      if (dyn_apply_schema_with_env (val, cur_schema,
					     result, 1, env))
		return 1;

	      schemas = dyn_rest (schemas);
	    }

	  return dyn_schema_mismatch (schema, val, soft);
	}
      else if (dyn_eq (op, "if"))
	{
	  dyn_val schema_pairs = dyn_rest (schema);
	  while (dyn_is_pair (schema_pairs))
	    {
	      dyn_val cur_schema = dyn_first (schema_pairs);
	      
	      if (dyn_apply_schema_with_env (val, cur_schema,
					     result, 1, env))
		{
		  if (dyn_is_pair (dyn_rest (schema_pairs)))
		    *result = dyn_first (dyn_rest (schema_pairs));
		  return 1;
		}

	      schema_pairs = dyn_rest (schema_pairs);
	      if (dyn_is_pair (schema_pairs))
		schema_pairs = dyn_rest (schema_pairs);
	    }

	  return dyn_schema_mismatch (schema, val, soft);
	}
#if 0
      else if (dyn_eq (op, "let"))
	{
	}
      else if (dyn_eq (op, "schema"))
	{
	}
#endif
      else
	dyn_error ("unsupported schema: %V", schema);
    }
  else
    dyn_error ("unsupported schema: %V", schema);
}

dyn_val
dyn_apply_schema (dyn_val val, dyn_val schema)
{
  dyn_val result;
  dyn_apply_schema_with_env (val, schema, &result, 0, NULL);
  return result;
}

/* Input streams */

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
dyn_input_set_static_buffer (dyn_input in, char *buf, int len)
{
  in->bufstatic = 1;
  in->buf = (char *)buf;
  in->bufsize = len;
  in->bufend = in->buf + in->bufsize;

  in->mark = in->buf;
  in->pos = in->mark;
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

const char *
dyn_stream_filename (dyn_input in)
{
  return in->filename;
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
  else if (has_suffix (filename, ".bz2"))
    in = dyn_open_bz2 (in);

  return in;
}

dyn_input
dyn_open_string (const char *str, int len)
{
  dyn_input in = dyn_input_new (NULL);
  dyn_input_set_static_buffer (in, (char *)str, (len < 0)? strlen (str) : len);
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
};

static int
dyn_bz2_read (void *handle, char *buf, int n)
{
  struct dyn_bzlib_handle *z = handle;
  int ret;

  z->stream.next_out = buf;
  z->stream.avail_out = n;

  /* Loop until we have produced some output */
  while (z->stream.avail_out == n)
    {
      /* Get more input if needed. */
      if (z->stream.avail_in == 0)
	{
	  dyn_input_set_mark (z->source);
	  dyn_input_advance (z->source, dyn_input_grow (z->source, 1));
	  z->stream.next_in = dyn_input_mark (z->source);
	  z->stream.avail_in =
	    dyn_input_pos (z->source) - dyn_input_mark (z->source);
	}

      /* Make some progress */
      ret = BZ2_bzDecompress (&(z->stream));
      if (ret != BZ_OK && ret != BZ_STREAM_END)
	dyn_error (bzerrfmt (ret), ret);

      if (ret == BZ_STREAM_END)
	break;
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

  in->handle = z;
  in->read = dyn_bz2_read;
  in->close = dyn_bz2_close;

  ret = BZ2_bzDecompressInit (&(z->stream), 0, 0);
  if (ret != BZ_OK)
    dyn_error (bzerrfmt (ret), ret);

  return in;
}

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

  if (!in->bufstatic && in->pos + min > in->bufend)
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

char *
dyn_input_mark (dyn_input in)
{
  return in->mark;
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
      char *p;
      for (p = in->pos; p < pos; p++)
	if (*p == '\n')
	  in->lineno++;
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

dyn_output dyn_stdout;

__attribute__ ((constructor))
static void
dyn_init_std ()
{
  DYN_ENSURE_TYPE (dyn_output);
  dyn_stdout = dyn_create_output_fd (1);
}

dyn_val
dyn_formatv (const char *fmt, va_list ap)
{
  dyn_output out = dyn_create_output_string ();
  dyn_writev (out, fmt, ap);
  return dyn_output_commit (out);
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
      dyn_write (out, "()");
    }
  else if (dyn_is_string (val))
    {
      const char *str = dyn_to_string (val);
      if (quoted
	  && (strchr (str, '(')
	      || strchr (str, ')')
	      || strchr (str, '{')
	      || strchr (str, '}')
	      || strchr (str, '"')
	      || strchr (str, ' ')
	      || strchr (str, '\t')
	      || strchr (str, '\n')))
	dyn_write (out, "%S", str);
      else
	dyn_write (out, "%s", str);
    }
  else if (dyn_is_list (val))
    {
      dyn_write (out, "(");
      while (dyn_is_pair (val))
	{
	  dyn_write_val (out, dyn_first (val), quoted);
	  val = dyn_rest (val);
	  if (val)
	    dyn_write (out, " ");
	}
      if (val)
	{
	  dyn_write (out, ". ");
	  dyn_write_val (out, val, quoted);
	}
      dyn_write (out, ")");
    }
  else if (dyn_is_dict (val))
    {
      dyn_write (out, "{ ... }");
    }
  else
    dyn_write (out, "<%s>", dyn_type_name (val));
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
	  switch (*fmt)
	    {
	    case '\0':
	      return;
	    case 's':
	      {
		char *str = va_arg (ap, char *);
		dyn_write_string (out, str, strlen (str));
	      }
	      break;
	    case 'S':
	      {
		char *str = va_arg (ap, char *);
		dyn_write_quoted (out, str, strlen (str));
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
	    case 'm':
	      {
		char *msg = strerror (err);
		dyn_write_string (out, msg, strlen (msg));
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

typedef struct {
  dyn_input in;
  int cur_kind, cur_len;
} dyn_read_state;

static void
dyn_read_next (dyn_read_state *state)
{
  dyn_input in = state->in;

 again:
  dyn_input_skip (in, " \t\r\v\n");
  if (dyn_input_looking_at (in, "#"))
    {
      dyn_input_find (in, "\n");
      goto again;
    }

  dyn_input_set_mark (in);
  if (dyn_input_looking_at (in, "\""))
    {
      /* Quoted string.
       */
      dyn_input_advance (in, 1);
      dyn_input_set_mark (in);
      while (dyn_input_find (in, "\""))
	{
	  char *mark = dyn_input_mark (in);
	  const char *pos = dyn_input_pos (in);
	  char *p1, *p2;

	  if (pos > mark && pos[-1] == '\\')
	    {
	      dyn_input_advance (in, 1);
	      continue;
	    }

	  p1 = mark;
	  p2 = mark;
	  while (p1 < pos)
	    {
	      if (*p1 == '\\' && p1 < pos - 1)
		{
		  p1++;
		  switch (*p1)
		    {
		    case 't':
		      *p2++ = '\t';
		      break;
		    case 'n':
		      *p2++ = '\n';
		      break;
		    case 'r':
		      *p2++ = '\r';
		      break;
		    case 'v':
		      *p2++ = '\v';
		      break;
		    case '"':
		      *p2++ = '\"';
		      break;
		    default:
		      dyn_error ("Unsupported escape '%c'", *p1);
		      break;
		    }
		  p1++;
		}
	      else
		*p2++ = *p1++;
	    }

	  dyn_input_advance (in, 1);
	  state->cur_kind = '"';
	  state->cur_len = p2 - mark;
	  return;
	}

      dyn_error ("Unexpected end of input within quotes");
    }
  else if (!dyn_input_looking_at (in, "(")
	   && !dyn_input_looking_at (in, ")")
	   && !dyn_input_looking_at (in, "{")
	   && !dyn_input_looking_at (in, "}")
	   && !dyn_input_looking_at (in, "\n"))
    {
      /* Symbol.
       */
      dyn_input_find (in, " \t\r\v\n{}()\"");
      if (dyn_input_pos (in) > dyn_input_mark (in))
	{
	  state->cur_kind = 'a';
	  state->cur_len = dyn_input_pos (in) - dyn_input_mark (in);
	}
      else
	{
	  state->cur_kind = '\0';
	  state->cur_len = 0;
	}
      return;
    }
  else if (dyn_input_grow (in, 1))
    {
      /* Some single character delimiter.
       */
      dyn_input_advance (in, 1);
      state->cur_kind = *dyn_input_mark (in);
      state->cur_len = 1;
      return;
    }
  else
    {
      /* End of input
       */
      state->cur_kind = '\0';
      state->cur_len = 0;
      return;
    }
}

static dyn_val dyn_end_of_input_token;

struct dyn_end_of_input_struct {
};

static void
dyn_end_of_input_unref (dyn_type *type, void *object)
{
}

static int
dyn_end_of_input_equal (void *a, void *b)
{
  return 0;
}

DYN_DECLARE_TYPE (dyn_end_of_input);
DYN_DEFINE_TYPE (dyn_end_of_input, "end-of-input");

__attribute__ ((constructor))
static void dyn_init_reader ()
{
  DYN_ENSURE_TYPE (dyn_end_of_input);
  dyn_end_of_input_token = dyn_new (dyn_end_of_input);
}

int
dyn_is_eof (dyn_val val)
{
  return val == dyn_end_of_input_token;
}

static dyn_val dyn_read_list (dyn_read_state *state);

static dyn_val
dyn_read_element (dyn_read_state *state)
{
  dyn_val elt = NULL;

  if (state->cur_kind == '(')
    {
      dyn_read_next (state);
      elt = dyn_read_list (state);
      if (state->cur_kind != ')')
	dyn_error ("Unexpected end of input in list");
    }
  else if (state->cur_kind == ')')
    {
      dyn_error ("Unexpected list delimiter");
    }
  else if (state->cur_kind == 0)
    {
      elt = dyn_end_of_input_token;
    }
  else
    {
      elt = dyn_from_stringn (dyn_input_mark (state->in),
			      state->cur_len);
    }

  return elt;
}

static dyn_val
dyn_read_list (dyn_read_state *state)
{
  dyn_list_builder builder;

  dyn_list_start (builder);
  while (state->cur_kind != 0
	 && state->cur_kind != ')')
    {
      dyn_list_append (builder, dyn_read_element (state));
      dyn_read_next (state);
    }

  return dyn_list_finish (builder);
}

dyn_val
dyn_read (dyn_input in)
{
  dyn_val val;
  dyn_read_state state;
  state.in = in;
  
  dyn_begin ();
  dyn_read_next (&state);
  val = dyn_read_element (&state);
  return dyn_end_with (val);
}
