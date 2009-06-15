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

#define _GNU_SOURCE

#include <obstack.h>
#include <assert.h>
#include <setjmp.h>

#include "alg.h"
#include "db.h"
#include "dyn.h"
#include "inst.h"

// #define DEBUG

static void log_rel (const char *msg, dpm_relation rel);

#define obstack_chunk_alloc dyn_malloc
#define obstack_chunk_free free

/* Data structures for a 'sitting links' algorithm.  I hope to upgrade
   to dancing links eventually.
 */

typedef struct pkg_info pkg_info;
typedef struct ver_info ver_info;
typedef struct cfl_info cfl_info;

typedef struct ver_node ver_node;
typedef struct cfl_node cfl_node;

struct ver_node {
  ver_node *next;
  ver_info *info;
};

struct cfl_node {
  cfl_node *next;
  cfl_info *info;
};

struct ver_info {
  dpm_version ver;
  pkg_info *package;
  int forbidden_count;
  cfl_node *conflicts;
};

struct pkg_info {
  dpm_package pkg;
  pkg_info *next;
  ver_node *providers;
  ver_node *candidates;
  int free_count;
  ver_info *selected;
};

struct cfl_info {
  int unselected_count;
  ver_node *versions;
};

struct dpm_ws_struct {
  struct obstack mem;
  
  int n_packages;
  pkg_info *pkg_info;

  int n_versions;
  ver_info *ver_info;

  pkg_info *head;
  pkg_info **tailp;
  int n_candidates;

  cfl_info *conflict;

  jmp_buf search_done;
};

static void
dpm_ws_unref (dyn_type *type, void *object)
{
  struct dpm_ws_struct *ws = object;
  obstack_free (&ws->mem, NULL);
}

static int
dpm_ws_equal (void *a, void *b)
{
  return 0;
}

DYN_DEFINE_TYPE (dpm_ws, "workspace");

static dyn_var cur_ws[1];

void
dpm_ws_create ()
{
  dpm_ws ws = dyn_new (dpm_ws);

  obstack_init (&ws->mem);

  ws->n_packages = dpm_db_package_count ();
  ws->pkg_info = calloc (ws->n_packages, sizeof(pkg_info));

  ws->n_versions = dpm_db_version_count ();
  ws->ver_info = calloc (ws->n_versions, sizeof(ver_info));

  ws->head = NULL;
  ws->tailp = &(ws->head);
  ws->n_candidates = 0;

  ws->conflict = NULL;

  dyn_let (cur_ws, ws);
}

dpm_ws
dpm_ws_current ()
{
  return dyn_get (cur_ws);
}

/* Some iterators
 */

static int
has_target (ss_val rels, dpm_package pkg)
{
  if (rels)
    {
      int len = ss_len (rels);
      for (int i = 0; i < len; i++)
	{
	  dpm_relation rel = ss_ref (rels, i);
	  for (int j = 0; j < ss_len (rel); j += 3)
	    if (dpm_rel_package (rel, j) == pkg)
	      return 1;
	}
    }
  return 0;
}

static void
do_providers (dpm_package virt, void (*proc) (dpm_version ver))
{
  ss_val reverse = dpm_db_reverse_relations (virt);
  if (reverse)
    {
      for (int i = 0; i < ss_len (reverse); i++)
	{
	  dpm_version ver = ss_ref (reverse, i);
	  dpm_relations rels = dpm_ver_relations (ver);
	  
	  if (has_target (dpm_rels_provides (rels), virt))
	    proc (ver);
	}
    }
}

static void
do_rels (ss_val rels, void (*proc) (dpm_relation rel))
{
  if (rels)
    for (int i = 0; i < ss_len (rels); i++)
      proc (ss_ref (rels, i));
}

static pkg_info *get_pkg_info (dpm_ws ws, dpm_package pkg);

