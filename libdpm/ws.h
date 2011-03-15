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

   For each package in the database, a workspace stores a list of some
   of its versions, called the "candidates" of that package.

   There is one special candidate per package, the "null candidate".
   This null candidate is used to represent the absence of that
   package.  For example, installing the null candidate of a package
   means that this package will be removed from the system.

   Of all the candidates of a package, exactly one is "selected".

   For each candidate, the workspace maintains the state of its
   dependencies on other candidates.  Each candidate has a list of
   dependencies ("deps" for short).  Each dep is a list of candidates,
   which are the alternatives ("alts") of that dep.

   Note that deps refer to other concrete candidates, not to things
   like "version 1.2 of foo or later".  For example, say versions 2.0,
   1.3, and 1.0 are the candidates of foo.  Let's denote them as
   "foo_2.0", "foo_1.3", and "foo_1.0", respectively.  Then a field
   like "Depends: foo (>= 1.2)" will be translated into the dep
   (foo_2.0, foo_1.3).  As you can see, for a Depends field, the
   corresponding dep will simply list all candidates that fulfill it.

   This is also done for things like "Depends: foo | bar" and
   Provides.  Any candidate of any package that can satisfy the
   Depends relation is included in the alternatives.

   A relation like "Conflicts: foo (<< 1.2)" is also translated into a
   dep by collecting all candidates of foo that satisfy the relation.
   Continuing the example above, these are foo_2.0, foo_1.3, and
   foo_null, the null candidate of foo.

   A dep is called "satisfied" when one of its alts is selected.

   A cand is called "satisfied" when all of its deps are satisfied.

   A cand is "broken" when it is selected but not satisfied.


   A workspace has some special cands that do not belong to any
   package.

   One is the 'goal' candidate.  It represents a desired goal state
   via its dependencies.  For example, installing a number of packages
   is represented by creating a goal candidate that depends on all of
   those packages.  Likewise, the desired removal a package is
   represented by a goal candidate that conflicts with that package.

   Another one is the 'ugly' candidate.  Using the ugly cand to
   satisfy a dep has higher cost than other cands and this makes it
   useful to represent optional dependencies.  For example a
   Recommends relation is represented by a dep with two alternatives:
   the recommended package and the ugly cand.
*/

DYN_DECLARE_TYPE (dpm_ws);

/* Workspace creation
 */

void dpm_ws_create ();
dpm_ws dpm_ws_current ();

void dpm_ws_dump ();
void dpm_ws_dump_pkg (dpm_package p);

/* Adding candidates to the current workspace.
*/

typedef struct dpm_cand_struct *dpm_cand;

dpm_cand dpm_ws_add_cand (dpm_version ver);
dpm_cand dpm_ws_add_cand_and_deps (dpm_version ver);

DYN_DECLARE_STRUCT_ITER (dpm_cand, dpm_ws_cands, dpm_package pkg)
{
  dpm_cand cur;
};

dpm_package dpm_cand_package (dpm_cand);
dpm_version dpm_cand_version (dpm_cand);

dpm_cand dpm_ws_cand (dpm_version ver);
dpm_cand dpm_ws_null_cand (dpm_package pkg);

void dpm_ws_start ();

int dpm_ws_cand_id_limit ();
int dpm_cand_id (dpm_cand c);

void dpm_cand_print_id (dpm_cand c);

/* Deps.
 */

typedef struct dpm_dep_struct *dpm_dep;

typedef struct dpm_cand_node_struct {
  struct dpm_cand_node_struct *next;
  dpm_cand elt;
} *dpm_cand_node;

typedef struct dpm_dep_node_struct {
  struct dpm_dep_node_struct *next;
  dpm_dep elt;
} *dpm_dep_node;

DYN_DECLARE_STRUCT_ITER (dpm_dep, dpm_cand_deps, dpm_cand cand)
{
  dpm_dep_node n;
};

DYN_DECLARE_STRUCT_ITER (dpm_dep, dpm_cand_revdeps, dpm_cand cand)
{
  dpm_dep_node n;
};

DYN_DECLARE_STRUCT_ITER (dpm_cand, dpm_dep_alts, dpm_dep dep)
{
  dpm_dep d;
  int i;
};

/* Selecting cands.
 */

void dpm_ws_select (dpm_cand cand);
dpm_cand dpm_ws_selected (dpm_package pkg);

bool dpm_dep_satisfied (dpm_dep d);
bool dpm_cand_satisfied (dpm_cand c);
bool dpm_cand_selected (dpm_cand c);

#endif /* !DPM_WS_H */
