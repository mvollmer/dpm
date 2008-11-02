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

#include <sys/fcntl.h>
#include <sys/stat.h>

#include <zlib.h>

#include "dpm.h"

void *
dpm_xmalloc (size_t size)
{
  void *mem = malloc (size);
  if (mem == NULL)
    {
      fprintf (stderr, "Out of memory.\n");
      abort ();
    }
  return mem;
}

void *
dpm_xremalloc (void *old, size_t size)
{
  void *mem = realloc (old, size);
  if (mem == NULL)
    {
      fprintf (stderr, "Out of memory.\n");
      abort ();
    }
  return mem;
}

void *
dpm_xstrdup (const char *str)
{
  char *dup;

  if (str == NULL)
    return NULL;

  dup = dpm_xmalloc (strlen (str) + 1);
  strcpy (dup, str);
  return dup;
}

struct dpm_parse_state {
  dpm_parse_state *parent;
  int close_parent;
  char *filename;
  dpm_parse_error_callback *on_error;

  int (*read) (dpm_parse_state *ps, char *buf, int n);
  void (*close) (void *stream);
  void *stream;

  int bufstatic;
  char *buf, *bufend;
  int bufsize;

  char *start;
  char *pos;
};

/* Aborting
 */

void
dpm_parse_abort (dpm_parse_state *ps, const char *fmt, ...)
{
  va_list ap;
  char *message;

  va_start (ap, fmt);
  if (vasprintf (&message, fmt, ap) < 0)
    message = NULL;
  va_end (ap);

  while (ps->on_error == NULL && ps->parent)
    {
      dpm_parse_state *parent = ps->parent;
      dpm_parse_close (ps);
      ps = parent;
    }

  if (ps->on_error)
    ps->on_error (ps, message);

  if (ps->filename)
    fprintf (stderr, "%s: %s\n", ps->filename, message);
  else
    fprintf (stderr, "%s\n", message);
  free (message);
  dpm_parse_close (ps);
  exit (1);
}

dpm_parse_state *
dpm_parse_new (dpm_parse_state *parent)
{
  dpm_parse_state *ps;

  ps = dpm_xmalloc (sizeof (dpm_parse_state));
  ps->parent = parent;
  ps->close_parent = 0;
  ps->filename = NULL;
  ps->on_error = NULL;
  ps->read = NULL;
  ps->close = NULL;

  ps->bufstatic = 0;
  ps->buf = NULL;
  ps->bufsize = 0;
  ps->bufend = ps->buf;

  ps->start = ps->buf;
  ps->pos = ps->start;

  return ps;
}

void
dpm_parse_close_parent (dpm_parse_state *ps)
{
  ps->close_parent = 1;
}

void
dpm_parse_set_static_buffer (dpm_parse_state *ps, char *buf, int len)
{
  ps->bufstatic = 1;
  ps->buf = (char *)buf;
  ps->bufsize = len;
  ps->bufend = ps->buf + ps->bufsize;

  ps->start = ps->buf;
  ps->pos = ps->start;
}

//#define BUFMASK 0xF
#define BUFMASK 0xFFFF
#define BUFSIZE (BUFMASK+1)

static int
dpm_fd_read (dpm_parse_state *ps, char *buf, int n)
{
  int fd = (int) ps->stream;
  return read (fd, buf, n);
}

static void
dpm_fd_close (void *stream)
{
  int fd = (int) stream;
  close (fd);
}

static int
has_suffix (const char *str, const char *suffix)
{
  char *pos = strstr (str, suffix);
  return pos && pos[strlen(suffix)] == '\0';
}

dpm_parse_state *
dpm_parse_open_file (const char *filename,
		     dpm_parse_error_callback *on_error)
{
  int fd;
  dpm_parse_state *ps = dpm_parse_new (NULL);

  ps->filename = dpm_xstrdup (filename);
  ps->on_error = on_error;
  ps->read = dpm_fd_read;
  ps->close = dpm_fd_close;

  fd = open (filename, O_RDONLY);
  ps->stream = (void *)fd;
  if (fd < 0)
    dpm_parse_abort (ps, "%m");

  if (has_suffix (filename, ".gz"))
    {
      ps = dpm_parse_open_zlib (ps);
      dpm_parse_close_parent (ps);
    }

  return ps;
}