static void
do_targets (dpm_ws ws, dpm_relation rel, int for_conflict,
	    void (*func) (pkg_info *p, int op, ss_val version))
{
  for (int i = 0; i < ss_len (rel); i += 3)
    {
      dpm_package target = dpm_rel_package (rel, i);
      pkg_info *t = get_pkg_info (ws, target);
      int op = dpm_rel_op (rel, i);

      func (t, op, dpm_rel_version (rel, i));
      for (ver_node *p = t->providers; p; p = p->next)
	{
	  /* If this is for a conflict, virtual packages are never the
	     target of relations with a version restriction.
	     Otherwise, we completely ignore operator and version of
	     the original dependency.  (With versioned Povides, we
	     would do some additional filtering here.)
	  */
	  if (!for_conflict || op == DPM_ANY)
	    func (p->info->package, DPM_EQ, dpm_ver_version (p->info->ver));
	}
    }
}

// Infos

static ver_info *
get_ver_info (dpm_ws ws, dpm_version ver)
{
  ver_info *v = ws->ver_info + dpm_ver_id (ver);
  if (v->package == NULL)
    {
      pkg_info *p = get_pkg_info (ws, dpm_ver_package (ver));
      v->package = p;
      v->ver = ver;
    }
  return v;
}

static void
add_ver (dpm_ws ws, ver_node **ptr, ver_info *info)
{
  ver_node *n = obstack_alloc (&ws->mem, sizeof (ver_node));
  n->info = info;
  n->next = *ptr;
  *ptr = n;
}

static pkg_info *
get_pkg_info (dpm_ws ws, dpm_package pkg)
{
  pkg_info *p = ws->pkg_info + dpm_pkg_id (pkg);

  if (p->pkg == NULL)
    {
      p->pkg = pkg;
      p->candidates = NULL;
      p->next = NULL;
      p->providers = NULL;

      // Get providers if there is no available real version for this
      // package.
      if (dpm_db_candidate (pkg) == NULL)
	{
	  void provider (dpm_version prov)
	  {
	    add_ver (ws, &(p->providers), get_ver_info (ws, prov));
	  }
	  do_providers (pkg, provider);
	}
    }

  return p;
}

static void
add_cfl (dpm_ws ws, cfl_node **ptr, cfl_info *info)
{
  cfl_node *n = obstack_alloc (&ws->mem, sizeof (cfl_node));
  n->info = info;
  n->next = *ptr;
  *ptr = n;
}

static void
show_ver_info (ver_info *v)
{
  if (v->ver == NULL)
    dyn_print ("<null>");
  else if (v->ver == (void *)-1)
    dyn_print ("<virtual>");
  else
    dyn_print ("%r", dpm_ver_version (v->ver));
}

static void
show_ver (const char *pfx, ver_info *v)
{
  dyn_print ("%s", pfx);
  show_ver_info (v);
  dyn_print ("\n");
}

static void
show_conflict (const char *pfx, cfl_info *conflict)
{
  dyn_print ("%s ", pfx);
  for (ver_node *n = conflict->versions; n; n = n->next)
    {
      dyn_print ("%r ", dpm_pkg_name (n->info->package->pkg));
      show_ver_info (n->info);
      dyn_print (", ");
    }
  dyn_print ("%d\n", conflict->unselected_count);
}

static void
update_conflict (cfl_info *conflict)
{
  // show_conflict ("+ ", conflict);

  conflict->unselected_count -= 1;
  if (conflict->unselected_count == 1)
    {
      /* Only one candidate remains unselected, forbid it from being
	 selected.
      */
      for (ver_node *n = conflict->versions; n; n = n->next)
	{
	  ver_info *v = n->info;
	  if (v->package->selected != v)
	    {
	      v->forbidden_count += 1;
	      if (v->forbidden_count == 1)
		{
		  v->package->free_count -= 1;
#ifdef DEBUG
		  dyn_print ("< %r ", dpm_pkg_name (v->package->pkg));
		  show_ver ("", v);
#endif
		}
	      break;
	    }
	}
    }
}

static void
update_conflicts (cfl_node *conflicts)
{
  while (conflicts)
    {
      update_conflict (conflicts->info);
      conflicts = conflicts->next;
    }
}

