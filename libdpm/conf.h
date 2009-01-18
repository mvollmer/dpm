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
#include "stream.h"

typedef struct dpm_conf_declaration {
  struct dpm_conf_declaration *next;

  dyn_var *var;
  const char *name;
  dyn_val schema;
  const char *docstring;
} dpm_conf_declaration;

void dpm_conf_register (dpm_conf_declaration *decl);
void dpm_conf_dump (void);

void dpm_conf_set (const char *name, dyn_val val);
void dpm_conf_let (const char *name, dyn_val val);

#define DPM_CONF_DECLARE(_sym,_name,_schema,_doc)			\
  dyn_var _sym;								\
  dpm_conf_declaration _sym##__decl = {					\
    .var = &_sym,							\
    .name = _name,							\
    .docstring = _doc							\
  };									\
  __attribute__ ((constructor))						\
  void									\
  _sym##__declare ()							\
  {									\
    _sym##__decl.schema = (_schema);                                    \
    dpm_conf_register (&_sym##__decl);				        \
  }

void dpm_conf_parse (const char *filename);

#endif /* !DPM_CONF_H */
