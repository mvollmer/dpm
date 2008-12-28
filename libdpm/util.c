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
dpm_uncaught_error (dyn_val val)
{
  fprintf (stderr, "%s\n", dyn_to_string (val));
  exit (1);
}

static dyn_condition error_condition = {
  .name = "error",
  .uncaught = dpm_uncaught_error
};

const char *
dpm_catch_error (void (*func) (void *data), void *data)
{
  return dyn_to_string (dyn_catch (&error_condition, func, data));
}

void
dpm_error (dyn_val context, const char *fmt, ...)
{
  char *message;
  va_list ap;
  va_start (ap, fmt);
  message = dpm_vsprintf (fmt, ap);
  va_end (ap);

  if (context)
    {
      char *outer = NULL;

      if (dyn_is_object (context, &dpm_stream_type))
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

  dyn_val cond = dyn_from_string (message);
  free (message);

  dyn_signal (&error_condition, cond);
}