static void
downdate_conflict (cfl_info *conflict)
{
  // show_conflict ("- ", conflict);

  conflict->unselected_count += 1;
  if (conflict->unselected_count == 2)
    {
      for (ver_node *n = conflict->versions; n; n = n->next)
	{
	  ver_info *v = n->info;
	  if (v->package->selected != v)
	    {
	      v->forbidden_count -= 1;
	      if (v->forbidden_count == 0)
		{
		  v->package->free_count += 1;
#ifdef DEBUG
		  dyn_print ("> %r ", dpm_pkg_name (v->package->pkg));
		  show_ver ("", v);
#endif
		}
	      break;
	    }
	}
    }
}

static void
downdate_conflicts (cfl_node *conflicts)
{
  while (conflicts)
    {
      downdate_conflict (conflicts->info);
      conflicts = conflicts->next;
    }
}

static void report (dpm_ws ws, const char *title, int verbose);

static pkg_info *
find_best_branch_point (dpm_ws ws)
{
  pkg_info *best = NULL;
  int best_free_count;

  for (pkg_info *p = ws->head; p; p = p->next)
    {
      if (p->selected == NULL)
	{
	  if (best == NULL || p->free_count < best_free_count)
	    {
	      best = p;
	      best_free_count = p->free_count;
	      if (best_free_count == 0)
		break;
	    }
	}
    }

  return best;
}

static void
search (dpm_ws ws)
{
  pkg_info *p = find_best_branch_point (ws);

  if (p == NULL)
    {
      // Found a solution.
      longjmp (ws->search_done, 1);
    }
  else
    {
      int found_some = 0;

#ifdef DEBUG
      int n_choices = 0, choice = 0;
      for (ver_node *n = p->candidates; n; n = n->next)
	if (n->info->forbidden_count == 0)
	  n_choices++;
#endif

      for (ver_node *n = p->candidates; n; n = n->next)
	{
	  ver_info *v = n->info;
	  if (v->forbidden_count == 0)
	    {
	      found_some = 1;

#ifdef DEBUG
	      dyn_print ("%r = ", dpm_pkg_name (p->pkg));
	      show_ver_info (v);
	      dyn_print (" (%d of %d)\n", choice+1, n_choices);
	      choice++;
#endif

	      p->selected = v;
	      update_conflicts (v->conflicts);
	      search (ws);
	      downdate_conflicts (v->conflicts);
	    }
	}
      p->selected = NULL;
      if (!found_some)
	{
	  dyn_print ("No candidate for %r\n",
		     dpm_pkg_name (p->pkg));
	  for (ver_node *n = p->candidates; n; n = n->next)
	    {
	      show_ver_info (n->info);
	      dyn_print (" forbidden %d times by\n", n->info->forbidden_count);
	      for (cfl_node *m = n->info->conflicts; m; m = m->next)
		if (m->info->unselected_count == 1)
		  show_conflict (" ", m->info);
	    }
	}
    }
}

static ver_info *
add_candidate (dpm_ws ws, pkg_info *p, dpm_version ver, int append)
{
  ver_info *v;

  for (ver_node *n = p->candidates; n; n = n->next)
    if (n->info->ver == ver)
      return n->info;

  if (ver && ver != (void *)-1)
    v = get_ver_info (ws, ver);
  else
    {
      v = obstack_alloc (&(ws->mem), sizeof (ver_info));
      v->package = p;
      v->ver = ver;
    }

  v->forbidden_count = 0;
  v->conflicts = NULL;

#ifdef DEBUG
  dyn_print ("%r: ", dpm_pkg_name (p->pkg));
  show_ver_info (v);
  dyn_print ("\n");
#endif

  if (p->candidates == NULL)
    {
      *(ws->tailp) = p;
      ws->tailp = &(p->next);
    }

  ver_node **np = &(p->candidates);
  if (append)
    while (*np)
      np = &(*np)->next;
  add_ver (ws, np, v);
  p->free_count += 1;

  ws->n_candidates++;

  return v;
}

static void
start_conflict (dpm_ws ws)
{
  cfl_info *c = obstack_alloc (&ws->mem, sizeof (cfl_info));
  c->unselected_count = 0;
  c->versions = NULL;

  ws->conflict = c;
  
#ifdef DEBUG
  dyn_print ("--\n");
#endif
}

