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

#ifndef DPM_CONF_H
#define DPM_CONF_H

#include "dyn.h"

typedef struct dpm_conf_var {
  struct dpm_conf_var *next;

  const char *name;
  dyn_val schema;
  const char *docstring;
  dyn_var var[1];
} dpm_conf_var;

void dpm_conf_register (dpm_conf_var *var, const char *def);
void dpm_conf_dump (void);

dpm_conf_var *dpm_conf_find (const char *name);

dyn_val dpm_conf_get (dpm_conf_var *var);
int dpm_conf_true (dpm_conf_var *var);
int dpm_conf_int (dpm_conf_var *var);
const char *dpm_conf_string (dpm_conf_var *var);
void dpm_conf_set (dpm_conf_var *var, dyn_val val);
void dpm_conf_let (dpm_conf_var *var, dyn_val val);

#define DPM_CONF_DECLARE(_sym,_name,_schema,_def,_doc)			\
  dpm_conf_var _sym[1] = { {					        \
    .name = _name,							\
    .docstring = _doc							\
  } };								        \
  __attribute__ ((constructor))						\
  void									\
  _sym##__declare ()							\
  {									\
    dyn_ensure_init();							\
    _sym[0].schema = (dyn_read_string (_schema));			\
    dpm_conf_register (_sym, _def);					\
  }

void dpm_conf_parse (const char *filename);

#endif /* !DPM_CONF_H */
