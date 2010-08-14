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

   A workspace stores a list of candidates for each package.  This is
   a subset of the available versions for that package, plus the
   special 'null' candidate that represents the situation when the
   package is not installed.

   A workspace can be manipulated by _selecting_ a candidate for a
   package.  Once a candidate has been selected for every package, a
   workspace can be _realized_.  This will install and remove package
   archives as necessary to make reality follow what the workspace has
   only simulated so far.

   A workspace offers a simplified view of packages and their
   versions.  It resolves virtual packages to their providers, and
   finds the list of candidates that satisfy dependencies and
   conflicts.

   A workspace can also tell you whether or not a candidate can be
   selected without causing any dpendencies and conflicts to be
   violated.  Such a candidate can still be selected, though, and a
   workspace can give a report about the constraints that are
   currently violated.
 */

DYN_DECLARE_TYPE (dpm_ws);

// Installs a new workspace as the current workspace for the current
// dynamic extent.  Initially, the workspace reflects reality.
//
void dpm_ws_create ();
dpm_ws dpm_ws_current ();

void dpm_ws_report (const char *title);
void dpm_ws_realize (int simulate);


// Operations:
// - list candidates of a package
// - list dependency candidates of a candidate
// - list conflict candidates of a candidate
// - select a candidate
// - tell whether a candidate would violate constraints if selected

#if 0
void dpm_ws_foreach_candidate (void (*func) (dpm_candidate cand),
			       dpm_package pkg);

void dpm_ws_foreach_dependency (void (*func) (dpm_dependency dep),
				dpm_candidate cand);
void dpm_ws_foreach_alternative (void (*func) (dpm_candidate alt),
				 dpm_dependency dep);
void dpm_ws_foreach_conflict (void (*func) (dpm_candidate cand),
			      dpm_candidate cand);

bool dpm_ws_select (dpm_candidate cand);
bool dpm_ws_selectable (dpm_candidate cand);
#endif

#endif /* !DPM_ALG_H */
