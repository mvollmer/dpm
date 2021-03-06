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
#include "inst.h"

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
      dpm_seatset visited = dpm_seatset_new ();
      dpm_seatset changed = dpm_seatset_new ();

      dpm_seat *winner = dyn_calloc(sizeof(dpm_seat)*dpm_ws_seat_id_limit());
      dyn_on_unwind_free (winner);

      bool better_than (dpm_cand a, dpm_cand b)
      {
	return (a == NULL
		|| dpm_cand_version (a) == NULL
		|| (dpm_cand_version (b)
		    && (dpm_db_compare_versions 
			(dpm_ver_version (dpm_cand_version (b)),
			 dpm_ver_version (dpm_cand_version (a)))
			> 0)));
      }

      dpm_cand find_best (dpm_dep d, bool accept_ugly)
      {
	/* The best candidate is the first candidate in the list of
	   alternatives that is selected and satisfied.  If no
	   candidate is selected and satisfied, or if the first such
	   candidate is the ugly candidate and accept_ugly is false,
	   then the best candidate is the candidate with the highest
	   version of the seat of the the first alternative is the
	   best.
	*/

	dpm_seat first_seat = NULL;
	dyn_foreach (a, dpm_dep_alts, d)
	  {
	    if (first_seat == NULL)
	      first_seat = dpm_cand_seat (a);

	    if (dpm_ws_is_selected (a) && dpm_cand_satisfied (a)
		&& (accept_ugly || a != dpm_ws_get_ugly_cand ()))
	      return a;
	  }

	dpm_cand best_cand = NULL;
	dyn_foreach (a, dpm_dep_alts, d)
	  {
	    if (dpm_cand_seat (a) == first_seat)
	      {
		if (better_than (best_cand, a))
		  best_cand = a;
	      }
	  }

	return best_cand;
      }

      void visit (dpm_cand c, dpm_seat p)
      {
	if (c == NULL)
	  return;

	bool accept_ugly = true;

	dpm_seat s = dpm_cand_seat (c);

	if (dpm_ws_selected (s) == c)
	  {
	    if (dpm_seatset_has (visited, s))
	      return;
	  }
	else
	  {
	    if (dpm_seatset_has (changed, s))
	      {
		dyn_print ("Rejecting %{cand} for %{seat}, using %{cand}\n",
                           c, p, dpm_ws_selected (s));
                int count = 0;
		while ((s = winner[dpm_seat_id(s)]) && count < 10)
                  {
                    dyn_print ("  %{cand} %d\n", dpm_ws_selected (s), dpm_seat_id (s));
                    count++;
                  }

		return;
	      }

	    winner[dpm_seat_id(s)] = p;

	    // dyn_print ("Selecting %{cand} for %{cand}\n", c, p? dpm_ws_selected (p) : NULL);

	    dpm_seatset_add (changed, s);
	    dpm_ws_select (c);
	    accept_ugly = false;
	  }
	
	dpm_seatset_add (visited, s);
        
	dyn_foreach (d, dpm_cand_deps, c)
          {
            visit (find_best (d, accept_ugly), s);

            if (!dpm_ws_is_selected (c))
              {
                // dyn_print ("Shit just got real, giving up on %{cand}.\n", c);
                break;
              }
          }
      }

      dpm_cand *initially_selected =
	dyn_calloc (dpm_ws_seat_id_limit()*sizeof(dpm_cand));

      dyn_foreach (s, dpm_ws_seats)
	{
	  dpm_version inst = dpm_stat_version (dpm_db_status (dpm_seat_package (s)));
	  dpm_cand c = (inst
			? dpm_ws_cand (inst)
			: dpm_seat_null_cand (s));
	  initially_selected[dpm_seat_id (s)] = c;
	}

      dyn_on_unwind_free (initially_selected);

      visit (dpm_ws_get_goal_cand (), NULL);

      void unused (dpm_seat s)
      {
        dpm_cand r = initially_selected[dpm_seat_id (s)];
        if (!dpm_ws_is_selected (r))
          {
            // dyn_print ("Reverting %{seat} to %{cand}\n", s, r);
            dpm_ws_select (r);
          }
      }

      res = dpm_alg_cleanup_goal (unused);
    }

  return res;
}

/* Ordering
 */

