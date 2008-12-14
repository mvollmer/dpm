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

#ifndef DPM_STREAM_H
#define DPM_STREAM_H

#include <sys/types.h>

/* Streams
 *
 * This is a little abstraction for buffered I/O that can replace
 * stdio for input processing.  A stream exposes its buffer so that
 * clients can directly exmine it.  The fundamental operation on a
 * stream is to put a mark at the current position and then advance
 * the stream as far as desired.  The content of the stream from the
 * mark to the new current position is then available in a contigous
 * buffer in memory.  When done with that buffer, the client advances
 * the mark as well.
 *
 * The client is allowed to modify the buffer between the mark and the
 * current position.
 *
 * Advancing the current position is done via looking ahead in the
 * stream: the buffer is enlarged beyond the current position by a
 * specified amount and can then be examined by the client.
 *
 * You can temporarily limit the length of a stream.
 */

typedef struct dpm_stream dpm_stream;

typedef void dpm_stream_error_callback (dpm_stream *ps, const char *msg);

dpm_stream *dpm_stream_open_file (const char *filename,
				  dpm_stream_error_callback *on_error);
dpm_stream *dpm_stream_open_string (const char *str, int len);
dpm_stream *dpm_stream_open_zlib (dpm_stream *ps);
dpm_stream *dpm_stream_open_bz2 (dpm_stream *ps);
void dpm_stream_close_parent (dpm_stream *ps);

void dpm_stream_count_lines (dpm_stream *stream);
int dpm_stream_lineno (dpm_stream *stream);
const char *dpm_stream_filename (dpm_stream *stream);

void dpm_stream_on_error (dpm_stream *ps,
			  dpm_stream_error_callback *on_error);
void dpm_stream_close (dpm_stream *ps);
void dpm_stream_abort (dpm_stream *ps, const char *fmt, ...);

void dpm_stream_push_limit (dpm_stream *ps, int len);
void dpm_stream_pop_limit (dpm_stream *ps);

char *dpm_stream_start (dpm_stream *ps);
int dpm_stream_len (dpm_stream *ps);
void dpm_stream_next (dpm_stream *ps);

/* Low level, unsafe */
const char *dpm_stream_pos (dpm_stream *ps);
void dpm_stream_set_pos (dpm_stream *ps, const char *pos);
int dpm_stream_try_grow (dpm_stream *ps, int len);
int dpm_stream_grow (dpm_stream *ps, int n);

/* Higher level, safer */
void dpm_stream_advance (dpm_stream *ps, int n);
int dpm_stream_find (dpm_stream *p, const char *delims);
int dpm_stream_find_after (dpm_stream *p, const char *delims);
void dpm_stream_skip (dpm_stream *p, const char *chars);
int dpm_stream_looking_at (dpm_stream *p, const char *str);

#endif /* !DPM_STREAM_H */
