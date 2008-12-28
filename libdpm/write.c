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
#include <ctype.h>

#include "util.h"
#include "store.h"
#include "conf.h"
#include "write.h"

void
dpm_write (FILE *f, const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  dpm_writev (f, fmt, ap);
  va_end (ap);
}

void
dpm_print (const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  dpm_writev (stdout, fmt, ap);
  va_end (ap);
}

static void
dpm_write_quoted (FILE *f, const char *str, int len)
{
  fputc ('"', f);
  while (len > 0)
    {
      switch (*str)
	{
	case '\0':
	  fputs ("\\0", f);
	  break;
	case '\n':
	  fputs ("\\n", f);
	  break;
	case '\t':
	  fputs ("\\t", f);
	  break;
	case '"':
	  fputs ("\\\"", f);
	  break;
	default:
	  if (isprint (*str))
	    fputc (*str, f);
	  else
	    fprintf (f, "\\x%02x", *str); 
	  break;
	}
      str++;
      len--;
    }
  fputc ('"', f);
}

typedef void (*dpm_tag_writer) (FILE *, ss_val, int quoted);

static dpm_tag_writer dpm_tag_writers[128];

void
dpm_register_tag_writer (int tag, dpm_tag_writer writer)
{
  if (tag >= 0 && tag < 128)
    dpm_tag_writers[tag] = writer;
}

static void
dpm_write_store_val (FILE *f, ss_val val, int quoted)
{
  dpm_tag_writer writer = dpm_tag_writers[ss_tag (val)];
  if (writer)
    writer (f, val, quoted);
  else
    {
      if (ss_is_blob (val))
	{
	  if (quoted)
	    dpm_write_quoted (f, ss_blob_start (val), ss_len (val));
	  else
	    fwrite (ss_blob_start (val), 1, ss_len (val), f);
	}
      else
	fprintf (f, "{tag %d, len %d}", ss_tag (val), ss_len (val));
    }
}

static void
dpm_write_val (FILE *f, dyn_val val, int quoted)
{
  if (val == NULL)
    {
      fprintf (f, "{}");
    }
  else if (dyn_is_string (val))
    {
      const char *str = dyn_to_string (val);
      if (quoted
	  && (strchr (str, '{')
	      || strchr (str, '}')
	      || strchr (str, '"')
	      || strchr (str, ' ')
	      || strchr (str, '\t')
	      || strchr (str, '\n')))
	dpm_write_quoted (f, str, strlen (str));
      else
	fprintf (f, "%s", str);
    }
  else if (dyn_is_list (val))
    {
      fprintf (f, "{ ");
      while (dyn_is_pair (val))
	{
	  dpm_write_val (f, dyn_first (val), quoted);
	  fprintf (f, " ");
	  val = dyn_rest (val);
	}
      if (val)
	{
	  fprintf (stderr, ". ");
	  dpm_write_val (f, val, quoted);
	}
      fprintf (f, "}");
    }
  else
    fprintf (f, "<%s>", dyn_type_name (val));
}

void
dpm_writev (FILE *f, const char *fmt, va_list ap)
{
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
	      fputs (va_arg (ap, char *), f);
	      break;
	    case 'S':
	      {
		char *str = va_arg (ap, char *);
		dpm_write_quoted (f, str, strlen (str));
	      }
	      break;
	    case 'd':
	      fprintf (f, "%d", va_arg (ap, int));
	      break;
	    case 'r':
	      {
		ss_val v = va_arg (ap, ss_val);
		dpm_write_store_val (f, v, 0);
	      }
	      break;
	    case 'R':
	      {
		ss_val v = va_arg (ap, ss_val);
		dpm_write_store_val (f, v, 1);
	      }
	      break;
	    case 'v':
	      {
		dyn_val val = va_arg (ap, dyn_val);
		dpm_write_val (f, val, 0);
	      }
	      break;
	    case 'V':
	      {
		dyn_val val = va_arg (ap, dyn_val);
		dpm_write_val (f, val, 1);
	      }
	      break;
	    }
	}
      else
	fputc (*fmt, f);
      fmt++;
    }

}