dpm_parse_state *
dpm_parse_open_string (const char *str, int len)
{
  dpm_parse_state *ps = dpm_parse_new (NULL);
  dpm_parse_set_static_buffer (ps, (char *)str, (len < 0)? strlen (str) : len);
  return ps;
}

static const char *
zerr (int ret)
{
  switch (ret) 
    {
    case Z_ERRNO:
      return "%m";
    case Z_STREAM_ERROR:
      return "invalid compression level";
    case Z_DATA_ERROR:
      return "invalid or incomplete deflate data";
    case Z_MEM_ERROR:
      return "out of memory";
    case Z_VERSION_ERROR:
      return "zlib version mismatch!";
    default:
      return "zlib error %d";
    }
}

static int
dpm_zlib_read (dpm_parse_state *ps, char *buf, int n)
{
  dpm_parse_state *pp = ps->parent;
  z_stream *stream = (z_stream *)ps->stream;
  int avail, ret;

  avail = dpm_parse_try_grow (pp, 1);
  
  if (avail == 0)
    return 0;

  stream->avail_in = avail;
  stream->next_in = (char *)dpm_parse_start (pp);
  stream->avail_out = n;
  stream->next_out = buf;

  ret = inflate (stream, Z_NO_FLUSH);
  if (ret != Z_OK && ret != Z_STREAM_END)
    dpm_parse_abort (ps, zerr (ret), ret);

  dpm_parse_skip_n (pp, avail - stream->avail_in);
  dpm_parse_next (pp);

  return n - stream->avail_out;
}

static void
dpm_zlib_close (void *stream)
{
  z_stream *z = (z_stream *)stream;
  inflateEnd (z);
  free (z);
}

dpm_parse_state *
dpm_parse_open_zlib (dpm_parse_state *parent)
{
  dpm_parse_state *ps = dpm_parse_new (parent);
  z_stream *stream = dpm_xmalloc (sizeof (z_stream));
  int ret;

  ps->stream = NULL;
  ps->read = dpm_zlib_read;
  ps->close = dpm_zlib_close;

  stream->zalloc = Z_NULL;
  stream->zfree = Z_NULL;
  stream->opaque = Z_NULL;
  stream->avail_in = 0;
  stream->next_in = NULL;
  ps->stream = stream;

  ret = inflateInit2 (stream, 32+15);
  if (ret != Z_OK)
    dpm_parse_abort (ps, zerr (ret), ret);

  return ps;
}

void
dpm_parse_on_error (dpm_parse_state *ps, 
		    dpm_parse_error_callback *on_error)
{
  ps->on_error = on_error;
}

void
dpm_parse_close (dpm_parse_state *ps)
{
  if (ps->close_parent && ps->parent)
    dpm_parse_close (ps->parent);

  if (ps->close)
    ps->close (ps->stream);

  if (!ps->bufstatic)
    free (ps->buf);
  free (ps->filename);
  free (ps);
}

int
dpm_parse_try_grow (dpm_parse_state *ps, int n)
{
#if 0
  fprintf (stderr, "GROW n %d, start %d, pos %d, end %d\n",
	   n,
	   ps->start - ps->buf,
	   ps->pos - ps->buf,
	   ps->bufend - ps->buf);
#endif

  if (ps->pos + n > ps->bufend)
    {
      /* Need to read more input
       */

      if (ps->bufstatic)
	return ps->bufend - ps->pos;

      if (ps->pos + n - ps->start > ps->bufsize)
	{
	  /* Need a bigger buffer
	   */
	  int newsize = ((ps->pos + n - ps->start) + BUFMASK) & ~BUFMASK;
	  char *newbuf = dpm_xmalloc (newsize);
	  memcpy (newbuf, ps->start, ps->bufend - ps->start);
	  ps->bufend = newbuf + (ps->bufend - ps->buf);
	  ps->pos = newbuf + (ps->pos - ps->start);
	  ps->start = newbuf;
	  free (ps->buf);
	  ps->buf = newbuf;
	  ps->bufsize = newsize;
	}
      else if (ps->pos + n > ps->buf + ps->bufsize)
	{
	  /* Need to slide down start to front of buffer
	   */
	  int d = ps->start - ps->buf;
	  memcpy (ps->buf, ps->start, ps->bufend - ps->start);
	  ps->bufend -= d;
	  ps->start -= d;
	  ps->pos -= d;
	}

      /* Fill buffer */
      while (ps->pos + n > ps->bufend)
	{
	  int l;
	  l = ps->read (ps, ps->bufend, ps->buf + ps->bufsize - ps->bufend);
#if 0
	  fprintf (stderr, "READ %d of %d\n", l,
		   ps->buf + ps->bufsize - ps->bufend);
#endif
	  if (l < 0)
	    dpm_parse_abort (ps, "%m", ps->filename);
	  if (l == 0)
	    break;
	  ps->bufend += l;
	}
    }

#if 0
  fprintf (stderr, "NOW  n %d, start %d, pos %d, end %d\n",
	   ps->bufend - ps->pos,
	   ps->start - ps->buf,
	   ps->pos - ps->buf,
	   ps->bufend - ps->buf);
#endif

  return ps->bufend - ps->pos;
}

