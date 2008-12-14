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

static void
dpm_conf_assert_one_token (const char *context, char **tokens)
{
  if (tokens[0] == NULL)
    dpm_error ("Missing value for %s", context);
  if (tokens[1] != NULL)
    dpm_error ("Junk after value for %s: '%s'", context, tokens[1]);
}

void *
dpm_conf_parse_bool (const char *context, char **tokens)
{
  dpm_conf_assert_one_token (context, tokens);
  if (!strcmp (tokens[0], "true"))
    return (void *)1;
  if (!strcmp (tokens[0], "false"))
    return (void *)0;
  dpm_error ("Unsupported value for boolean %s: %s", context, tokens[0]);
}

void
dpm_conf_write_bool (FILE *f, void *value)
{
  fprintf (f, "%s", (value? "true" : "false"));
}

dpm_conf_type dpm_conf_type_bool = {
  .name = "bool",
  .free = NULL,
  .parse = dpm_conf_parse_bool,
  .write = dpm_conf_write_bool
};

static dpm_conf_declaration *conf_vars;

void
dpm_conf_declare (dpm_conf_declaration *conf, const char *init)
{
  conf->next = conf_vars;
  conf_vars = conf;

  {
    const char *tokens[2] = { init, NULL };
    dyn_set (conf->var, conf->type->parse (conf->name, tokens));
  }
}

static dpm_conf_declaration *
dpm_conf_find (const char *name)
{
  dpm_conf_declaration *conf;

  for (conf = conf_vars; conf; conf = conf->next)
    if (!strcmp (conf->name, name))
      return conf;
  dpm_error ("No such configuration variable: %s", name);
}

void
dpm_conf_setv (const char *name, char **tokens)
{
  dpm_conf_declaration *conf = dpm_conf_find (name);
  dyn_set (conf->var, conf->type->parse (conf->name, tokens));
}

void
dpm_conf_set (const char *name, char *value)
{
  char *tokens[2] = { value, NULL };
  dpm_conf_setv (name, tokens);
}

void
dpm_conf_letv (const char *name, char **tokens)
{
  dpm_conf_declaration *conf = dpm_conf_find (name);
  dyn_let (conf->var, conf->type->parse (conf->name, tokens));
}

void
dpm_conf_let (const char *name, char *value)
{
  char *tokens[2] = { value, NULL };
  dpm_conf_letv (name, tokens);
}

void
dpm_conf_dump ()
{
  dpm_conf_declaration *conf;

  for (conf = conf_vars; conf; conf = conf->next)
    {
      // printf ("# %s\n", conf->docstring);
      printf ("%s ", conf->name);
      conf->type->write (stdout, dyn_get (conf->var));
      printf ("\n");
    }
}

typedef struct {
  dpm_stream *stream;
  int lineno;
  int cur_kind, cur_len;

  char **tokens;
  int max, n;
} dpm_conf_parse_state;

static void
dpm_conf_parse_next (dpm_conf_parse_state *state, int stop_at_newline)
{
  dpm_stream *stream = state->stream;

 again:
  dpm_stream_skip (stream, (stop_at_newline? " \t\r\v" : " \t\r\v\n"));
  if (dpm_stream_looking_at (stream, "#"))
    {
      dpm_stream_find (stream, "\n");
      goto again;
    }

  dpm_stream_next (stream);
  if (dpm_stream_looking_at (stream, "\""))
    {
      /* Quoted string.
       */
      dpm_stream_advance (stream, 1);
      dpm_stream_next (stream);
      while (dpm_stream_find (stream, "\""))
	{
	  char *mark = dpm_stream_start (stream);
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
		      dpm_error ("Unsupported escape '%c'", *p1);
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

      dpm_error ("Unexpected end of input within quotes");
    }
  else if (!dpm_stream_looking_at (stream, "{")
	   && !dpm_stream_looking_at (stream, "}")
	   && !dpm_stream_looking_at (stream, "\n"))
    {
      /* Symbol.
       */
      dpm_stream_find (stream, " \t\r\v\n{}\"");
      if (dpm_stream_len (stream) > 0)
	{
	  state->cur_kind = 'a';
	  state->cur_len = dpm_stream_len (stream);
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
      state->cur_kind = *dpm_stream_start (stream);
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

static void
dpm_conf_store_token (dpm_conf_parse_state *state)
{
  if (state->n >= state->max)
    {
      state->max *= 2;
      state->tokens =
	dpm_xremalloc (state->tokens,
		       sizeof (char *) * (state->max + 1));
    }
  state->tokens[state->n++] = dpm_xstrndup (dpm_stream_start (state->stream),
					    state->cur_len);
}

static void
dpm_conf_parse_list (dpm_conf_parse_state *state,
		     int delim)
{
  dpm_conf_parse_next (state, !delim);

  while (state->cur_kind != 0)
    {
      if (state->cur_kind == '{')
	{
	  dpm_conf_store_token (state);
	  dpm_conf_parse_list (state, 1);
	  if (state->cur_kind != '}')
	    dpm_error ("Unexpected end of input in list");
	  dpm_conf_store_token (state);
	}
      else if (state->cur_kind == '}')
	{
	  if (delim)
	    break;
	  dpm_error ("Unexpected list delimiter");
	}
      else if (state->cur_kind == '\n')
	{
	  if (!delim)
	    break;
	  dpm_conf_store_token (state);
	}
      else
	dpm_conf_store_token (state);

      dpm_conf_parse_next (state, 1);
    }
}

static void
dpm_conf_parse_cleanup (int for_throw, void *data)
{
  dpm_conf_parse_state *state = (dpm_conf_parse_state *)data;
  char **ptr;

  for (ptr = state->tokens; *ptr; ptr++)
    free (*ptr);
  free (state->tokens);
  
  dpm_stream_close (state->stream);
}

char *
dpm_conf_error_context (const char *message, int level, void *data)
{
  dpm_conf_parse_state *state = (dpm_conf_parse_state *)data;

  return dpm_sprintf ("%s:%d: %s",
		      dpm_stream_filename (state->stream),
		      (state->lineno
		       ? state->lineno
		       : dpm_stream_lineno (state->stream)),
		      message);
}

dpm_conf_parse (const char *filename)
{
  dpm_conf_parse_state state;
  int lineno;
  char **ptr;

  dyn_begin ();

  state.stream = dpm_stream_open_file (filename, NULL);
  state.max = 5;
  state.n = 0;
  state.tokens = dpm_xmalloc (sizeof (char *) * (state.max + 1));

  dpm_stream_count_lines (state.stream);
  dpm_let_error_context (dpm_conf_error_context, &state);

  dyn_wind (dpm_conf_parse_cleanup, &state);

  while (1)
    {
      state.lineno = 0;
      dpm_conf_parse_next (&state, 0);
      if (state.cur_kind == 0)
	break;

      lineno = dpm_stream_lineno (state.stream);
      dpm_conf_store_token (&state);

      dpm_conf_parse_list (&state, 0);
      state.lineno = lineno;
      dpm_conf_setv (state.tokens[0], state.tokens + 1);

      for (ptr = state.tokens; *ptr; ptr++)
	{
	  free (*ptr);
	  *ptr = NULL;
	}
      state.n = 0;
    }

  dyn_end ();
}
