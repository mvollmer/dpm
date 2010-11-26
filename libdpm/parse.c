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

#include "parse.h"

static int
whitespace_p (char c)
{
  return c == ' ' || c == '\t' || c == '\n';
}

void
dpm_parse_comma_fields_init (dpm_parse_comma_fields *iter,
			     dyn_input in)
{
  iter->in = dyn_ref (in);
  dpm_parse_comma_fields_step (iter);
}

void
dpm_parse_comma_fields_fini (dpm_parse_comma_fields *iter)
{
  dyn_unref (iter->in);
}

void
dpm_parse_comma_fields_step (dpm_parse_comma_fields *iter)
{
  dyn_input in = iter->in;

  dyn_input_skip (in, " \t\n");
  if (dyn_input_grow (in, 1) < 1)
    {
      iter->field = NULL;
      return;
    }

  dyn_input_set_mark (in);
  dyn_input_find (in, ",");
      
  iter->field = dyn_input_mark (in);
  iter->len = dyn_input_pos (in) - iter->field;
  while (iter->len > 0 && whitespace_p (iter->field[iter->len-1]))
    iter->len--;

  if (dyn_input_looking_at (in, ","))
    dyn_input_advance (in, 1);
}

bool
dpm_parse_comma_fields_done (dpm_parse_comma_fields *iter)
{
  return iter->field == NULL;
}

bool
dpm_parse_next_relation (dyn_input in)
{
  dyn_input_skip (in, " \t");
  if (dyn_input_looking_at (in, ","))
    {
      dyn_input_advance (in, 1);
      return true;
    }
  else
    return false;
}

void
dpm_parse_relation_alternatives_init (dpm_parse_relation_alternatives *iter,
				      dyn_input in)
{
  iter->in = dyn_ref (in);
  iter->first = true;
  dpm_parse_relation_alternatives_step (iter);
}

void
dpm_parse_relation_alternatives_fini (dpm_parse_relation_alternatives *iter)
{
  dyn_unref (iter->in);
}

void
dpm_parse_relation_alternatives_step (dpm_parse_relation_alternatives *iter)
{
  dyn_input in = iter->in;

  dyn_input_skip (in, " \t\n");

  if (!iter->first)
    {
      if (dyn_input_looking_at (in, "|"))
	{
	  dyn_input_advance (in, 1);
	  dyn_input_skip (in, " \t\n");
	}
      else
	{
	  iter->name = NULL;
	  return;
	}
    }

  dyn_input_set_mark (in);
  dyn_input_find (in, " \t\n,(|");
  iter->name_len = dyn_input_off (in);
  
  if (iter->name_len == 0)
    {
      iter->name = NULL;
      return;
    }

  dyn_input_skip (in, " \t\n");
  if (dyn_input_looking_at (in, "("))
    {
      int op_offset, version_offset;
      
      dyn_input_advance (in, 1);

      dyn_input_skip (in, " \t\n");
      op_offset = dyn_input_off (in);
      dyn_input_skip (in, "<>=");
      iter->op_len = dyn_input_off (in) - op_offset;

      dyn_input_skip (in, " \t\n");
      if (dyn_input_looking_at (in, ")")
	  || dyn_input_looking_at (in, ",")
	  || dyn_input_looking_at (in, "|"))
	dyn_error ("missing version in relation: %I", in);

      version_offset = dyn_input_off (in);
      dyn_input_find (in, " \t\n),|");
      iter->version_len = dyn_input_off (in) - version_offset;
	  
      dyn_input_skip (in, " \t\n");
      if (!dyn_input_looking_at (in, ")"))
	dyn_error ("missing parentheses in relation");
      dyn_input_advance (in, 1);

      const char *mark = dyn_input_mark (in);
      iter->name = mark;
      iter->op = mark + op_offset;
      iter->version = mark + version_offset;
    }
  else
    {
      const char *mark = dyn_input_mark (in);
      iter->name = mark;
      iter->op = NULL;
      iter->op_len = 0;
      iter->version = NULL;
      iter->version_len = 0;
    }

  iter->first = false;
}

bool
dpm_parse_relation_alternatives_done (dpm_parse_relation_alternatives *iter)
{
  return iter->name == 0;
}

static const int max_line_fields = 512;

void
dpm_parse_lines_init (dpm_parse_lines *iter,
		       dyn_input in)
{
  iter->in = dyn_ref (in);
  iter->fields = dyn_malloc (max_line_fields * sizeof (const char *));
  iter->field_lens = dyn_malloc (max_line_fields * sizeof (int));
  dpm_parse_lines_step (iter);
}

