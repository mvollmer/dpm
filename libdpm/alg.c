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

struct cand_prio {
  dpm_cand cand;
  int prio;
};

struct dpm_candpq_struct {
  struct cand_prio *cp;
  int cp_capacity;
  int n;
  int *pos;
};

static void
dpm_candpq_unref (dyn_type *type, void *object)
{
  dpm_candpq q = object;
  free (q->cp);
  free (q->pos);
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
  q->cp = NULL;
  q->cp_capacity = 0;
  q->n = 0;
  q->pos = dyn_calloc (dpm_ws_cand_id_limit()*sizeof(int));
  return q;
}

static void
dpm_candpq_reheap (dpm_candpq q, int j, struct cand_prio cp)
{
  while (true)
    {
      int i = (j-1)/2;
      if (j == 0 || q->cp[i].prio >= cp.prio)
	break;
      q->cp[j] = q->cp[i];
      q->pos[dpm_cand_id(q->cp[j].cand)] = j+1;
      j = i;
    }
  
  while (true)
    {
      int i = 2*j+1;
      if (i+1 < q->n && q->cp[i+1].prio > q->cp[i].prio)
	i = i+1;
      if (i >= q->n || cp.prio >= q->cp[i].prio)
	break;
      q->cp[j] = q->cp[i];
      q->pos[dpm_cand_id(q->cp[j].cand)] = j+1;
      j = i;
    }

  q->cp[j] = cp;
  q->pos[dpm_cand_id(q->cp[j].cand)] = j+1;
}

void
dpm_candpq_set (dpm_candpq q, dpm_cand cand, int prio)
{
  int j = q->pos[dpm_cand_id(cand)];
  if (j == 0)
    {
      q->n += 1;
      j = q->n;
      q->cp = dyn_mgrow (q->cp, &q->cp_capacity, sizeof (struct cand_prio), q->n);
    }

  struct cand_prio cp;
  cp.prio = prio;
  cp.cand = cand;
  dpm_candpq_reheap (q, j-1, cp);
}

int
dpm_candpq_set_max (dpm_candpq q, dpm_cand cand, int prio)
{
  int j = q->pos[dpm_cand_id(cand)];
  int old_prio = (j > 0? q->cp[j-1].prio : 0);
  if (j == 0 || old_prio < prio)
    {
      dpm_candpq_set (q, cand, prio);
      return prio;
    }
  else
    return old_prio;
}

int
dpm_candpq_get (dpm_candpq q, dpm_cand cand)
{
  int j = q->pos[dpm_cand_id(cand)];
  if (j == 0)
    return 0;
  else
    return q->cp[j-1].prio;
}

bool
dpm_candpq_peek_x (dpm_candpq q, dpm_cand *candp, int *priop)
{
  if (q->n > 0)
    {
      if (candp)
	*candp = q->cp[0].cand;
      if (priop)
	*priop = q->cp[0].prio;
      
      return true;
    }
  else
    return false;
}

bool
dpm_candpq_pop_x (dpm_candpq q, dpm_cand *candp, int *priop)
{
  if (dpm_candpq_peek_x (q, candp, priop))
    {
      q->n -= 1;
      q->pos[dpm_cand_id(q->cp[0].cand)] = 0;
      if (q->n > 0)
	dpm_candpq_reheap (q, 0, q->cp[q->n]);
      return true;
    }
  else
    return false;
}

dpm_cand
dpm_candpq_pop (dpm_candpq q)
{
  dpm_cand c;
  if (dpm_candpq_pop_x (q, &c, NULL))
    return c;
  else
    return NULL;
}

dpm_cand
dpm_candpq_peek (dpm_candpq q)
{
  dpm_cand c;
  if (dpm_candpq_peek_x (q, &c, NULL))
    return c;
  else
    return NULL;
}

/* Shortest paths
 */

void
dpm_alg_print_relation (dpm_cand a, dpm_cand b)
{
  dyn_block
    {
      dpm_candset visited = dpm_candset_new ();
      dpm_candpq queue = dpm_candpq_new ();

      dpm_cand cur = a;
      dpm_candpq_set (queue, a, 0);
      {
      }
    }
}
