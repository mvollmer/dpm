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

void dpm_candset_reset (dpm_candset s);
void dpm_candset_add (dpm_candset s, dpm_cand c);
void dpm_candset_rem (dpm_candset s, dpm_cand c);
bool dpm_candset_has (dpm_candset s, dpm_cand c);

/* Cand priority queues

   A 'candpq' maintains a priority queue of candidates.  You can set
   priorities for candidates, and the candpq can efficiently give you
   the highest priority item at any time.
*/

DYN_DECLARE_TYPE (dpm_candpq);

dpm_candpq dpm_candpq_new ();

void dpm_candpq_push (dpm_candpq q, dpm_cand c, int prio);
dpm_cand dpm_candpq_pop (dpm_candpq q);
dpm_cand dpm_candpq_peek (dpm_candpq q);

/* Find the shortest path from A to B and print it.
 */
void dpm_alg_print_relation (dpm_cand a, dpm_cand b);

#endif /* !DPM_ALG_H */
