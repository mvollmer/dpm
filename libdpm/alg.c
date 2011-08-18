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
  bool res;

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
		    if (dpm_cand_version (b)
			&& dpm_cand_seat (b) == dpm_cand_seat (a))
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

      bool dep_satisfied_by_ugly (dpm_dep d)
      {
	dyn_foreach (a, dpm_dep_alts, d)
	  if (dpm_ws_is_selected (a)
	      && a != dpm_ws_get_ugly_cand ())
	    return false;
	return true;
      }

      void visit (dpm_cand c)
      {
	if (c == NULL)
	  return;

	if (dpm_seatset_has (touched, dpm_cand_seat (c)))
	  return;

	// dyn_print ("Selecting %{cand}\n", c);

	dpm_seatset_add (touched, dpm_cand_seat (c));
	bool was_selected = dpm_ws_is_selected (c);
	dpm_ws_select (c);

	dyn_foreach (d, dpm_cand_deps, c)
	  if (!dpm_dep_satisfied (d)
	      || (!was_selected && dep_satisfied_by_ugly (d)))
            visit (find_best (d));
      }

      dpm_cand *initially_selected =
	dyn_calloc (dpm_ws_seat_id_limit()*sizeof(dpm_cand));

      dyn_foreach (s, dpm_ws_seats)
	initially_selected[dpm_seat_id (s)] = dpm_ws_selected (s);
      dyn_on_unwind_free (initially_selected);

      visit (dpm_ws_get_goal_cand ());

      void unused (dpm_seat s)
      {
	dpm_ws_select (initially_selected[dpm_seat_id (s)]);
      }

      res = dpm_alg_cleanup_goal (unused);
    }

  return res;
}

/* Executing a plan.

   We leave strongly connected components unbroken for now.
 */

void
dpm_alg_order (void (*visit_comp) (dpm_seat *seats, int n_seats))
{
  int tag;
  int *seat_tag;
  
  dpm_seat stack[20000];  // XXX
  int stack_top;

  int visit (dpm_seat s)
  {
    int s_id = dpm_seat_id (s);

    if (seat_tag[s_id] != 0)
      return seat_tag[s_id];

    seat_tag[s_id] = ++tag;
    int stack_pos = stack_top;
    stack[stack_top++] = s;

    int min_tag = seat_tag[s_id];
    dyn_foreach (d, dpm_cand_deps, dpm_ws_selected (s))
      dyn_foreach (a, dpm_dep_alts, d)
        if (dpm_ws_is_selected (a))
	  {
	    int t = visit (dpm_cand_seat (a));
	    if (t > 0 && t < min_tag)
	      min_tag = t;
	  }

    if (min_tag == seat_tag[s_id])
      {
	for (int i = stack_pos; i < stack_top; i++)
	  seat_tag[dpm_seat_id(stack[i])] = -1;

	visit_comp (stack + stack_pos, stack_top - stack_pos);
	stack_top = stack_pos;
      }

    return min_tag;
  }
  
  dyn_block
    {
      tag = 0;
      seat_tag = dyn_calloc (dpm_ws_seat_id_limit()*sizeof(int));
      dyn_on_unwind_free (seat_tag);

      stack_top = 0;

      visit (dpm_cand_seat (dpm_ws_get_goal_cand ()));
    }
}

bool
dpm_alg_cleanup_goal (void (*unused) (dpm_seat s))
{
  struct {
    bool seen : 1;
    bool ok : 1;
    bool needed: 1;
  } *seat_info;

  bool goal_ok;

  dyn_block
    {
      seat_info = dyn_calloc (dpm_ws_seat_id_limit()*sizeof(*seat_info));
      dyn_on_unwind_free (seat_info);

      void visit (dpm_seat *seats, int n_seats)
      {
        /* If a alt is still 'unseen', it must be a cand of a seat in
           this component, and we ignore it.
        */
	bool ok = true;
	for (int i = 0; i < n_seats && ok; i++)
          dyn_foreach (d, dpm_cand_deps, dpm_ws_selected (seats[i]))
            {
              bool found_internal = false;
              bool found_ok = false;
              dyn_foreach (a, dpm_dep_alts, d)
                if (dpm_ws_is_selected (a))
                  {
                    int id = dpm_seat_id (dpm_cand_seat (a));
                    if (!seat_info[id].seen)
                      {
                        found_internal = true;
                        continue;
                      }
                    if (seat_info[id].ok)
                      {
                        found_ok = true;
                        break;
                    }
                  }
              
              if (!found_ok && !found_internal)
                {
                  ok = false;
                  break;
                }
            }
#if 0        
        dyn_print ("%s:", ok? "OK":"NOK");
        for (int i = 0; i < n_seats; i++)
          dyn_print (" %{seat}", seats[i]);
        dyn_print ("\n");
#endif

        for (int i = 0; i < n_seats; i++)
          {
            int id = dpm_seat_id (seats[i]);
            seat_info[id].seen = true;
            seat_info[id].ok = ok;
          }
      }

      dpm_alg_order (visit);

      goal_ok = seat_info[dpm_seat_id (dpm_cand_seat (dpm_ws_get_goal_cand ()))].ok;

      if (goal_ok && unused)
        {
          /* Walk the depenency tree from the goal, following only
             'ok' seats, and sweep away what we don't need.
          */
          
          void visit (dpm_cand c)
          {
            if (!dpm_ws_is_selected (c))
              return;
            int id = dpm_seat_id (dpm_cand_seat (c));
            if (!seat_info[id].ok)
              return;
            if (seat_info[id].needed)
              return;

            seat_info[id].needed = true;
            dyn_foreach (d, dpm_cand_deps, c)
              dyn_foreach (a, dpm_dep_alts, d)
                visit (a);
          }
          
          visit (dpm_ws_get_goal_cand ());

          dyn_foreach (s, dpm_ws_seats)
            {
              int id = dpm_seat_id (s);
              if (!seat_info[id].needed)
                unused (s);
            }
        }

    }

  return goal_ok;
}
