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

#include "conf.h"

static dpm_conf_var *conf_vars;

void
dpm_conf_register (dpm_conf_var *conf)
{
  conf->next = conf_vars;
  conf_vars = conf;
}

dpm_conf_var *
dpm_conf_find (const char *name)
{
  dpm_conf_var *conf;

  for (conf = conf_vars; conf; conf = conf->next)
    if (!strcmp (conf->name, name))
      return conf;
  dyn_error ("No such configuration variable: %s", name);
}

dyn_val
dpm_conf_get (dpm_conf_var *conf)
{
  return dyn_get (conf->var);
}

int
dpm_conf_true (dpm_conf_var *conf)
{
  return dyn_eq (dyn_get (conf->var), "true");
}

void
dpm_conf_set (dpm_conf_var *conf, dyn_val val)
{
  dyn_set (conf->var, dyn_apply_schema (val, conf->schema));
}

void
dpm_conf_let (dpm_conf_var *conf, dyn_val val)
{
  dyn_let (conf->var, dyn_apply_schema (val, conf->schema));
}

void
dpm_conf_dump ()
{
  dpm_conf_var *conf;

  for (conf = conf_vars; conf; conf = conf->next)
    dyn_write (dyn_stdout, "%s: %V\n", conf->name, dyn_get (conf->var));
  dyn_output_flush (dyn_stdout);
}

void
dpm_conf_parse (const char *filename)
{
  dyn_begin ();

  dyn_input in = dyn_open_file (filename);
  dyn_input_count_lines (in);

  while (1)
    {
      dyn_val form = dyn_read (in);

      if (dyn_is_eof (form))
	break;

      if (dyn_is_pair (form))
	{
	  dyn_val var = dyn_first (form);
	  dyn_val val = dyn_second (form);
	  
	  if (!dyn_is_string (var))
	    dyn_error ("variable names must be strings");

	  // dyn_print ("var %V val %V\n", var, val);

	  dpm_conf_set (dpm_conf_find (dyn_to_string (var)), val);
	}
      else
	{
	  dyn_write (dyn_stdout, "Unhandled form %V\n", form);
	  dyn_output_flush (dyn_stdout);
	}
    }

  dyn_end ();
}

DYN_DEFINE_SCHEMA (bool, (or (value true) (value false)));