static void
add_conflict (dpm_ws ws, ver_info *v)
{
  cfl_info *c = ws->conflict;
  c->unselected_count += 1;

#ifdef DEBUG
  dyn_print ("%r ", dpm_pkg_name (v->package->pkg));
  show_ver_info (v);
  dyn_print ("\n");
#endif

  add_ver (ws, &(c->versions), v);
  add_cfl (ws, &(v->conflicts), c);
}

static void
end_conflict (dpm_ws ws)
{
  if (ws->conflict->unselected_count == 1)
    {
      ver_info *v = ws->conflict->versions->info;
#ifdef DEBUG
      dyn_print ("UNICONF %r\n",
		 dpm_pkg_name (ws->conflict->versions->info->package->pkg));
#endif
      v->forbidden_count += 1;
      if (v->forbidden_count == 1)
	v->package->free_count -= 1;
    }
}

int
dpm_ws_search ()
{
  dpm_ws ws = dyn_get (cur_ws);
#ifdef DEBUG
  dyn_print ("\nSearch:\n");
#endif
  if (setjmp (ws->search_done))
    {
#ifdef DEBUG
      report (ws, "Solution", 1);
#endif
      return 1;
    }
  search (ws);
  return 0;
}

/* Setup for installing a new package.

 */

// Setup the candidates of PKG.  There will be upto three candidates:
// the highest version that is available, the version that is
// currently installed, and the null version.
//
// The candidates are ordered from best to worst, as required by the
// search algorithm.  This order is determined by a global policy
// settings together with some information about the package, such as
// whether the package has been selected manually.
//
// A typical policy would be to treat removal as the best option,
// except for manually selected packages.  For those, another global
// policy setting would determine whether upgrading is better than the
// status quo.
//
// In any case, the candidate order is not determined by the
// relationships that the package appears in.
//
// For now, we use the simple policy that the status quo is best, and
// that upgrading is better than removal.
//
static void
setup_candidates (dpm_ws ws, pkg_info *p)
{
  if (p->candidates)
    return;

  dpm_version installed = dpm_db_installed (p->pkg);
  dpm_version candidate = dpm_db_candidate (p->pkg);
  
  add_candidate (ws, p, installed, 0);
  if (candidate)
    add_candidate (ws, p, candidate, 1);
  add_candidate (ws, p, NULL, 1);
}

// Determine whether V satiesfies the relation (OP VERSION)
//
static int
satisfies (ver_info *v, int op, ss_val version)
{
  return (v->ver != NULL
	  && dpm_db_check_versions (dpm_ver_version (v->ver),
				    op,
				    version));
}

static void
setup_target_candidates (dpm_ws ws, dpm_relation dep, int for_conflict)
{
  void target (pkg_info *p, int op, ss_val version)
  {
    setup_candidates (ws, p);
  }
  do_targets (ws, dep, for_conflict, target);
}

static void
setup_relations_candidates (dpm_ws ws, ver_info *v)
{
  void depends (dpm_relation rel)
  {
    setup_target_candidates (ws, rel, 0);
  }

  void conflicts (dpm_relation rel)
  {
    setup_target_candidates (ws, rel, 1);
  }
      
  dpm_relations rels = dpm_ver_relations (v->ver);
  do_rels (dpm_rels_pre_depends (rels), depends);
  do_rels (dpm_rels_depends (rels), depends);
  do_rels (dpm_rels_conflicts (rels), conflicts);
}

static void
setup_all_candidates (dpm_ws ws)
{
  int old_n_candidates;

  do {
    old_n_candidates = ws->n_candidates;

    for (pkg_info *p = ws->head; p; p = p->next)
      for (ver_node *n = p->candidates; n; n = n->next)
	if (n->info->ver)
	  setup_relations_candidates (ws, n->info);

  } while (ws->n_candidates > old_n_candidates);
}

