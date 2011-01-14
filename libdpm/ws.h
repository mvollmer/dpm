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

#ifndef DPM_WS_H
#define DPM_WS_H

#include "dyn.h"
#include "store.h"
#include "db.h"

/* Workspaces for playing with the database.
 */

/* Workspaces.

   A workspace is a place where one can play with package versions and
   their relations.

   For each package in the database, a workspace maintains a list of
   its versions, called the "candidates" of that package.  You can
   control which versions of package are among its candidates.

   There is one special candidate per package, the "null candidate".
   This null candidate is used to represent the absence of that
   package.  For example, installing the null candidate of a package
   means that this package will be removed from the system.

   Of all the candidates of a package, one might be "selected".  For
   example, if you use the workspace to plan the installation of new
   packages, the selected candidate will be the one that is currently
   planned to be installed.

   Note that selecting the null candidate is different from not
   selecting any candidate.  Selecting no candidate means that no
   decision has been made yet about this package, while selecting the
   null candidate means that it has been decided to remove this
   package.

   If every package has a selected candidate, the workspace is called
   "complete".

   For each candidate, the workspace maintains the state of its
   relation with other candidates.  It does this in two complementary
   forms: as dependencies ("deps" for short) and as conflicts
   ("cfls").

   These two forms each contain the same information: It is enough to
   look at only one of them to know about all relations.  A workspace
   provides both forms since some algorithms are easier to express
   with one, while others are easier with the other.

   Each candidate has a list of deps.  Each dep is a list of
   candidates, which are the alternatives ("alts") of that dep.

   Note that deps refer to other concrete candidates, not to things
   like "version 1.2 of foo or later".  For example, say versions 2.0,
   1.3, and 1.0 are the candidates of foo.  Let's denote them as
   "foo_2.0", "foo_1.3", and "foo_1.0", respectively.  Then a field
   like "Depends: foo (>= 1.2)" will be translated into the dep
   (foo_2.0, foo_1.3).  As you can see, for a Depends field, the
   corresponding dep will simply list all candidates that fulill it.

   This is also done for things like "Depends: foo | bar" and
   Provides.  Any candidate of any package that can satisfy the
   Depends relation is included in the alternatives.

   Consequently, a dependency that can not be satisfied by any
   candidate has a empty list of alternatives.

   A relation like "Conflict: foo (<< 1.2)" is also translated into a
   dep by collecting all candidates of foo that satisfy the relation.
   Continuing the example above, these are foo_2.0, foo_1.3, and
   foo_null, the null candidate of foo.

   A dep is "satisfied" when one of its alternatives is selected.

   A complete workspace is called "non-broken" when all deps of all
   selected candidates are satisfied.  (Otherwise it is called
   "broken".)

   Thus, to get a complete, non-broken workspace, one has to select
   candidates in such a way that all their deps are satisfied.

   The same information as contained in the deps is expressed a second
   time with conflicts, called "cfls".  A cfl is a set of candidates
   that can't be all selected at the same time, called the "peers" of
   the cfl.

   For example, if package bar has candidates bar_2, bar_1, and
   bar_null, and candidate foo_1 has a dep with alternatives bar_2 and
   bar_1, then there is also a cfl with peers foo_1 and bar_null, with
   the meaning that bar_null can not be selected when foo_1 is, and
   foo_1 can not be selected when bar_null is.

   Thus, cfls are not associated with a specific candidate, like deps
   are.  However, a workspace can efficiently list all cfls that have
   a given candidate as their member.

   A cfl is "violated" when all of its candidates are selected.

   If a workspace is complete and none of its cfls are violated, then
   it is also not broken.

   Thus, a second way to get a complete, non-broken workspace is to
   select candidates such that no cfl is violated.  The workspace can
   tell you efficiently which cfls would become violated if a given
   candidate would be selected.
*/

DYN_DECLARE_TYPE (dpm_ws);

/* Workspace creation
 */

void dpm_ws_create ();
dpm_ws dpm_ws_current ();

void dpm_ws_report (const char *title);

/* Adding candidates to the current workspace.
*/

typedef struct dpm_cand_struct *dpm_cand;

dpm_cand dpm_ws_add_cand (dpm_version ver);
dpm_cand dpm_ws_add_null_cand (dpm_package pkg);

DYN_DECLARE_STRUCT_ITER (dpm_cand, dpm_ws_cands, dpm_package pkg)
{
  dpm_cand cur;
};

dpm_package dpm_cand_package (dpm_cand);
dpm_version dpm_cand_version (dpm_cand);

/* Deps and cfls.
 */

typedef struct dpm_dep_struct *dpm_dep;
typedef struct dpm_cfl_struct *dpm_cfl;

void dpm_ws_compute_deps_and_cfls ();

dpm_cand dpm_ws_cand (dpm_version ver);
dpm_cand dpm_ws_null_cand (dpm_package pkg);

DYN_DECLARE_STRUCT_ITER (dpm_dep, dpm_cand_deps, dpm_cand cand);
DYN_DECLARE_STRUCT_ITER (dpm_cand, dpm_dep_alts, dpm_dep dep);

DYN_DECLARE_STRUCT_ITER (dpm_cfl, dpm_cand_cfls, dpm_cand cand);
DYN_DECLARE_STRUCT_ITER (dpm_cand, dpm_cfl_peers, dpm_cfl cfl);

/* Selecting cands.

   Deps and cfls have to be setup before selecting candidates.
 */

void dpm_ws_select (dpm_cand cand);
void dpm_ws_unselect (dpm_package pkg);
dpm_cand dpm_ws_selected (dpm_package pkg);

bool dpm_ws_is_selected (dpm_cand cand);
bool dpm_ws_is_selectable (dpm_cand cand);

DYN_DECLARE_STRUCT_ITER (dpm_cfl, dpm_ws_violations);

#endif /* !DPM_ALG_H */
