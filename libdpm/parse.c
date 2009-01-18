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

#include <sys/fcntl.h>
#include <sys/stat.h>

#include "util.h"
#include "parse.h"

static int
linear_whitespace_p (char c)
{
  return c == ' ' || c == '\t';
}

static int
whitespace_p (char c)
{
  return c == ' ' || c == '\t' || c == '\n';
}

static int
decode_extended_value (char *value, int len)
{
  /* For all lines, the first space is removed. If after that a line
     consists solely of a '.', that dot is removed as well.

     XXX - skip trailing whitespace of lines.
  */

  char *src = value, *dst = value;
  char *bol = src;

  while (src < value + len)
    {
      if (src == bol && linear_whitespace_p (*src))
	{
	  src++;
	  continue;
	}

      if (*src == '\n')
	{
	  if (bol+2 == src
	      && linear_whitespace_p (bol[0])
	      && bol[1] == '.')
	    dst--;
	  bol = src + 1;
	}

      *dst++ = *src++;
    }

  return dst - value;
}

static void
decode_value (char **value_ptr, int *value_len_ptr)
{
  char *value = *value_ptr, *rest;
  int len = *value_len_ptr;

  while (len > 0 && linear_whitespace_p (value[0]))
    {
      value++;
      len--;
    }

  *value_ptr = value;
  *value_len_ptr = len;

  rest = memchr (value, '\n', len);
  if (rest && rest < value+len-1)
    {
      int skip = rest + 1 - value;
      *value_len_ptr = (decode_extended_value (rest + 1, len - skip)
			+ skip);
    }
  else
    {
      while (len > 0 && whitespace_p (value[len-1]))
	len--;
      *value_len_ptr = len;
    }
}

int
dpm_parse_control (dyn_input in,
		   void (*func) (dyn_input in,
				 const char *name, int name_len,
				 const char *value, int value_len,
				 void *data),
		   void *data)
{
  int in_header = 0;

  dyn_input_set_mark (in);

  while (dyn_input_find (in, ":\n")
	 || dyn_input_pos (in) > dyn_input_mark (in))
    {
      if (dyn_input_pos (in) == dyn_input_mark (in))
	{
	  /* Empty line.  Gobble it up when we haven't seen a field yet.
	   */
	  if (!in_header)
	    {
	      dyn_input_advance (in, 1);
	      dyn_input_set_mark (in);
	    }
	  else
	    return 1;
	}
      else
	{
	  char *name, *value;
	  int name_len, value_off, value_len;

	  if (!dyn_input_looking_at (in, ":"))
	    dyn_error ("No field name in '%.*s'",
		       dyn_input_pos (in) - dyn_input_mark (in),
		       dyn_input_mark (in));

	  name_len = dyn_input_pos (in) - dyn_input_mark (in);

	  dyn_input_advance (in, 1);

	  value_off = dyn_input_pos (in) - dyn_input_mark (in);

	  dyn_input_find_after (in, "\n");
	  while (dyn_input_looking_at (in, " ")
		 || dyn_input_looking_at (in, "\t"))
	    dyn_input_find_after (in, "\n");

	  value_len = dyn_input_pos (in) - dyn_input_mark (in) - value_off;

	  name = dyn_input_mark (in);
	  value = name + value_off;
	  decode_value (&value, &value_len);
	  func (in, name, name_len, value, value_len, data);

	  dyn_input_set_mark (in);
	  in_header = 1;
	}
    }

  return in_header;
}

static uintmax_t
dpm_parse_uint (dyn_input in,
		const char *str, int len, int base,
		uintmax_t max)
{
  uintmax_t val;
  const char *ptr = str, *end = str + len;

  while (ptr < end && whitespace_p (*ptr))
    ptr++;
  
  val = 0;
  while (ptr < end && *ptr >= '0' && *ptr < '0' + base)
    {
      uintmax_t old = val;
      val = val * base + *ptr - '0';
      if (val < old)
	goto out_of_range;
      ptr++;
    }
  if (val > max)
    {
    out_of_range:
      dyn_error ("value out of range: %.*s", len, str);
    }
  
  while (ptr < end && whitespace_p (*ptr))
    ptr++;
  
  if (*ptr && ptr != end)
    dyn_error ("junk at end of number: %.*s", len, str);

  return val;
}