// Setup the conflict sets for a dependency of V.
//
// We enumerate all possible combinations of all candidates of the
// targets of DEP, and if that combination does not satisfy DEP, we
// create a conflict set for it.
//
static void
setup_depends_conflicts (dpm_ws ws, ver_info *v, dpm_relation dep)
{
  int n_targets = 0;
  struct {
    pkg_info *p;
    int op;
    ss_val version;
  } targets[200];

  void collect_target (pkg_info *p, int op, ss_val version)
  {
    int i = n_targets++;
    targets[i].p = p;
    targets[i].op = op;
    targets[i].version = version;
  }

  do_targets (ws, dep, 0, collect_target);

  ver_info *candidates[n_targets];

  void do_candidates (int i)
  {
    if (i < n_targets)
      {
	for (ver_node *c = targets[i].p->candidates; c; c = c->next)
	  {
	    if (!satisfies (c->info, targets[i].op, targets[i].version))
	      {
		candidates[i] = c->info;
		do_candidates (i+1);
	      }
	  }
      }
    else
      {
	start_conflict (ws);
	add_conflict (ws, v);
	for (int j = 0; j < n_targets; j++)
	  add_conflict (ws, candidates[j]);
	end_conflict (ws);
      }
  }
  
#ifdef DEBUG
  log_rel ("DEP", dep);
#endif

  do_candidates (0);
}

// Setup the conflict sets for a conflict of V.
//
// We create a conflict set for each candidate of the target that
// satisfies CONF.
//
static void
setup_conflicts_conflicts (dpm_ws ws, ver_info *v, dpm_relation conf)
{
  void target (pkg_info *p, int op, ss_val version)
  {
    for (ver_node *c = p->candidates; c; c = c->next)
      {
	if (c->info != v
	    && satisfies (c->info, op, version))
	  {
	    start_conflict (ws);
	    add_conflict (ws, v);
	    add_conflict (ws, c->info);
	    end_conflict (ws);
	  }
      }
  }

  do_targets (ws, conf, 1, target);
}

// Setup conflict sets for a candidate
//
static void
setup_candidate_conflicts (dpm_ws ws, ver_info *v)
{
  if (v->ver)
    {
      void depends (dpm_relation dep)
      {
	setup_depends_conflicts (ws, v, dep);
      }
      
      void conflicts (dpm_relation conf)
      {
	setup_conflicts_conflicts (ws, v, conf);
      }

      dpm_relations rels = dpm_ver_relations (v->ver);
      do_rels (dpm_rels_pre_depends (rels), depends);
      do_rels (dpm_rels_depends (rels), depends);
      do_rels (dpm_rels_conflicts (rels), conflicts);
    }
}

// Setup conflicts for all candidates of all packages
//
static void
setup_all_conflicts (dpm_ws ws)
{
  for (pkg_info *p = ws->head; p; p = p->next)
    for (ver_node *n = p->candidates; n; n = n->next)
      setup_candidate_conflicts (ws, n->info);
}

void
dpm_ws_mark_install (dpm_package pkg)
{
  dpm_ws ws = dyn_get (cur_ws);
  pkg_info *p = get_pkg_info (ws, pkg);
  setup_candidates (ws, p);
  
  ver_info *n = add_candidate (ws, p, NULL, 1);
  start_conflict (ws);
  add_conflict (ws, n);
  end_conflict (ws);
}

void
dpm_ws_setup_finish ()
{
  dpm_ws ws = dyn_get (cur_ws);
  setup_all_candidates (ws);
  setup_all_conflicts (ws);

#ifdef DEBUG
  report (ws, "Setup", 1);
#endif
}

static const char *opname[] = {
  [DPM_ANY] = "any",
  [DPM_EQ] = "=",
  [DPM_LESS] = "<",
  [DPM_LESSEQ] = "<=",
  [DPM_GREATER] = ">",
  [DPM_GREATEREQ] = ">="
};

static void
show_relation (ss_val rel)
{
  for (int i = 0; i < ss_len (rel); i += 3)
    {
      int op = dpm_rel_op (rel, i);
      if (i > 0)
	dyn_print (" | ");
      dyn_print ("%r", dpm_pkg_name (dpm_rel_package (rel, i)));
      if (op != DPM_ANY)
	dyn_print (" (%s %r)", opname[op], dpm_rel_version (rel, i));
    }
}

