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

/* Seat sets
 */

struct dpm_seatset_struct {
  int *tags;
  int tag;
};

static void
dpm_seatset_unref (dyn_type *type, void *object)
{
  dpm_seatset s = object;
  free (s->tags);
}

static int
dpm_seatset_equal (void *a, void *b)
{
  return 0;
}

DYN_DEFINE_TYPE (dpm_seatset, "seatset");

dpm_seatset
dpm_seatset_new ()
{
  dpm_seatset s = dyn_new (dpm_seatset);
  s->tags = dyn_calloc (dpm_ws_seat_id_limit()*sizeof(int));
  s->tag = 1;
  return s;
}

void
dpm_seatset_reset (dpm_seatset s)
{
  s->tag++;
  if (s->tag == 0)
    abort ();
}

void
dpm_seatset_add (dpm_seatset s, dpm_seat c)
{
  s->tags[dpm_seat_id(c)] = s->tag;
}

void
dpm_seatset_rem (dpm_seatset s, dpm_seat c)
{
  s->tags[dpm_seat_id(c)] = 0;
}

bool
dpm_seatset_has (dpm_seatset s, dpm_seat c)
{
  return s->tags[dpm_seat_id(c)] == s->tag;
}

/* Cand priority queues
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

/* The super naive install

   We just walk down the dep graph and for each unsatisfied dep, we
   select the candidate with the highest version for the first
   untouched seat.
*/

bool
dpm_alg_install_naively ()
{
  bool failed = false;

  dyn_block
    {
      dpm_seatset touched = dpm_seatset_new ();

      dpm_cand find_best (dpm_dep d)
      {
	dyn_foreach (a, dpm_dep_alts, d)
	  {
	    dpm_seat s = dpm_cand_seat (a);
	    if (!dpm_seatset_has (touched, s))
	      {
		if (dpm_cand_version (a) == NULL)
		  return a;
		ss_val a_ver = dpm_ver_version (dpm_cand_version (a));
		dyn_foreach (b, dpm_dep_alts, d)
		  {
		    if (dpm_cand_version (b))
		      {
			ss_val b_ver = dpm_ver_version (dpm_cand_version (b));
			if (dpm_db_compare_versions (b_ver, a_ver) > 0)
			  {
			    a = b;
			    a_ver = b_ver;
			  }
		      }
		  }
		return a;
	      }
	  }
	return NULL;
      }

      void visit (dpm_cand c)
      {
	if (c == NULL)
	  return;

	if (dpm_seatset_has (touched, dpm_cand_seat (c)))
	  return;

	dyn_print ("Selecting ");
	dpm_cand_print_id (c);
	dyn_print ("\n");

	dpm_seatset_add (touched, dpm_cand_seat (c));
	dpm_ws_select (c, 0);

	dyn_foreach (d, dpm_cand_deps, c)
	  if (!dpm_dep_satisfied (d, 0))
	    {
	      visit (find_best (d));
	      if (!dpm_dep_satisfied (d, 0))
		failed = true;
	    }
      }

      visit (dpm_ws_get_goal_cand ());
    }

  return !failed;
}

/* Executing a plan.
 */

void
dpm_alg_execute ()
{
  dpm_seatset unpack_done = dpm_seatset_new ();
  dpm_seatset setup_queued = dpm_seatset_new ();
  dpm_seatset setup_done = dpm_seatset_new ();

  void do_unpack (dpm_seat s)
  {
    dpm_cand c = dpm_ws_selected (s, 0);
    dpm_version v = dpm_cand_version (c);
    dpm_package p = dpm_seat_package (s);
    dpm_version inst = dpm_db_installed (p);

    if (v != inst)
      {
	if (v)
	  dyn_print ("Unpacking %r %r\n",
		     dpm_pkg_name (dpm_ver_package (v)), dpm_ver_version (v));
	else if (p)
	  dyn_print ("Removing %r\n", dpm_pkg_name (p));
      }
  }

  void do_setup (dpm_seat s)
  {
    dpm_cand c = dpm_ws_selected (s, 0);
    dpm_version v = dpm_cand_version (c);
    
    if (v)
      dyn_print ("Setting up %r %r\n",
		 dpm_pkg_name (dpm_ver_package (v)), dpm_ver_version (v));
  }

  auto void setup (dpm_seat s);

  void unpack (dpm_seat s)
  {
    if (dpm_seatset_has (unpack_done, s))
      return;

    dyn_foreach (d, dpm_cand_deps, dpm_ws_selected (s, 0))
      if (dpm_dep_for_unpack (d))
	{
	  dyn_foreach (a, dpm_dep_alts, d)
	    if (dpm_ws_is_selected (a, 0))
	      {
		dyn_print ("(setting up %r for pre-dep of %r)\n",
			   dpm_pkg_name (dpm_seat_package (dpm_cand_seat (a))),
			   dpm_pkg_name (dpm_seat_package (s)));
		setup (dpm_cand_seat (a));
		break;
	      }
	}

    if (!dpm_seatset_has (unpack_done, s))
      {
	do_unpack (s);
	dpm_seatset_add (unpack_done, s);
      }
  }

  void setup (dpm_seat s)
  {
    if (dpm_seatset_has (setup_done, s))
      return;

    if (dpm_seatset_has (setup_queued, s))
      {
	dyn_print ("(dep cycle broken at %r)\n",
		   dpm_pkg_name (dpm_seat_package (s)));
	return;
      }

    dpm_seatset_add (setup_queued, s);

    dyn_foreach (d, dpm_cand_deps, dpm_ws_selected (s, 0))
      dyn_foreach (a, dpm_dep_alts, d)
        if (dpm_ws_is_selected (a, 0))
	  {
	    if (!dpm_seatset_has (setup_done, dpm_cand_seat (a)))
	      dyn_print ("(setting up %r for dep of %r)\n",
			 dpm_pkg_name (dpm_seat_package (dpm_cand_seat (a))),
			 dpm_pkg_name (dpm_seat_package (s)));
	    setup (dpm_cand_seat (a));
	    break;
	  }

    unpack (s);

    if (!dpm_seatset_has (setup_done, s))
      {
	do_setup (s);
	dpm_seatset_add (setup_done, s);
      }
  }

  dyn_foreach (s, dpm_ws_seats)
    {
      if (dpm_seat_relevant (s) && dpm_seat_package (s) != NULL)
	setup (s);
    }
}