void
dpm_parse_lines_fini (dpm_parse_lines *iter)
{
  free (iter->fields);
  free (iter->field_lens);
  dyn_unref (iter->in);
}

void
dpm_parse_lines_step (dpm_parse_lines *iter)
{
  dyn_input in = iter->in;

  iter->n_fields = 0;
  dyn_input_set_mark (in);

  while (1)
    {
      dyn_input_skip (in, " \t");
      if (dyn_input_looking_at (in, "\n"))
	{
	  dyn_input_advance (in, 1);
	  return;
	}
      else if (dyn_input_grow (in, 1) < 1)
	{
	  if (iter->n_fields == 0)
	    iter->n_fields = -1;
	  return;
	}
      else
	{
	  int n = iter->n_fields;
	  if (n == max_line_fields)
	    dyn_error ("too many fields");
	  iter->fields[n] = dyn_input_pos (in);
	  dyn_input_find (in, " \t\n");
	  iter->field_lens[n] = dyn_input_pos (in) - iter->fields[n];
	  iter->n_fields++;
	}
    }
}

bool
dpm_parse_lines_done (dpm_parse_lines *iter)
{
  return iter->n_fields < 0;
}

bool
dpm_parse_looking_at_control (dyn_input in)
{
  dyn_input_skip (in, "\n");
  return (dyn_input_grow (in, 1) > 0);
}

void
dpm_parse_control_fields_init (dpm_parse_control_fields *iter, dyn_input in)
{
  iter->in = dyn_ref (in);
  iter->starting = true;
  dpm_parse_control_fields_step (iter);
}

void
dpm_parse_control_fields_fini (dpm_parse_control_fields *iter)
{
  dyn_unref (iter->in);
}

void
dpm_parse_control_fields_step (dpm_parse_control_fields *iter)
{
  dyn_input in = iter->in;

  dyn_input_set_mark (in);

  while (dyn_input_find (in, ":\n")
	 || dyn_input_pos (in) > dyn_input_mark (in))
    {
      if (dyn_input_pos (in) == dyn_input_mark (in))
	{
	  /* Empty line.  Gobble it up when we haven't seen a field yet.
	   */
	  if (iter->starting)
	    {
	      dyn_input_advance (in, 1);
	      dyn_input_set_mark (in);
	    }
	  else
	    {
	      iter->name = NULL;
	      return;
	    }
	}
      else
	{
	  int value_off;

	  if (!dyn_input_looking_at (in, ":"))
	    dyn_error ("No field name");

	  iter->name_len = dyn_input_pos (in) - dyn_input_mark (in);

	  dyn_input_advance (in, 1);

	  value_off = dyn_input_pos (in) - dyn_input_mark (in);

	  dyn_input_find_after (in, "\n");
	  while (dyn_input_looking_at (in, " ")
		 || dyn_input_looking_at (in, "\t"))
	    dyn_input_find_after (in, "\n");

	  iter->value_len =
	    dyn_input_pos (in) - dyn_input_mark (in) - value_off;

	  iter->name = dyn_input_mark (in);
	  iter->value = iter->name + value_off;

	  while (iter->value_len > 0
		 && whitespace_p (iter->value[0]))
	    {
	      iter->value++;
	      iter->value_len--;
	    }
	  while (iter->value_len > 0
		 && whitespace_p (iter->value[iter->value_len-1]))
	    {
	      iter->value_len--;
	    }

	  iter->starting = false;
	  return;
	}
    }

  iter->name = NULL;
}

bool
dpm_parse_control_fields_done (dpm_parse_control_fields *iter)
{
  return iter->name == NULL;
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
      dyn_error ("value out of range: %ls", str, len);
    }
  
  while (ptr < end && whitespace_p (*ptr))
    ptr++;
  
  if (*ptr && ptr != end)
    dyn_error ("junk at end of number: %ls", str, len);

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
dpm_parse_ar_members_init (dpm_parse_ar_members *iter, dyn_input in)
{
  iter->in = dyn_ref (in);

  dyn_input_must_grow (in, 8);
  if (memcmp (dyn_input_mark (in), "!<arch>\n", 8) != 0)
    dyn_error ("Not a deb file");
  dyn_input_advance (in, 8);

  iter->name = NULL;
  dpm_parse_ar_members_step (iter);
}