static void
log_rel (const char *msg, dpm_relation rel)
{
  dyn_print ("%s ", msg);
  show_relation (rel);
  dyn_print ("\n");
}

static void
show_relations (const char *field, ss_val rels)
{
  if (rels)
    {
      dyn_print ("%s: ", field);
      for (int i = 0; i < ss_len (rels); i++)
	{
	  if (i > 0)
	    dyn_print (", ");
	  show_relation (ss_ref (rels, i));
	}
      dyn_print ("\n");
    }
}

static int
is_broken (dpm_ws ws, dpm_relation rel, int conflict)
{
  for (int i = 0; i < ss_len(rel); i += 3)
    {
      dpm_package target = dpm_rel_package (rel, i);
      pkg_info *p = get_pkg_info (ws, target);
      if (p->selected)
	{
	  dpm_version ver = p->selected->ver;
	  if (ver == (void *)-1)
	    return conflict;
	  else if (ver
		   && dpm_db_check_versions (dpm_ver_version (ver),
					     dpm_rel_op (rel, i),
					     dpm_rel_version (rel, i)))
	    return conflict;
	}
    }
  
  return !conflict;
}

static void
check_broken (dpm_ws ws, ss_val rels, int conflict)
{
  // show_relations ("Checking", rels);

  if (rels)
    {
      int len = ss_len (rels);
      for (int i = 0; i < len; i++)
	{
	  dpm_relation rel = ss_ref (rels, i);
	  if (is_broken (ws, rel, conflict))
	    {
	      dyn_print (" %s ",
			 (conflict? "conflicts with" : "needs"));
	      show_relation (rel);
	      dyn_print ("\n");
	    }
	}
    }
}

void
report (dpm_ws ws, const char *title, int verbose)
{
  dyn_print ("\n%s:\n", title);
  for (pkg_info *p = ws->head; p; p = p->next)
    {
      if (p->selected)
	{
	  ver_info *v = p->selected;
	  dpm_version ver = v->ver;

	  if (ver && ver != (void *)-1)
	    {
	      dyn_print ("%r %r\n",
			 dpm_pkg_name (dpm_ver_package (ver)),
			 dpm_ver_version (ver));

	      dpm_relations rels = dpm_ver_relations (ver);
	      check_broken (ws, dpm_rels_pre_depends (rels), 0);
	      check_broken (ws, dpm_rels_depends (rels), 0);
	      check_broken (ws, dpm_rels_conflicts (rels), 1);
	    } 
	  else if (ver == (void *)-1)
	    dyn_print ("%r virtual\n", dpm_pkg_name (p->pkg));
	  else if (verbose)
	    dyn_print ("%r not installed\n", dpm_pkg_name (p->pkg));
	}
      else
	{
	  dyn_print ("%r candidates\n",  dpm_pkg_name (p->pkg));
	  for (ver_node *n = p->candidates; n; n = n->next)
	    {
	      dyn_print (" ");
	      show_ver_info (n->info);
	      dyn_print ("\n");
	    }
	}
    }
}

void
dpm_ws_report (const char *title)
{
  dpm_ws ws = dyn_get (cur_ws);

  report (ws, title, 0);
}

/* Doing it.
 */

void
dpm_ws_import ()
{
  dpm_ws ws = dyn_get (cur_ws);

  void import (dpm_package pkg, void *unused)
  {
    pkg_info *p = get_pkg_info (ws, pkg);
    dpm_version inst = dpm_db_installed (pkg);
    if (inst)
      p->selected = add_candidate (ws, p, inst, 0);
  }

  dpm_db_foreach_package (import, NULL);
}

void
dpm_ws_realize (int simulate)
{
  dpm_ws ws = dyn_get (cur_ws);

  for (pkg_info *p = ws->head; p; p = p->next)
    {
      ver_info *v = p->selected;

      if (v->ver != (dpm_version)-1)
	{
	  dpm_version old = dpm_db_installed (p->pkg);
	  if (v->ver != old)
	    {
	      if (v->ver == NULL)
		dpm_remove (p->pkg);
	      else
		dpm_install (v->ver);
	    }
	}
    }
  
  if (!simulate)
    dpm_db_checkpoint ();
}
