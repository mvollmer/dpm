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
#include "conf.h"
#include "write.h"

dyn_val
dpm_conf_filter_bool (const char *context, dyn_val val)
{
  if (dyn_is_string (val))
    {
      if (!strcmp (dyn_to_string (val), "true"))
	return val;
      if (!strcmp (dyn_to_string (val), "false"))
	return NULL;
    }
  
  dpm_error (NULL, "Unrecognized value for boolean %s: %V", context, val);
}

dyn_val
dpm_conf_unfilter_bool (dyn_val val)
{
  if (val)
    return val;
  else
    return dyn_from_string ("false");
}

dpm_conf_type dpm_conf_type_bool = {
  .name = "bool",
  .filter = dpm_conf_filter_bool,
  .unfilter = dpm_conf_unfilter_bool
};

dyn_val
dpm_conf_filter_string (const char *context, dyn_val val)
{
  if (!dyn_is_string (val))
    dpm_error (NULL, "Not a single string for %s: %V", context, val);
  return val;
}

dpm_conf_type dpm_conf_type_string = {
  .name = "string",
  .filter = dpm_conf_filter_string
};

dpm_conf_type dpm_conf_type_any = {
  .name = "any"
};

static dpm_conf_declaration *conf_vars;

void
dpm_conf_declare (dpm_conf_declaration *conf, dyn_val init)
{
  conf->next = conf_vars;
  conf_vars = conf;
  dyn_set (conf->var, init);
}

static dpm_conf_declaration *
dpm_conf_find (const char *name)
{
  dpm_conf_declaration *conf;

  for (conf = conf_vars; conf; conf = conf->next)
    if (!strcmp (conf->name, name))
      return conf;
  dpm_error (NULL, "No such configuration variable: %s", name);
}

void
dpm_conf_set (const char *name, dyn_val val)
{
  dpm_conf_declaration *conf = dpm_conf_find (name);
  dyn_val fval = (conf->type->filter
		  ? conf->type->filter (name, val)
		  : val);
  dyn_set (conf->var, fval);
}

void
dpm_conf_let (const char *name, dyn_val val)
{
  dpm_conf_declaration *conf = dpm_conf_find (name);
  dyn_val fval = (conf->type->filter
		  ? conf->type->filter (name, val)
		  : val);
  dyn_let (conf->var, fval);
}

void
dpm_conf_dump ()
{
  dpm_conf_declaration *conf;

  for (conf = conf_vars; conf; conf = conf->next)
    {
      dyn_val val = dyn_get (conf->var);
      dyn_val uval = (conf->type->unfilter
		      ? conf->type->unfilter (val)
		      : val);
      // printf ("# %s\n", conf->docstring);
      dpm_print ("%s %V\n", conf->name, uval);
    }
}

typedef struct {
  dpm_stream *stream;
  int lineno;
  int cur_kind, cur_len;
} dpm_conf_parse_state;

static void
dpm_conf_parse_next (dpm_conf_parse_state *state)
{
  dpm_stream *stream = state->stream;

 again:
  dpm_stream_skip (stream, " \t\r\v\n");
  if (dpm_stream_looking_at (stream, "#"))
    {
      dpm_stream_find (stream, "\n");
      goto again;
    }

  dpm_stream_set_mark (stream);
  if (dpm_stream_looking_at (stream, "\""))
    {
      /* Quoted string.
       */
      dpm_stream_advance (stream, 1);
      dpm_stream_set_mark (stream);
      while (dpm_stream_find (stream, "\""))
	{
	  char *mark = dpm_stream_mark (stream);
	  const char *pos = dpm_stream_pos (stream);
	  char *p1, *p2;

	  if (pos > mark && pos[-1] == '\\')
	    {
	      dpm_stream_advance (stream, 1);
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
		      dpm_error (stream, "Unsupported escape '%c'", *p1);
		      break;
		    }
		  p1++;
		}
	      else
		*p2++ = *p1++;
	    }

	  dpm_stream_advance (stream, 1);
	  state->cur_kind = '"';
	  state->cur_len = p2 - mark;
	  return;
	}

      dpm_error (stream, "Unexpected end of input within quotes");
    }
  else if (!dpm_stream_looking_at (stream, "{")
	   && !dpm_stream_looking_at (stream, "}")
	   && !dpm_stream_looking_at (stream, "\n"))
    {
      /* Symbol.
       */
      dpm_stream_find (stream, " \t\r\v\n{}\"");
      if (dpm_stream_pos (stream) > dpm_stream_mark (stream))
	{
	  state->cur_kind = 'a';
	  state->cur_len = dpm_stream_pos (stream) - dpm_stream_mark (stream);
	}
      else
	{
	  state->cur_kind = '\0';
	  state->cur_len = 0;
	}
      return;
    }
  else if (dpm_stream_try_grow (stream, 1))
    {
      /* Some single character delimiter.
       */
      dpm_stream_advance (stream, 1);
      state->cur_kind = *dpm_stream_mark (stream);
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

static dyn_val dpm_conf_parse_list (dpm_conf_parse_state *state);

static dyn_val
dpm_conf_parse_element (dpm_conf_parse_state *state)
{
  dyn_val elt = NULL;

  if (state->cur_kind == '{')
    {
      dpm_conf_parse_next (state);
      elt = dpm_conf_parse_list (state);
      if (state->cur_kind != '}')
	dpm_error (state->stream, "Unexpected end of input in list");
    }
  else if (state->cur_kind == '}')
    {
      dpm_error (state->stream, "Unexpected list delimiter");
    }
  else
    {
      elt = dyn_from_stringn (dpm_stream_mark (state->stream),
			      state->cur_len);
    }

  dpm_conf_parse_next (state);
  return elt;
}

static dyn_val
dyn_reverse (dyn_val val)
{
  dyn_val res = NULL;
  while (dyn_is_pair (val))
    {
      res = dyn_cons (dyn_first (val), res);
      val = dyn_rest (val);
    }
  return res;
}

static dyn_val
dpm_conf_parse_list (dpm_conf_parse_state *state)
{
  dyn_val res = NULL;

  while (state->cur_kind != 0
	 && state->cur_kind != '}')
    res = dyn_cons (dpm_conf_parse_element (state), res);

  return dyn_reverse (res);
}

void
dpm_conf_parse (const char *filename)
{
  dpm_conf_parse_state state;
  int lineno;
  dyn_val var, val;

  dyn_begin ();

  state.stream = dpm_stream_open_file (filename);
  dpm_stream_count_lines (state.stream);

  dpm_conf_parse_next (&state);

  while (1)
    {
      state.lineno = 0;
      if (state.cur_kind == 0)
	break;

      lineno = dpm_stream_lineno (state.stream);
      var = dpm_conf_parse_element (&state);
      val = dpm_conf_parse_element (&state);
      state.lineno = lineno;
      if (!dyn_is_string (var))
	dpm_error (NULL, "Variable names must be strings");
      dpm_conf_set (dyn_to_string (var), val);
    }

  dyn_end ();
}
