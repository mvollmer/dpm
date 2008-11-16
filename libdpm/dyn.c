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
  dpm_dyn_kind_extent,
  dpm_dyn_kind_func,
  dpm_dyn_kind_var,
  dpm_dyn_kind_catch
} dpm_dyn_kind;

typedef struct dpm_dyn_item {
  struct dpm_dyn_item *up;
  dpm_dyn_kind kind;
  union {
    struct {
      void (*func) (int for_throw, void *data);
      void *data;
    } func;
    struct {
      dpm_dyn_var *var;
      void *oldval;
    } var;
    struct {
      jmp_buf *target;
      char *message;
    } catch;
  } val;
} dpm_dyn_item;

/* XXX - use a thread local variable instead of a global.
 */

static dpm_dyn_item *windlist;

static void
dpm_dyn_add (dpm_dyn_item *item)
{
  item->up = windlist;
  windlist = item;
}

static void
dpm_dyn_unwind (dpm_dyn_item *goal, int for_throw)
{
  while (windlist != goal)
    {
      dpm_dyn_item *w = windlist;

      switch (w->kind)
	{
	case dpm_dyn_kind_extent:
	  break;
	case dpm_dyn_kind_func:
	  w->val.func.func (for_throw, w->val.func.data);
	  break;
	case dpm_dyn_kind_var:
	  w->val.var.var->opaque[0] = w->val.var.oldval;
	  break;
	case dpm_dyn_kind_catch:
	  free (w->val.catch.target);
	  free (w->val.catch.message);
	  break;
	}
      windlist = w->up;
      free (w);
    }
}

void
dpm_dyn_begin ()
{
  dpm_dyn_item *item = dpm_xmalloc (sizeof (dpm_dyn_item));
  item->kind = dpm_dyn_kind_extent;
  dpm_dyn_add (item);
}

void
dpm_dyn_end ()
{
  dpm_dyn_item *w = windlist;
  while (w->kind != dpm_dyn_kind_extent)
    w = w->up;
  dpm_dyn_unwind (w->up, 0);
}

void *
dpm_dyn_get (dpm_dyn_var *var)
{
  return var->opaque[0];
}

void
dpm_dyn_set (dpm_dyn_var *var, void *val)
{
  var->opaque[0] = val;
}

void
dpm_dyn_let (dpm_dyn_var *var, void *val)
{
  dpm_dyn_item *item = dpm_xmalloc (sizeof (dpm_dyn_item));
  item->kind = dpm_dyn_kind_var;
  item->val.var.var = var;
  item->val.var.oldval = var->opaque[0];
  dpm_dyn_add (item);
  var->opaque[0] = val;
}

char *
dpm_dyn_catch (void (*func) (void *data), void *data)
{
  dpm_dyn_item *item = dpm_xmalloc (sizeof (dpm_dyn_item));
  item->kind = dpm_dyn_kind_catch;
  item->val.catch.target = dpm_xmalloc (sizeof (jmp_buf));
  item->val.catch.message = NULL;
  dpm_dyn_add (item);
  if (setjmp (*(item->val.catch.target)))
    {
      char *message = item->val.catch.message;
      item->val.catch.message = NULL;
      dpm_dyn_unwind (item->up, 1);
      return message;
    }
  else
    {
      func (data);
      return NULL;
    }
}

void
dpm_dyn_throw (char *message)
{
  dpm_dyn_item *w = windlist;
  while (w && (w->kind != dpm_dyn_kind_catch || !w->val.catch.target))
    w = w->up;

  if (w)
    {
      w->val.catch.message = message;
      longjmp (*(w->val.catch.target), 1);
    }
  else
    {
      fprintf (stderr, "%s\n", message);
      exit (1);
    }
}

void
dpm_dyn_error (const char *fmt, ...)
{
  char *message;
  va_list ap;
  va_start (ap, fmt);
  message = dpm_vsprintf (fmt, ap);
  va_end (ap);
  dpm_dyn_throw (message);
}