void
dpm_parse_ar_members_fini (dpm_parse_ar_members *iter)
{
  if (iter->name)
    {
      free (iter->name);
      dyn_input_pop_limit (iter->in);
      dyn_input_advance (iter->in, iter->size % 2);
    }

  dyn_unref (iter->in);
}

void
dpm_parse_ar_members_step (dpm_parse_ar_members *iter)
{
  dyn_input in = iter->in;

  if (iter->name)
    {
      free (iter->name);
      dyn_input_pop_limit (in);
      dyn_input_advance (in, iter->size % 2);
    }
    
  dyn_input_set_mark (in);

  if (dyn_input_grow (in, sizeof (ar_header)) >= sizeof (ar_header))
    {
      int name_len;
      ar_header *head = (ar_header *)dyn_input_mark (in);
    
      iter->size = dpm_parse_uint (in, head->size, sizeof (head->size), 10, 
				   OFF_T_MAX);

      if (iter->size == 0)
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

	  iter->name = dyn_malloc (name_len + 1);
	  memcpy (iter->name, head->name, name_len);
	  iter->name[name_len] = 0;
	}

      dyn_input_advance (in, sizeof (ar_header));
      dyn_input_set_mark (in);

      dyn_input_push_limit (in, iter->size);
    }
  else
    iter->name = NULL;
}

bool
dpm_parse_ar_members_done (dpm_parse_ar_members *iter)
{
  return iter->name == NULL;
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
dpm_parse_tar_members_init (dpm_parse_tar_members *iter, dyn_input in)
{
  iter->in = dyn_ref (in);

  iter->name = NULL;
  iter->target = NULL;
  dpm_parse_tar_members_step (iter);
}

void
dpm_parse_tar_members_fini (dpm_parse_tar_members *iter)
{
  if (iter->name)
    {
      dyn_input_pop_limit (iter->in);
      dyn_input_advance (iter->in, ((iter->size + 511) & ~511) - iter->size);
    }
  free (iter->name);
  free (iter->target);
  dyn_unref (iter->in);
}

void
dpm_parse_tar_members_step (dpm_parse_tar_members *iter)
{
  dyn_input in = iter->in;

  if (iter->name)
    {
      dyn_input_pop_limit (in);
      dyn_input_advance (in, ((iter->size + 511) & ~511) - iter->size);
      dyn_input_set_mark (in);
  
      free (iter->name);
      iter->name = NULL;
      free (iter->target);
      iter->target = NULL;
    }

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

      iter->size  = dpm_parse_uint (in,
				    head->size, sizeof (head->size), 8,
				    OFF_T_MAX);
      iter->mode  = dpm_parse_uint (in,
				    head->mode, sizeof (head->mode), 8,
				    INT_MAX);
      iter->uid   = dpm_parse_uint (in,
				    head->userid, sizeof (head->userid), 8,
				    INT_MAX);
      iter->gid   = dpm_parse_uint (in,
				    head->groupid, sizeof (head->groupid), 8,
				    INT_MAX);
      iter->mtime = dpm_parse_uint (in,
				    head->mtime, sizeof (head->mtime), 8,
				    INT_MAX);
      iter->major = dpm_parse_uint (in,
				    head->major, sizeof (head->major), 8,
				    INT_MAX);
      iter->minor = dpm_parse_uint (in,
				    head->minor, sizeof (head->minor), 8,
				    INT_MAX);

      if (iter->name == NULL)
	iter->name = dyn_strndup (head->name, sizeof (head->name));
      if (iter->target == NULL)
	iter->target = dyn_strndup (head->linkname, sizeof (head->linkname));

      iter->type = head->linkflag;
      if (iter->type == 0)
	iter->type = '0';

      dyn_input_advance (in, 512);
      dyn_input_set_mark (in);

      if (iter->type == 'L')
	{
	  dyn_input_advance (in, iter->size);
	  iter->name = dyn_strndup (dyn_input_mark (in), iter->size);
	}
      else if (iter->type == 'K')
	{
	  dyn_input_advance (in, iter->size);
	  iter->target = dyn_strndup (dyn_input_mark (in), iter->size);
	}
      else
	{
	  dyn_input_push_limit (in, iter->size);
	  return;
	}
    }

  free (iter->name);
  iter->name = NULL;
}

bool
dpm_parse_tar_members_done (dpm_parse_tar_members *iter)
{
  return iter->name == NULL;
}