struct dpm_alg_order_context_struct {
  int *seat_tag;
};

void
dpm_alg_order_done (dpm_alg_order_context ctxt, dpm_seat s)
{
  ctxt->seat_tag[dpm_seat_id (s)] = -1;
}

bool
dpm_alg_order_is_done (dpm_alg_order_context ctxt, dpm_seat s)
{
  return ctxt->seat_tag[dpm_seat_id (s)] == -1;
}

void
dpm_alg_order (void (*visit_comp) (dpm_alg_order_context ctxt,
				   dpm_seat *seats, int n_seats))
{
  struct dpm_alg_order_context_struct ctxt;
  int tag;
  
  dpm_seat stack[20000];  // XXX
  int stack_top;

  int visit (dpm_seat s)
  {
    int s_id = dpm_seat_id (s);

    if (ctxt.seat_tag[s_id] != 0)
      return ctxt.seat_tag[s_id];

    ctxt.seat_tag[s_id] = ++tag;
    int stack_pos = stack_top;
    stack[stack_top++] = s;

    int min_tag = ctxt.seat_tag[s_id];
    dyn_foreach (d, dpm_cand_deps, dpm_ws_selected (s))
      dyn_foreach (a, dpm_dep_alts, d)
        if (dpm_ws_is_selected (a))
	  {
	    int t = visit (dpm_cand_seat (a));
	    if (t > 0 && t < min_tag)
	      min_tag = t;
	  }

    if (min_tag == ctxt.seat_tag[s_id])
      {
	for (int i = stack_pos; i < stack_top; i++)
	  ctxt.seat_tag[dpm_seat_id (stack[i])] = 0;

	visit_comp (&ctxt, stack + stack_pos, stack_top - stack_pos);

	for (int i = stack_pos; i < stack_top; i++)
	  {
	    while (!dpm_alg_order_is_done (&ctxt, stack[i]))
	      visit (stack[i]);
	  }

	stack_top = stack_pos;
      }

    return min_tag;
  }
  
  dyn_block
    {
      tag = 0;
      ctxt.seat_tag = dyn_calloc (dpm_ws_seat_id_limit()*sizeof(int));
      dyn_on_unwind_free (ctxt.seat_tag);

      stack_top = 0;

      dyn_foreach (s, dpm_ws_seats)
        if (dpm_seat_is_relevant (s))
          visit (s);
    }
}

/* Three valued logic, with true, false, and unknown.

   Truth tables:

   OR T F U     AND T F U
    T T T T       T T F U
    F T F U       F F F F
    U T U U       U U F U
 */

typedef enum { T, F, U } l3;
l3 l3_or_tab[3][3] =  { { T, T, T }, { T, F, U }, { T, U, U } };
l3 l3_and_tab[3][3] = { { T, F, U }, { F, F, F }, { U, F, U } };

l3 l3_or (l3 a, l3 b)
{
  return l3_or_tab[a][b];
}

l3 l3_and (l3 a, l3 b)
{
  return l3_and_tab[a][b];
}