#define OFF_T_MAX ((off_t)-1)

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
dpm_parse_ar (dyn_input in,
	      void (*func) (dyn_input in,
			    const char *member_name,
			    void *data),
	      void *data)
{
  dyn_input_must_grow (in, 8);
  if (memcmp (dyn_input_mark (in), "!<arch>\n", 8) != 0)
    dyn_error ("Not a deb file");
  dyn_input_advance (in, 8);
  dyn_input_set_mark (in);

  while (dyn_input_grow (in, sizeof (ar_header)) >= sizeof (ar_header))
    {
      off_t size;
      char *name;
      int name_len;
      ar_header *head = (ar_header *)dyn_input_mark (in);
    
      size = dpm_parse_uint (in, head->size, sizeof (head->size), 10, 
			     OFF_T_MAX);

      if (size == 0)
	dyn_error ("huh?");

      if (memcmp (head->name, "#1/", 3) == 0)
	{
	  dyn_error ("long names not supported yet");
	}
      else
	{
	  name_len = sizeof (head->name);
	  while (name_len > 0 && head->name[name_len-1] == ' ')
	    name_len--;

	  name = dyn_malloc (name_len + 1);
	  memcpy (name, head->name, name_len);
	  name[name_len] = 0;
	}

      dyn_input_advance (in, sizeof (ar_header));
      dyn_input_set_mark (in);

      dyn_input_push_limit (in, size);
      func (in, name, data);
      dyn_input_pop_limit (in);

      dyn_input_advance (in, size % 2);
      dyn_input_set_mark (in);

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
dpm_parse_tar (dyn_input in,
	       void (*func) (dyn_input in,
			     dpm_tar_member *info,
			     void *data),
	       void *data)
{
  dpm_tar_member info;

  info.name = NULL;
  info.target = NULL;

  while (dyn_input_grow (in, 1) > 0)
    {
      unsigned char *block;
      tar_header *head;
      int checksum, wantsum, i;

      dyn_input_must_grow (in, 512);
      block = (unsigned char *)dyn_input_mark (in);
      head = (tar_header *)block;

      /* Compute checksum, pretending the checksum field itself is
	 filled with blanks.
       */
      wantsum = dpm_parse_uint (in,
				head->checksum, sizeof (head->checksum), 8,
				INT_MAX);

      checksum = 0;
      for (i = 0; i < 512; i++)
	checksum += block[i];
      if (checksum == 0)
	return;
      for (i = 0; i < sizeof (head->checksum); i++)
	checksum -= head->checksum[i];
      checksum += ' '*sizeof (head->checksum);

      if (checksum != wantsum)
	dyn_error ("checksum mismatch in tar header");

      info.size  = dpm_parse_uint (in,
				   head->size, sizeof (head->size), 8,
				   OFF_T_MAX);
      info.mode  = dpm_parse_uint (in,
				   head->mode, sizeof (head->mode), 8,
				   INT_MAX);
      info.uid   = dpm_parse_uint (in,
				   head->userid, sizeof (head->userid), 8,
				   INT_MAX);
      info.gid   = dpm_parse_uint (in,
				   head->groupid, sizeof (head->groupid), 8,
				   INT_MAX);
      info.mtime = dpm_parse_uint (in,
				   head->mtime, sizeof (head->mtime), 8,
				   INT_MAX);
      info.major = dpm_parse_uint (in,
				   head->major, sizeof (head->major), 8,
				   INT_MAX);
      info.minor = dpm_parse_uint (in,
				   head->minor, sizeof (head->minor), 8,
				   INT_MAX);

      if (info.name == NULL)
	info.name = dyn_strndup (head->name, sizeof (head->name));
      if (info.target == NULL)
	info.target = dyn_strndup (head->linkname, sizeof (head->linkname));

      info.type = head->linkflag;
      if (info.type == 0)
	info.type = '0';

      dyn_input_advance (in, 512);
      dyn_input_set_mark (in);

      dyn_input_push_limit (in, info.size);

      if (info.type == 'L')
	{
	  dyn_input_advance (in, info.size);
	  info.name = dyn_strndup (dyn_input_mark (in), info.size);
	}
      else if (info.type == 'K')
	{
	  dyn_input_advance (in, info.size);
	  info.target = dyn_strndup (dyn_input_mark (in), info.size);
	}
      else
	func (in, &info, data);

      dyn_input_pop_limit (in);
      
      dyn_input_advance (in, ((info.size + 511) & ~511) - info.size);
      dyn_input_set_mark (in);

      if (info.type != 'L')
	{
	  free (info.name);
	  info.name = NULL;
	}
      if (info.type != 'K')
	{
	  free (info.target);
	  info.target = NULL;
	}
    }
}
