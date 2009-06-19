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

#ifndef DPM_ALG_H
#define DPM_ALG_H

#include "dyn.h"
#include "store.h"
#include "db.h"

/* Algorithms for working with the database.
 */

/* Workspaces.

   A workspace computes and stores information about packages (such as
   which versions of it might be installed without breaking other
   packages) and efficiently tracks how this information would change
   when packages are installed, upgraded, or removed.

   Just as with the database, there is a 'current workspace' that is
   used as an implicit argument for most functions.

   The workspace keeps track of a set of _interesting_ packages.  Each
   interesting package has a set of _candidates_.  A candidate is
   either a available version of its package, or the 'null version'.
   Selecting the null version for a package means that it is not installed.

   To represent relationships between these candidates, the workspace
   also stores a set of _conflicts_.  A conflict is a set of
   candidates, with the meaning that these candidates can not be
   selected at the same time.

   For example, if candidate A depends on candidate B, this fact is
   expressed by adding a conflict set to the workspace that contains A
   and the null version of the package of B.

   After setting all this up, a workspace can be used to find a
   _solution_.  A solution selects exactly one candidate for each
   interesting package, while not violating any of the conflict sets.

   The only thing you can do with a workspace is to prepare it by
   defining the sets of candidates for each interesting package and
   the conflict sets, and then letting it search for a _solution_.

   Different 'algorithms' work by preparing the workspace differently,
   but all the grunt work is done by exhaustive searching.

   This exhaustive searching is not that bad since usually the
   workspace is prepared such that there are not many possible
   solutions (and the search is smart enough not to waste time with
   non-solutions), or it is prepared in such a way that the first
   solution found is also the best.


   The function dpm_ws_add_candidate adds a new candidate to a package
   and thus makes that package 'interesting'.  Packages that haven't
   gotten any candidates added to them are ignored in the search.

   The order of the calls to dpm_ws_add_candidate determines which
   solution will be considered first: earlier candidates are preferred
   over later candidates for the same package, and packages that
   became interesting earlier are preferred over other packages.

   For example, after this sequence of calls:

     add_candidate (PA, VA1);
     add_candidate (PA, VA2);
     add_candidate (PB, VB1);
     add_candidate (PB, VB2);

   it is considered better to install VA1 than VA2, and the
   combination VA1,VB2 is preferred to VA2,VB1 (if VA1,VB1 is not
   possible).

   These preferences are important when a candidate is the null
   version: you generally want to avoid selecting the null version,
   since uninstalling a package is a much bigger change to the users
   system than upgrading a package.
 */

DYN_DECLARE_TYPE (dpm_ws);

// Installs a new empty workspace as the current workspace for the
// current dynamic extent.
//
void dpm_ws_create ();

void dpm_ws_target_dist (const char *dist);

dpm_ws dpm_ws_current ();

void dpm_ws_mark_install (dpm_package pkg);
void dpm_ws_mark_remove (dpm_package pkg);
void dpm_ws_setup_finish ();

int  dpm_ws_search ();
void dpm_ws_report (const char *title);

void dpm_ws_import ();
void dpm_ws_realize (int simulate);

#endif /* !DPM_ALG_H */