bool
dpm_alg_cleanup_goal (void (*unused) (dpm_seat s))
{
  struct {
    bool ok : 1;
    bool needed: 1;
  } *seat_info;

  bool goal_ok;

  dyn_block
    {
      seat_info = dyn_calloc (dpm_ws_seat_id_limit()*sizeof(*seat_info));
      dyn_on_unwind_free (seat_info);

      void visit (dpm_alg_order_context ctxt,
		  dpm_seat *seats, int n_seats)
      {
	l3 seat_ok (dpm_seat s)
	{
	  if (!dpm_alg_order_is_done (ctxt, s))
	    return U;
	  else if (seat_info[dpm_seat_id (s)].ok)
	    return T;
	  else
	    return F;
	}

	bool some_done = false;

	for (int i = 0; i < n_seats; i++)
	  {
	    l3 deps_ok = T;
	    dyn_foreach (d, dpm_cand_deps, dpm_ws_selected (seats[i]))
	      {
		l3 alts_ok = F;
		dyn_foreach (a, dpm_dep_alts, d)
		  {
		    if (dpm_ws_is_selected (a))
		      {
			alts_ok = l3_or (alts_ok, seat_ok (dpm_cand_seat (a)));
			if (alts_ok == T)
			  break;
		      }
		  }

		deps_ok = l3_and (deps_ok, alts_ok);
		if (deps_ok == F)
		  break;
	      }

	    if (deps_ok != U)
	      {
		seat_info[dpm_seat_id (seats[i])].ok = (deps_ok == T);
		dpm_alg_order_done (ctxt, seats[i]);
		some_done = true;
	      }
	  }

	if (!some_done)
	  {
	    for (int i = 0; i < n_seats; i++)
	      {
		seat_info[dpm_seat_id (seats[i])].ok = true;
		dpm_alg_order_done (ctxt, seats[i]);
	      }
	  }
      }

      dpm_seat_set_relevant (dpm_cand_seat (dpm_ws_get_goal_cand ()), true);
      dpm_alg_order (visit);

      goal_ok =
	seat_info[dpm_seat_id (dpm_cand_seat (dpm_ws_get_goal_cand ()))].ok;

      /* Walk the depenency tree from the goal, following only
	 'ok' seats, and sweep away what we don't need.
      */
      
      void visit_needed (dpm_cand c)
      {
	if (!dpm_ws_is_selected (c))
	  return;
	int id = dpm_seat_id (dpm_cand_seat (c));
	if (seat_info[id].needed)
	  return;
	
	seat_info[id].needed = true;
	dyn_foreach (d, dpm_cand_deps, c)
	  {
	    bool found_ok = false;
	    dyn_foreach (a, dpm_dep_alts, d)
	      if (seat_info[dpm_seat_id (dpm_cand_seat (a))].ok)
		{
		  found_ok = true;
		  visit_needed (a);
		}
	    if (!found_ok)
	      dyn_foreach (a, dpm_dep_alts, d)
		visit_needed (a);
	  }
      }
          
      visit_needed (dpm_ws_get_goal_cand ());

      if (unused)
	dyn_foreach (s, dpm_ws_seats)
	  {
	    int id = dpm_seat_id (s);
	    if (!seat_info[id].needed)
	      unused (s);
	  }
    }

  return goal_ok;
}

static void
print_intradeps (dpm_alg_order_context ctxt,
		 dpm_seat *seats, int n_seats)
{
  for (int i = 0; i < n_seats; i++)
    {
      dpm_cand c = dpm_ws_selected (seats[i]);
      dyn_print ("  %{cand}\n", c);
      dyn_foreach (d, dpm_cand_deps, c)
	dyn_foreach (a, dpm_dep_alts, d)
	  {
	    if (dpm_ws_is_selected (a)
		&& !dpm_alg_order_is_done (ctxt, dpm_cand_seat (a)))
	      {
		if (dpm_dep_is_reversed (d))
		  dyn_print ("    (reversed%s)  [%{cand}]\n",
			     dpm_dep_is_reversed_conflict (d)? " conflict":"", a);
		else
		  dyn_print ("    %{rel}  [%{cand}]\n", dpm_dep_relation (d), a);
	      }
	  }
    }
}

void
dpm_alg_install_component (dpm_alg_order_context ctxt,
			   dpm_seat *seats, int n_seats)
{
  bool some_done = false;

  dpm_package pkgs[n_seats];
  dpm_version vers[n_seats];

  for (int i = 0; i < n_seats; i++)
    {
      pkgs[i] = dpm_seat_package (seats[i]);
      vers[i] = dpm_cand_version (dpm_ws_selected (seats[i]));
      
      if (pkgs[i])
	{
	  dpm_status status = dpm_db_status (pkgs[i]);

	  if (!(vers[i] != dpm_stat_version (status)
		|| dpm_stat_status (status) != DPM_STAT_OK))
	    {
	      dpm_alg_order_done (ctxt, seats[i]);
	      some_done = true;
	    }
	}
    }
  
  if (some_done)
    return;

  if (n_seats > 1)
    {
      print_intradeps (ctxt, seats, n_seats);
      dyn_print ("Installing %d:\n", n_seats);
    }

  for (int i = 0; i < n_seats; i++)
    {
      dpm_alg_order_done (ctxt, seats[i]);

      if (n_seats > 1)
	dyn_print (" ");
      if (vers[i])
	dpm_inst_install (vers[i]);
      else if (pkgs[i])
	dpm_inst_remove (pkgs[i]);
    }
}

