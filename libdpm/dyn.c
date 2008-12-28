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

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <setjmp.h>

#include "util.h"
#include "dyn.h"

/* Dynamic values */

#define DYN_HEAD(v)      (((uint32_t *)v)[-1])
#define DYN_REFCOUNT(v)  (DYN_HEAD(v)&0xFFFFFF)
#define DYN_TAG(v)       (DYN_HEAD(v)>>24)

#define DYN_TAG_STRING 0
#define DYN_TAG_PAIR   1
#define DYN_TAG_FUNC   2
#define DYN_TAG_USER   3

static dyn_type dyn_type_string = {
  .tag = DYN_TAG_STRING,
  .name = "string",
  .unref = NULL
};

static void dyn_pair_unref (dyn_type *, void *);

static dyn_type dyn_type_pair = {
  .tag = DYN_TAG_PAIR,
  .name = "pair",
  .unref = dyn_pair_unref
};

static void dyn_func_unref (dyn_type *, void *);

static dyn_type dyn_type_func = {
  .tag = DYN_TAG_FUNC,
  .name = "function",
  .unref = dyn_func_unref
};

static dyn_type *dyn_types[256] = {
  [DYN_TAG_STRING] = &dyn_type_string,
  [DYN_TAG_PAIR] = &dyn_type_pair,
  [DYN_TAG_FUNC] = &dyn_type_func
};

static int dyn_next_tag = DYN_TAG_USER;

void
dyn_type_register (dyn_type *type)
{
  int t = dyn_next_tag++;
  dyn_types[t] = type;
  type->tag = t;
}

static int n_objects;

dyn_val
dyn_alloc (dyn_type *type, size_t size)
{
  uint32_t *mem = dpm_xmalloc (size + sizeof (uint32_t));
  dyn_val val = (dyn_val)(mem + 1);
  mem[0] = (type->tag << 24) | 1;
  // fprintf (stderr, "%p * %s\n", val, type->name);
  dyn_unref_on_unwind (val);
  n_objects += 1;
  return val;
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

int
dyn_is_string (dyn_val val)
{
  return val && DYN_TAG(val) == DYN_TAG_STRING;
}

const char *
dyn_to_string (dyn_val val)
{
  return (const char *)val;
}

dyn_val
dyn_from_string (const char *str)
{
  return dyn_from_stringn (str, strlen (str));
}

dyn_val
dyn_from_stringn (const char *str, int len)
{
  dyn_val val = dyn_alloc (&dyn_type_string, len + 1);
  strncpy ((char *)val, str, len);
  ((char *)val)[len] = 0;
  // fprintf (stderr, "%p = %.*s\n", val, len, str);
  return val;
}

static void
dyn_pair_unref (dyn_type *type, void *object)
{
  dyn_unref (((dyn_val *)object)[0]);
  dyn_unref (((dyn_val *)object)[1]);
}

int
dyn_is_pair (dyn_val val)
{
  return val && DYN_TAG(val) == DYN_TAG_PAIR;
}

dyn_val
dyn_cons (dyn_val first, dyn_val rest)
{
  dyn_val val = dyn_alloc (&dyn_type_pair, sizeof (dyn_val) * 2);
  ((dyn_val *)val)[0] = dyn_ref (first);
  ((dyn_val *)val)[1] = dyn_ref (rest);
  return val;
}

dyn_val
dyn_first (dyn_val val)
{
  return ((dyn_val *)val)[0];
}

dyn_val
dyn_rest (dyn_val val)
{
  return ((dyn_val *)val)[1];
}

int
dyn_is_list (dyn_val val)
{
  return val == NULL || DYN_TAG(val) == DYN_TAG_PAIR;
}

typedef struct {
  void (*func) ();
  void *data;
  void (*free) (void *data);
} dyn_func;

static void
dyn_func_unref (dyn_type *type, void *object)
{
  dyn_func *func = object;
  if (func->free)
    func->free (func->data);
}

dyn_val
dyn_lambda (void (*func) (), void *data, void (*free) (void *data))
{
  dyn_func *f = dyn_alloc (&dyn_type_func, sizeof (dyn_func));
  f->func = func;
  f->data = data;
  f->free = free;
  return f;
}

void
(*dyn_func_func (dyn_val val))()
{
  return ((dyn_func *)val)->func;
}

void *
dyn_func_data (dyn_val val)
{
  return ((dyn_func *)val)->data;
}

int
dyn_is_func (dyn_val val)
{
  return dyn_is_object (val, &dyn_type_func);
}

int
dyn_is_object (dyn_val val, dyn_type *type)
{
  return val && dyn_types[DYN_TAG(val)] == type;
}

const char *
dyn_type_name (dyn_val val)
{
  return dyn_types[DYN_TAG(val)]->name;
}

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
  dyn_item *item = dpm_xmalloc (sizeof (dyn_item));
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
  dyn_item *item = dpm_xmalloc (sizeof (dyn_item));
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
  dyn_item *item = dpm_xmalloc (sizeof (dyn_item));
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
  dyn_item *item = dpm_xmalloc (sizeof (dyn_item));
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

dyn_val
dyn_catch (dyn_condition *condition,
	   void (*func) (void *data), void *data)
{
  dyn_item *item = dpm_xmalloc (sizeof (dyn_item));

  item->kind = dyn_kind_catch;
  item->val.catch.target = dpm_xmalloc (sizeof (jmp_buf));
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
    dyn_func_func (handler) (value, dyn_func_data (handler));
  else if (condition->unhandled)
    condition->unhandled (value);

  dyn_throw (condition, value);
}

void
dyn_let_handler (dyn_condition *condition, dyn_val handler)
{
  dyn_let (&(condition->handler), handler);
}
