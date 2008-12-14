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

#include "dpm.h"

static void
dpm_oom ()
{
  fprintf (stderr, "Out of memory.\n");
  abort ();
}

void *
dpm_xmalloc (size_t size)
{
  void *mem = malloc (size);
  if (mem == NULL)
    dpm_oom ();
  return mem;
}

void *
dpm_xremalloc (void *old, size_t size)
{
  void *mem = realloc (old, size);
  if (mem == NULL)
    dpm_oom ();
  return mem;
}

void *
dpm_xstrdup (const char *str)
{
  char *dup;

  if (str == NULL)
    return NULL;

  dup = dpm_xmalloc (strlen (str) + 1);
  strcpy (dup, str);
  return dup;
}

void *
dpm_xstrndup (const char *str, int n)
{
  char *dup;

  if (str == NULL)
    return NULL;

  dup = strndup (str, n);
  if (dup == NULL)
    dpm_oom ();
  return dup;
}

char *
dpm_sprintf (const char *fmt, ...)
{
  char *result;
  va_list ap;
  va_start (ap, fmt);
  result = dpm_vsprintf (fmt, ap);
  va_end (ap);
  return result;
}

char *
dpm_vsprintf (const char *fmt, va_list ap)
{
  char *result;
  if (vasprintf (&result, fmt, ap) < 0)
    dpm_oom ();
  return result;
}

void
dpm_uncaught_error (void *value)
{
  fprintf (stderr, "%s\n", value);
  exit (1);
}

static dyn_condition error_condition = {
  .name = "error",
  .free = free,
  .uncaught = dpm_uncaught_error
};

char *
dpm_catch_error (void (*func) (void *data), void *data)
{
  return (char *)dyn_catch (&error_condition, func, data);
}

typedef struct dpm_error_context {
  struct dpm_error_context *next;
  char *(*func) (const char *message, int level, void *data);
  void *data;
} dpm_error_context;

static dyn_var dpm_cur_error_context;

void
dpm_let_error_context (char *(*func) (const char *message, int level, 
				      void *data),
		       void *data)
{
  dpm_error_context *ctxt = dpm_xmalloc (sizeof (dpm_error_context));
  ctxt->func = func;
  ctxt->data = data;
  
  ctxt->next = dyn_get (&dpm_cur_error_context);
  dyn_let (&dpm_cur_error_context, ctxt);
  dyn_free (ctxt);
}

void
dpm_error (const char *fmt, ...)
{
  dpm_error_context *ctxt;
  int level;
  char *message;
  va_list ap;
  va_start (ap, fmt);
  message = dpm_vsprintf (fmt, ap);
  va_end (ap);

  level = 0;
  for (ctxt = dyn_get (&dpm_cur_error_context); ctxt; ctxt = ctxt->next)
    {
      char *outer = ctxt->func (message, level, ctxt->data);
      free (message);
      message = outer;
      level += 1;
    }

  dyn_throw (&error_condition, message);
}
