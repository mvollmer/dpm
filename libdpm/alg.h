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

   For each interesting package, the workspace tracks the following
   information:

   - A ordered list of _potential_ installation candidates.  This is a
     subset of the available versions for the package; it excludes
     those versions that are not 'interesting'.  It might also
     explicitly include the 'null version', which--if
     choosen--signifies the removal of the package.

     The list is sorted from best to worst.

   - A _selected_ candidate.  If set, this is one of the viable
     candidates (see below).  Selecting a candidate will update the
     viable candidates of other packages.  The goal of all algorithms
     is to select a candidate for all interesting packages.

   - A set of _viable_ candidates.  This is the union of all allowed
     candidates of the selected candidates of all packages,
     intersected with the potential candidates of this package.

   For each version, the workspace tracks the following:

   - A set of _allowed_ candidates.  This set includes those potential
     candidates of any package that can be installed together with
     this version.  (Conflicts are expressed by only allowing the null
     version to be installed, for example.)

     Actually, we track the set of _forbidden_ candidates: those that
     can not be installed alongside with this version.  The set of
     forbidden candidates is just the set difference of all potential
     candidates and allowed candidates.  It is usually much smaller
     and easier to compute from dependencies, etc.

   The potential candidates are choosen in a preparation phase and
   don't change afterwards.  The viable candidates change as different
   versions are systematically selected when searching for a solution.

   The only thing you can do with a workspace is to prepare it by
   defining the sets of potential candidates for each interesting
   package and the sets of forbidden candidates for each potential
   candidate, and then letting it search for a set of selected
   candidates that is the _best_solution_.  A solution has exactly one
   selected candidate for each interesting packages, and the best
   solution is XXX.

   Different 'algorithms' work by preparing the workspace differently,
   but all the grunt work is done by exhaustive searching.

   This exhaustive searching is not that bad since usually the
   workspace is prepared such that there are not many possible
   solutions (and the search is smart enough not to waste time with
   non-solutions), or it is prepared in such a way that the best
   solution is easy to find because there aren't many constraints to
   be considered.
 */

DYN_DECLARE_TYPE (dpm_ws);

// Installs a new empty workspace as the current workspace for the
// current dynamic extent.
//
void dpm_ws_create ();

dpm_ws dpm_ws_current ();

void dpm_ws_add_candidate (dpm_package pkg, dpm_version ver);


void dpm_ws_report ();

#endif /* !DPM_ALG_H */