void
dpm_alg_print_path (dpm_seat a, dpm_seat b)
{
  dyn_block
    {
      dpm_cand a_cand = dpm_ws_selected (a);
      dpm_cand b_cand = dpm_ws_selected (b);

      dpm_candset visited = dpm_candset_new ();

      bool visit (dpm_cand c)
      {
	if (c == b_cand)
	  return true;

	if (dpm_candset_has (visited, c))
	  return false;

	dpm_candset_add (visited, c);

	dyn_foreach (d, dpm_cand_deps, c)
	  dyn_foreach (a, dpm_dep_alts, d)
	    {
	      if (dpm_ws_is_selected (a) && visit (a))
		{
		  dyn_print ("%{cand}\n", a);
		  return true;
		}
	    }
	return false;
      }

      if (visit (a_cand))
	dyn_print ("%{cand}\n", a_cand);
    }
}

void
dpm_alg_remove_unused ()
{
  dyn_block
    {
      dpm_seatset marked = dpm_seatset_new ();

      void mark (dpm_seat s)
      {
        if (dpm_seatset_has (marked, s))
          return;

        dpm_seatset_add (marked, s);
        dyn_foreach (d, dpm_cand_deps, dpm_ws_selected (s))
	  if (!dpm_dep_is_required_by_target (d))
	    dyn_foreach (a, dpm_dep_alts, d)
	      if (dpm_ws_is_selected (a))
		mark (dpm_cand_seat (a));
      }
       
      void sweep ()
      {
        dyn_foreach (s, dpm_ws_seats)
          if (!dpm_seatset_has (marked, s))
            {
              dpm_ws_select (dpm_seat_null_cand (s));
              dpm_seat_set_relevant (s, true);
            }
      }

      mark (dpm_cand_seat (dpm_ws_get_goal_cand ()));
      dyn_foreach (p, dpm_db_packages)
        {
          dpm_status stat = dpm_db_status (p);
          if (dpm_stat_flags (stat) & DPM_STAT_MANUAL)
            dyn_foreach (s, dpm_ws_package_seats, p)
              mark (s);
        }

      sweep ();
    }
}

