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

typedef enum {
  dyn_kind_extent,
  dyn_kind_func,
  dyn_kind_var,
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
      void *oldval;
    } var;
    struct {
      jmp_buf *target;
      dyn_condition *condition;
      void *value;
    } catch;
  } val;
} dyn_item;

/* XXX - use a thread local variable instead of a global.
 */

static dyn_item *windlist;

static void
dyn_add (dyn_item *item)
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
	  w->val.var.var->opaque[0] = w->val.var.oldval;
	  break;
	case dyn_kind_catch:
	  free (w->val.catch.target);
	  if (w->val.catch.value
	      && w->val.catch.condition->free)
	    w->val.catch.condition->free (w->val.catch.value);
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
  dyn_add (item);
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
dyn_wind (void (*func) (int for_throw, void *data), void *data)
{
  dyn_item *item = dpm_xmalloc (sizeof (dyn_item));
  item->kind = dyn_kind_func;
  item->val.func.func = func;
  item->val.func.data = data;
  dyn_add (item);
}

static void
unwind_free (int for_throw, void *data)
{
  free (data);
}

void
dyn_free (void *mem)
{
  dyn_wind (unwind_free, mem);
}

void *
dyn_get (dyn_var *var)
{
  return var->opaque[0];
}

void
dyn_set (dyn_var *var, void *val)
{
  var->opaque[0] = val;
}

void
dyn_let (dyn_var *var, void *val)
{
  dyn_item *item = dpm_xmalloc (sizeof (dyn_item));
  item->kind = dyn_kind_var;
  item->val.var.var = var;
  item->val.var.oldval = var->opaque[0];
  dyn_add (item);
  var->opaque[0] = val;
}

void *
dyn_catch (dyn_condition *condition,
	   void (*func) (void *data), void *data)
{
  dyn_item *item = dpm_xmalloc (sizeof (dyn_item));

  item->kind = dyn_kind_catch;
  item->val.catch.target = dpm_xmalloc (sizeof (jmp_buf));
  item->val.catch.condition = condition;
  item->val.catch.value = NULL;
  dyn_add (item);

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
dyn_throw (dyn_condition *condition, void *value)
{
  dyn_item *w = windlist;
  while (w && (w->kind != dyn_kind_catch
	       || w->val.catch.condition != condition
	       || w->val.catch.value != NULL))
    w = w->up;

  if (w)
    {
      w->val.catch.value = value;
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
