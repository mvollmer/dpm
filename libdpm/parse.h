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

#ifndef DPM_PARSE_H
#define DPM_PARSE_H

#include <sys/types.h>

#include "dyn.h"

/* Parsing
 *
 * Parsers are iterators.
 */

DYN_DECLARE_STRUCT_ITER(void, dpm_parse_comma_fields_, dyn_input in)
{
  dyn_input in;
  
  const char *field; int len;
};

DYN_DECLARE_STRUCT_ITER(void, dpm_parse_relations, dyn_input in)
{
  dyn_input in;
  bool first;

  const char *name; int name_len;
  const char *op; int op_len;
  const char *version; int version_len;
};

DYN_DECLARE_STRUCT_ITER(void, dpm_parse_lines_, dyn_input in)
{
  dyn_input in;

  int n_fields;
  const char **fields;
  int *field_lens;
};

DYN_DECLARE_STRUCT_ITER(void, dpm_parse_control_, dyn_input in)
{
  dyn_input in;
  bool starting;
  
  const char *name; int name_len;
  const char *value; int value_len;
};

/* Old style.
 */

void dpm_parse_comma_fields (dyn_input in,
			     void (*func) (dyn_input in,
					   const char *field, int field_len,
					   void *data),
			     void *data);

int dpm_parse_relation (dyn_input in,
			void (*func) (dyn_input in,
				      const char *name, int name_len,
				      const char *op, int op_len,
				      const char *version, int version_len,
				      void *data),
			void *data);

void dpm_parse_lines (dyn_input in,
		      void (*func) (dyn_input in,
				    int n_fields,
				    const char **fields, int *field_lens,
				    void *data),
		      void *data);

int dpm_parse_control (dyn_input in,
		       void (*func) (dyn_input in,
				     const char *name, int name_len,
				     const char *value, int value_len,
				     void *data),
		       void *data);

typedef struct {
  int n, max;
  char **names;
  char **values;
} dpm_control_fields;

int dpm_parse_control_fields (dyn_input in, dpm_control_fields *result);
char *dpm_control_fields_get (dpm_control_fields *fields, const char *name);
void dpm_control_fields_free (dpm_control_fields *fields);

void dpm_parse_ar (dyn_input in,
		   void (*func) (dyn_input in,
				 const char *member_name,
				 void *data),
		   void *data);

typedef enum {
  DPM_TAR_FILE = '0',
  DPM_TAR_HARDLINK = '1',
  DPM_TAR_SYMLINK = '2',
  DPM_TAR_CHAR_DEVICE = '3',
  DPM_TAR_BLOCK_DEVICE = '4',
  DPM_TAR_DIRECTORY = '5',
  DPM_TAR_FIFO = '6'
} dpm_tar_type;

typedef struct {
  dpm_tar_type type;
  char        *name;
  char        *target;
  mode_t       mode;
  uid_t        uid;
  gid_t        gid;
  off_t        size;
  time_t       mtime;
  int          major;
  int          minor;
} dpm_tar_member;

void dpm_parse_tar (dyn_input in,
		    void (*func) (dyn_input in,
				  dpm_tar_member *info,
				  void *data),
		    void *data);

#endif /* !DPM_PARSE_H */
