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
#include "util.h"

int
dpm_parse_control (dpm_stream *ps,
		   void (*func) (dpm_stream *ps,
				 const char *name, int name_len,
				 const char *value, int value_len,
				 void *data),
		   void *data)
{
  int in_header = 0;

 again:
  if (dpm_stream_find (ps, ":\n")
      || dpm_stream_len (ps) > 0)
    {
      if (dpm_stream_len (ps) == 0)
	{
	  /* Empty line.  Gobble it up when we haven't seen a field yet.
	   */
	  if (!in_header)
	    {
	      dpm_stream_advance (ps, 1);
	      dpm_stream_next (ps);
	      goto again;
	    }
	  else
	    return 1;
	}
      else
	{
	  const char *name;
	  int name_len, value_off, value_len;

	  if (!dpm_stream_looking_at (ps, ":"))
	    dpm_stream_abort (ps, "No field name in '%.*s'",
			     dpm_stream_len (ps), dpm_stream_start (ps));

	  name_len = dpm_stream_len (ps);

	  dpm_stream_advance (ps, 1);
	  dpm_stream_skip (ps, " \t");

	  value_off = dpm_stream_len (ps);

	  dpm_stream_find (ps, "\n");
	  dpm_stream_advance (ps, 1);
	  while (dpm_stream_looking_at (ps, " ")
		 || dpm_stream_looking_at (ps, "\t"))
	    {
	      dpm_stream_find (ps, "\n");
	      dpm_stream_advance (ps, 1);
	    }

	  value_len = dpm_stream_len (ps) - value_off - 1;

	  name = dpm_stream_start (ps);
	  func (ps, name, name_len, name + value_off, value_len, data);

	  dpm_stream_next (ps);
	  in_header = 1;
	  goto again;
	}
    }

  return in_header;
}

typedef struct {
  char name[16];
  char mtime[12];
  char uid[6];
  char gid[6];
  char mode[8];
  char size[10];
  char magic[2];
} ar_header;

void
dpm_parse_ar (dpm_stream *ps,
	      void (*func) (dpm_stream *ps,
			    const char *member_name,
			    void *data),
	      void *data)
{
  dpm_stream_grow (ps, 8);
  if (memcmp (dpm_stream_start (ps), "!<arch>\n", 8) != 0)
    dpm_stream_abort (ps, "Not a deb file");
  dpm_stream_advance (ps, 8);
  dpm_stream_next (ps);

  while (dpm_stream_try_grow (ps, sizeof (ar_header)) >= sizeof (ar_header))
    {
      int size, name_len;
      char *name;
      ar_header *head = (ar_header *)dpm_stream_start (ps);
    
      size = atoi (head->size);

      if (size == 0)
	dpm_stream_abort (ps, "huh?");

      if (memcmp (head->name, "#1/", 3) == 0)
	{
	  dpm_stream_abort (ps, "long names not supported yet");
	}
      else
	{
	  name_len = sizeof (head->name);
	  while (name_len > 0 && head->name[name_len-1] == ' ')
	    name_len--;

	  name = dpm_xmalloc (name_len + 1);
	  memcpy (name, head->name, name_len);
	  name[name_len] = 0;
	}

      dpm_stream_advance (ps, sizeof (ar_header));
      dpm_stream_next (ps);

      dpm_stream_push_limit (ps, size);
      func (ps, name, data);
      dpm_stream_pop_limit (ps);

      dpm_stream_advance (ps, size % 2);
      dpm_stream_next (ps);

      free (name);
    }
}

typedef struct {
  char name[100];
  char mode[8];
  char userid[8];
  char groupid[8];
  char size[12];
  char mtime[12];
  char checksum[8];
  char linkflag;
  char linkname[100];
  char magicnumber[8];
  char username[32];
  char groupname[32];
  char major[8];
  char minor[8];      
} tar_header;

void
dpm_parse_tar (dpm_stream *ps,
	       void (*func) (dpm_stream *ps,
			     const char *member_name,
			     void *data),
	       void *data)
{
  char *name = NULL, *target = NULL;

  while (dpm_stream_try_grow (ps, 1) > 0)
    {
      unsigned char *block;
      tar_header *head;
      int size, checksum, wantsum, i;

      dpm_stream_grow (ps, 512);
      block = (unsigned char *)dpm_stream_start (ps);
      head = (tar_header *)block;

      /* Compute checksum, pretending the checksum field itself is
	 filled with blanks.
       */
      wantsum = strtol (head->checksum, NULL, 8);

      checksum = 0;
      for (i = 0; i < 512; i++)
	checksum += block[i];
      if (checksum == 0)
	return;
      for (i = 0; i < sizeof (head->checksum); i++)
	checksum -= head->checksum[i];
      checksum += ' '*sizeof (head->checksum);

      if (checksum != wantsum)
	dpm_stream_abort (ps, "checksum mismatch in tar header");

      size = strtol (head->size, NULL, 8);

      name = dpm_xstrdup (head->name);

      // printf ("%c %s %d\n", head->linkflag, name, size);
      dpm_stream_advance (ps, 512);
      dpm_stream_next (ps);

      dpm_stream_push_limit (ps, size);
      func (ps, name, data);
      dpm_stream_pop_limit (ps);
      
      dpm_stream_advance (ps, ((size + 511) & ~511) - size);
      dpm_stream_next (ps);
      free (name);
    }
}