void
dpm_alg_execute ()
{
  /* When faced with a strongly connected component, we try to find
     something to do for one or more of its seats.  If that is
     possible, we do it and mark those seats as done and let the
     ordering proceed.  If nothing can be done, we mark all seats as
     done and continue anyway.

     We consider these options in turn when looking for things to do:

     - If a candidate is already fully installed, it is trivially
       done.

     - If a candidate currently has all its deps satisfied, we install it.

     - Then we try to unpack some of the seats, in some order.

     - We again look for candidates that have all deps satisfied.  We
       have some chances to now find some installable candidates,
       since some deps can be satisfied by a unpackaged seat, and when
       a package doesn't have a postinst script, just unpacking it will
       actually mark it is as fully installed.

     - Then we look for candidates that have all deps satisfied that
       are needed to install it, but we allow other seats to be broken
       by this.  We trust that this is only temporary and later
       actions on the now broken seats will make them whole again.

     - If we still haven't found any seat that could be installed, we
       just try every seat in turn.

   */

  void handle_comp (dpm_alg_order_context ctxt,
		    dpm_seat *seats, int n_seats)
  {
    
    bool satisfied_by_cand (dpm_dep d, dpm_cand c)
    {
      return (dpm_cand_is_installed (c)
	      || (dpm_dep_is_satisfied_by_unpacked (d)
		  && dpm_cand_is_unpacked (c)));
    }

    bool satisfied_for_install (dpm_dep d)
    {
      dyn_foreach (a, dpm_dep_alts, d)
	if (satisfied_by_cand (d, a))
	  return true;
      
      return false;
    }

    bool satisfied_for_install_unpack_is_enough (dpm_dep d)
    {
      dyn_foreach (a, dpm_dep_alts, d)
	if (dpm_cand_is_unpacked (a))
	  return true;
      
      return false;
    }

    bool satisfied_for_install_optimistic (dpm_dep d)
    {
      /* At least one alternative must be fully installed, and
	 not-yet-done seats are assumed to be fully installed.
      */
      dyn_foreach (a, dpm_dep_alts, d)
	{
	  if ((dpm_ws_is_selected (a)
	       && !dpm_alg_order_is_done (ctxt, dpm_cand_seat (a)))
	      || satisfied_by_cand (d, a))
	    return true;
	}
      
      return false;
    }

    bool satisfied_for_install_allow_breaks (dpm_dep d)
    {
      return (dpm_dep_is_required_by_target (d)
	      || satisfied_for_install (d));
    }

    bool satisfied_for_unpack (dpm_dep d)
    {
      return (!dpm_dep_must_be_satisfied_for_unpack (d)
	      || satisfied_for_install (d));
    }
    
    bool all_deps (dpm_cand c, bool (*pred) (dpm_dep d))
    {
      dyn_foreach (d, dpm_cand_deps, c)
	if (!pred (d))
	  return false;
      return true;
    }

    bool install_satisfied (bool (*pred) (dpm_dep d))
    {
      bool some_done = false;
      
      for (int i = 0; i < n_seats; i++)
	{
	  dpm_cand c = dpm_ws_selected (seats[i]);
	  
	  if (all_deps (c, pred))
	    {
	      dpm_cand_install (c);
	      dpm_alg_order_done (ctxt, seats[i]);
	      some_done = true;
	    }
	}
      
      return some_done;
    }

    bool install_some ()
    {
      /* Check if some don't need any work at all.
       */
      bool some_done = false;
      for (int i = 0; i < n_seats; i++)
	{
	  dpm_cand c = dpm_ws_selected (seats[i]);
	  
	  if (dpm_cand_is_installed (c))
	    {
	      dpm_alg_order_done (ctxt, seats[i]);
	      some_done = true;
	    }
	}
      
      if (some_done)
	return true;
      
      if (install_satisfied (satisfied_for_install))
	return true;

      if (install_satisfied (satisfied_for_install_allow_breaks))
	{
	  dyn_print ("(That broke some packages)\n");
	  return true;
	}

      return false;
    }

#if 0
    dyn_print ("on");
    for (int i = 0; i < n_seats; i++)
      {
	dyn_print (" %{seat}", seats[i]);
      }
    dyn_print ("\n");
#endif

    if (install_some ())
      return;

    /* Now it gets interesting, we might need to break a cycle.

       But first we check whether all seats could be installed with
       the assumption that all other seats in this component are
       already installed.  If this is not the case, then some package
       outside of this component has failed to be installed earlier,
       and we give up silently.
    */

    for (int i = 0; i < n_seats; i++)
      {
	dpm_cand c = dpm_ws_selected (seats[i]);
	
	if (!all_deps (c, satisfied_for_install_optimistic))
	  {
	    // Give up.
	    for (int i = 0; i < n_seats; i++)
	      dpm_alg_order_done (ctxt, seats[i]);
	    return;
	  }
      }
    
    /* Unpack as many as we can.
     */

    bool some_are_unpacked = false;
    bool made_some_progress;

    do
      {
	made_some_progress = false;
	for (int i = 0; i < n_seats; i++)
	  {
	    dpm_cand c = dpm_ws_selected (seats[i]);
	    
	    if (!dpm_cand_is_unpacked (c))
	      {
		if (all_deps (c, satisfied_for_unpack)
		    && dpm_cand_unpack (c))
		  made_some_progress = true;
	      }
	    else
	      some_are_unpacked = true;
	  }
      }
    while (made_some_progress);

    if (some_are_unpacked)
      {
	if (install_some ())
	  return;
	
	/* Try them one by one.
	 */

	for (int i = 0; i < n_seats; i++)
	  {
	    dpm_cand c = dpm_ws_selected (seats[i]);
	    
	    if (all_deps (c, satisfied_for_install_unpack_is_enough)
		&& dpm_cand_install (c))
	      {
		dpm_alg_order_done (ctxt, seats[i]);
		return;
	      }
	  }
      }

    /* Didn't find any way to make progress.  Give up on this
       component.
    */
    dyn_print ("Unbreakable cycle:\n");
    print_intradeps (ctxt, seats, n_seats);
    for (int i = 0; i < n_seats; i++)
      dpm_alg_order_done (ctxt, seats[i]);
  }

  dpm_alg_order (handle_comp);
}
