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

#ifndef DPM_ALG_H
#define DPM_ALG_H

#include "dyn.h"
#include "store.h"
#include "db.h"
#include "ws.h"

/* Algorithms and data structures for workspaces
 */

/* Cand sets

   A 'candset' can efficiently maintain a set of candidates.  Adding,
   removing, resetting, and testing membership are all guaranteed to
   be O(1) operations.

   Creating a candset is relatively expensive, but resetting it is
   dirt cheap.  Thus, the idea is that a given candset is reused as
   often as possible.
*/

DYN_DECLARE_TYPE (dpm_candset);

dpm_candset dpm_candset_new ();

void dpm_candset_reset (dpm_candset cs);
void dpm_candset_add (dpm_candset cs, dpm_cand c);
void dpm_candset_rem (dpm_candset cs, dpm_cand c);
bool dpm_candset_has (dpm_candset cs, dpm_cand c);

/* Seat sets

   The same as a candset, but for seats.
*/

DYN_DECLARE_TYPE (dpm_seatset);

dpm_seatset dpm_seatset_new ();

void dpm_seatset_reset (dpm_seatset ss);
void dpm_seatset_add (dpm_seatset ss, dpm_seat s);
void dpm_seatset_rem (dpm_seatset ss, dpm_seat s);
bool dpm_seatset_has (dpm_seatset ss, dpm_seat s);

/* Cand priority queues

   A 'candpq' maintains a priority queue of candidates.  You can set
   priorities for candidates, and the candpq can efficiently give you
   the highest priority item at any time.
*/

DYN_DECLARE_TYPE (dpm_candpq);

dpm_candpq dpm_candpq_new ();

void dpm_candpq_set (dpm_candpq q, dpm_cand c, int prio);
int dpm_candpq_get (dpm_candpq q, dpm_cand c);
int dpm_candpq_set_max (dpm_candpq q, dpm_cand c, int prio);

dpm_cand dpm_candpq_pop (dpm_candpq q);
dpm_cand dpm_candpq_peek (dpm_candpq q);

bool dpm_candpq_pop_x (dpm_candpq q, dpm_cand *candp, int *priop);
bool dpm_candpq_peek_x (dpm_candpq q, dpm_cand *candp, int *priop);

/* Planning and executing operations
 */

/* Plan the installation of the goal candidate in the current
   workspace in a naive way, without any back tracking or SAT solving.
 
   More sophisticated methods might come later.
*/
bool dpm_alg_install_naively ();

/* Call VISIT_COMP with the strongly connected components formed by
   the dependencies of the selected cands, beginning with the one that
   doesn't have any dependencies on other components, and ending with
   the goal candidate.

   The VISIT_COMP function must call dpm_alg_order_done on each seat
   that should be considered done.  The remaining seats will be
   visited again.  Of course, VISIT_COMP must call dpm_alg_order_done
   for at least one seat; otherwise, dpm_alg_order gets into an
   infinite loop.
*/
typedef struct dpm_alg_order_context_struct *dpm_alg_order_context;
void dpm_alg_order_done (dpm_alg_order_context ctxt, dpm_seat s);

void dpm_alg_order (void (*visit_comp) (dpm_alg_order_context ctxt,
					dpm_seat *seats, int n_seats));

void dpm_alg_order_lax (void (*visit_comp) (dpm_alg_order_context ctxt,
					    dpm_seat *seats, int n_seats));

/* Check all direct and indirect dependencies of the goal candidate
   and return true iff all of them are satisfied by the currently
   selected candidates.  Also, if UNUSED is given, it is called for
   all each seat that is not needed to satisfy the goal.
*/
bool dpm_alg_cleanup_goal (void (*unused) (dpm_seat s));

void dpm_alg_install_component (dpm_alg_order_context ctxt,
				dpm_seat *seats, int n_seats);

void dpm_alg_print_path (dpm_seat a, dpm_seat b);

#endif /* !DPM_ALG_H */
