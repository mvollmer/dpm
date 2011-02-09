/*
 * Copyright (C) 2011 Marius Vollmer <marius.vollmer@gmail.com>
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

#define _GNU_SOURCE

#include "alg.h"

/* Cand sets
 */

struct dpm_candset_struct {
  int *tags;
  int tag;
};

static void
dpm_candset_unref (dyn_type *type, void *object)
{
  dpm_candset s = object;
  free (s->tags);
}

static int
dpm_candset_equal (void *a, void *b)
{
  return 0;
}

DYN_DEFINE_TYPE (dpm_candset, "candset");

dpm_candset
dpm_candset_new ()
{
  dpm_candset s = dyn_new (dpm_candset);
  s->tags = calloc (dpm_ws_cand_id_limit (), sizeof (int));
  s->tag = 1;
  return s;
}

void
dpm_candset_reset (dpm_candset s)
{
  s->tag++;
  if (s->tag == 0)
    abort ();
}

void
dpm_candset_add (dpm_candset s, dpm_cand c)
{
  s->tags[dpm_cand_id(c)] = s->tag;
}

void
dpm_candset_rem (dpm_candset s, dpm_cand c)
{
  s->tags[dpm_cand_id(c)] = 0;
}

bool
dpm_candset_has (dpm_candset s, dpm_cand c)
{
  return s->tags[dpm_cand_id(c)] == s->tag;
}
