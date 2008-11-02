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

#ifndef DPM_H
#define DPM_H

/* Parsing
 *
 * The parsers are functions that repeatedly invoke a callback with
 * interesting bits of the input.
 *
 * The input is represented by a dpm_parse_state object.  When a
 * parser function returns, the parse state has been updated to refer
 * to the unparsed rest of the input.
 *
 * Parser functions return true when they have processed a piece of
 * the input, and false when the input is empty.
 *
 * Parser functions abort in case of errors.  (This will be improved
 * to allow more control, but in general, these parsers expect already
 * validated input.  They won't give nice and helpful error messages.)
 */

typedef struct dpm_parse_state dpm_parse_state;

typedef void dpm_parse_error_callback (dpm_parse_state *ps, const char *msg);

dpm_parse_state *dpm_parse_open_file (const char *filename,
				      dpm_parse_error_callback *on_error);
dpm_parse_state *dpm_parse_open_string (const char *str, int len);
dpm_parse_state *dpm_parse_open_zlib (dpm_parse_state *ps);

void dpm_parse_on_error (dpm_parse_state *ps,
			 dpm_parse_error_callback *on_error);
void dpm_parse_close (dpm_parse_state *ps);
void dpm_parse_abort (dpm_parse_state *ps, const char *fmt, ...);

const char *dpm_parse_start (dpm_parse_state *ps);
int dpm_parse_len (dpm_parse_state *ps);
void dpm_parse_next (dpm_parse_state *ps);
int dpm_parse_try_grow (dpm_parse_state *ps, int len);
int dpm_parse_grow (dpm_parse_state *ps, int n);

int dpm_parse_find (dpm_parse_state *p, const char *delims);
void dpm_parse_skip (dpm_parse_state *p, const char *chars);
void dpm_parse_skip_n (dpm_parse_state *ps, int n);
int dpm_parse_looking_at (dpm_parse_state *p, const char *str);

int dpm_parse_header (dpm_parse_state *ps,
		      void (*func) (dpm_parse_state *ps,
				    const char *name, int name_len,
				    const char *value, int value_len,
				    void *data),
		      void *data);

#endif /* !DPM_H */
