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

/* Parsing
 *
 * The parsers are functions that repeatedly invoke a callback with
 * interesting bits of the input.
 *
 * The input is represented by a dpm_stream.  When a parser function
 * returns, the stream has been updated to position and mark of the
 * stream have been advanced to the unparsed rest of the input.
 *
 * Parser functions return true when they have processed a piece of
 * the input, and false otherwise.
 *
 * Parser functions abort in case of errors.  (This will be improved
 * to allow more control, but in general, these parsers expect already
 * validated input.  They won't give nice and helpful error messages.)
 */

int dpm_parse_control (dpm_stream *ps,
		       void (*func) (dpm_stream *ps,
				     const char *name, int name_len,
				     const char *value, int value_len,
				     void *data),
		       void *data);

void dpm_parse_ar (dpm_stream *ps,
		   void (*func) (dpm_stream *ps,
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

void dpm_parse_tar (dpm_stream *ps,
		    void (*func) (dpm_stream *ps,
				  dpm_tar_member *info,
				  void *data),
		    void *data);

#endif /* !DPM_PARSE_H */
