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
  s->tags = dyn_calloc (dpm_ws_cand_id_limit()*sizeof(int));
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

/* Cand priority queues

   XXX - don't allow candidates to be in the queue more than once.
*/

struct dpm_candpq_struct {
  int *prio;
  dpm_cand *cand;
  int n;
};

static void
dpm_candpq_unref (dyn_type *type, void *object)
{
  dpm_candpq q = object;
  free (q->prio);
  free (q->cand);
}

static int
dpm_candpq_equal (void *a, void *b)
{
  return 0;
}

DYN_DEFINE_TYPE (dpm_candpq, "candpq");

dpm_candpq
dpm_candpq_new ()
{
  dpm_candpq q = dyn_new (dpm_candpq);
  q->prio = dyn_malloc (dpm_ws_cand_id_limit()*sizeof(int));
  q->cand = dyn_malloc (dpm_ws_cand_id_limit()*sizeof(dpm_cand));
  q->n = 0;
  return q;
}

static void
dpm_candpq_reheap (dpm_candpq q, int j, dpm_cand cand, int prio)
{
  while (true)
    {
      int i = (j-1)/2;
      if (j == 0 || q->prio[i] >= prio)
	break;
      q->prio[j] = q->prio[i];
      q->cand[j] = q->cand[i];
      j = i;
    }
  
  while (true)
    {
      int i = 2*j+1;
      if (i+1 < q->n && q->prio[i+1] > q->prio[i])
	i = i+1;
      if (i >= q->n || prio >= q->prio[i])
	break;
      q->prio[j] = q->prio[i];
      q->cand[j] = q->cand[i];
      j = i;
    }

  q->prio[j] = prio;
  q->cand[j] = cand;
}

void
dpm_candpq_push (dpm_candpq q, dpm_cand c, int prio)
{
  q->n += 1;
  dpm_candpq_reheap (q, q->n-1, c, prio);
}

dpm_cand
dpm_candpq_pop (dpm_candpq q)
{
  dpm_cand ret = q->cand[0];
  q->n -= 1;
  if (q->n > 0)
    dpm_candpq_reheap (q, 0, q->cand[q->n], q->prio[q->n]);
  return ret;
}

dpm_cand
dpm_candpq_peek (dpm_candpq q)
{
  if (q->n > 0)
    return q->cand[0];
  else
    return NULL;
}

int
dpm_candpq_peek_prio (dpm_candpq q)
{
  if (q->n > 0)
    return q->prio[0];
  else
    return 0;
}