int
dpm_parse_grow (dpm_parse_state *ps, int n)
{
  int l = dpm_parse_try_grow (ps, n);
  if (l < n)
    dpm_parse_abort (ps, "Unexpected end of file.");
  return l;
}

const char *
dpm_parse_start (dpm_parse_state *ps)
{
  return ps->start;
}

int 
dpm_parse_len (dpm_parse_state *ps)
{
  return ps->pos - ps->start;
}

void
dpm_parse_next (dpm_parse_state *ps)
{
  ps->start = ps->pos;
}

void
dpm_parse_skip_n (dpm_parse_state *ps, int n)
{
  dpm_parse_grow (ps, n);
  ps->pos += n;
}

int
dpm_parse_looking_at (dpm_parse_state *ps, const char *str)
{
  int n = strlen (str);
  if (dpm_parse_try_grow (ps, n) >= n)
    return memcmp (ps->pos, str, n) == 0;
  else
    return 0;
}

int
dpm_parse_find (dpm_parse_state *ps, const char *delims)
{
  while (1)
    {
      char *ptr, *end;
      int n = dpm_parse_try_grow (ps, 1);
      
      if (n == 0)
	return 0;

      for (ptr = ps->pos, end = ps->pos + n; ptr < end; ptr++)
	if (strchr (delims, *ptr))
	  break;

      ps->pos = ptr;
      if (ptr < end)
	return 1;
    }
}

void
dpm_parse_skip (dpm_parse_state *ps, const char *chars)
{
  while (1)
    {
      char *ptr, *end;
      int n = dpm_parse_try_grow (ps, 1);
      
      if (n == 0)
	return;

      for (ptr = ps->pos, end = ps->pos + n; ptr < end; ptr++)
	if (!strchr (chars, *ptr))
	  break;

      ps->pos = ptr;
      if (ptr < end)
	return;
    }
}


int
dpm_parse_header (dpm_parse_state *ps,
		  void (*func) (dpm_parse_state *ps,
				const char *name, int name_len,
				const char *value, int value_len,
				void *data),
		  void *data)
{
  int in_header = 0;

 again:
  if (dpm_parse_find (ps, ":\n")
      || dpm_parse_len (ps) > 0)
    {
      if (dpm_parse_len (ps) == 0)
	{
	  /* Empty line.  Gobble it up when we haven't seen a field yet.
	   */
	  if (!in_header)
	    {
	      dpm_parse_skip_n (ps, 1);
	      dpm_parse_next (ps);
	      goto again;
	    }
	  else
	    return 1;
	}
      else
	{
	  const char *name;
	  int name_len, value_off, value_len;

	  if (!dpm_parse_looking_at (ps, ":"))
	    dpm_parse_abort (ps, "No field name in '%.*s'",
			     dpm_parse_len (ps), dpm_parse_start (ps));

	  name_len = dpm_parse_len (ps);

	  dpm_parse_skip_n (ps, 1);
	  dpm_parse_skip (ps, " \t");

	  value_off = dpm_parse_len (ps);

	  dpm_parse_find (ps, "\n");
	  dpm_parse_skip_n (ps, 1);
	  while (dpm_parse_looking_at (ps, " ")
		 || dpm_parse_looking_at (ps, "\t"))
	    {
	      dpm_parse_find (ps, "\n");
	      dpm_parse_skip_n (ps, 1);
	    }

	  value_len = dpm_parse_len (ps) - value_off - 1;

	  name = dpm_parse_start (ps);
	  func (ps, name, name_len, name + value_off, value_len, data);

	  dpm_parse_next (ps);
	  in_header = 1;
	  goto again;
	}
    }

  return in_header;
}
