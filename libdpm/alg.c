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

//#define DEBUG

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
  ver_node *candidates;
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

  ws->conflict = NULL;

  dyn_let (cur_ws, ws);
}

dpm_ws
dpm_ws_current ()
{
  return dyn_get (cur_ws);
}

static pkg_info *
get_pkg_info (dpm_ws ws, dpm_package pkg)
{
  return ws->pkg_info + dpm_pkg_id (pkg);
}

static ver_info *
get_ver_info (dpm_ws ws, dpm_version ver)
{
  return ws->ver_info + dpm_ver_id (ver);
}

static void
add_ver (dpm_ws ws, ver_node **ptr, ver_info *info)
{
  ver_node *n = obstack_alloc (&ws->mem, sizeof (ver_node));
  n->info = info;
  n->next = *ptr;
  *ptr = n;
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
#ifdef DEBUG
	      if (v->forbidden_count == 1)
		{
		  dyn_print ("< %r ", dpm_pkg_name (v->package->pkg));
		  show_ver ("", v);
		}
#endif
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
  if (conflict->unselected_count > 1)
    {
      for (ver_node *n = conflict->versions; n; n = n->next)
	{
	  ver_info *v = n->info;
	  if (v->package->selected != v)
	    {
	      v->forbidden_count -= 1;
#ifdef DEBUG
	      if (v->forbidden_count == 0)
		{
		  dyn_print ("> %r ", dpm_pkg_name (v->package->pkg));
		  show_ver ("", v);
		}
#endif
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

static void report (dpm_ws ws, const char *title);

static void
search (dpm_ws ws, pkg_info *first)
{
  if (first == NULL)
    {
      // Found a solution.
      longjmp (ws->search_done, 1);
    }
  else
    {
      int found_some = 0;
      for (ver_node *n = first->candidates; n; n = n->next)
	{
	  ver_info *v = n->info;
	  if (v->forbidden_count == 0)
	    {
	      found_some = 1;

#ifdef DEBUG
	      dyn_print ("%r = ", dpm_pkg_name (first->pkg));
	      show_ver_info (v);
	      dyn_print ("\n");
#endif

	      first->selected = v;
	      update_conflicts (v->conflicts);
	      search (ws, first->next);
	      downdate_conflicts (v->conflicts);
	    }
	}
      first->selected = NULL;
      if (!found_some)
	{
	  dyn_print ("===\nNo candidate for %r\n",
		     dpm_pkg_name (first->pkg));
	  for (ver_node *n = first->candidates; n; n = n->next)
	    {
	      show_ver_info (n->info);
	      dyn_print (" forbidden by\n");
	      for (cfl_node *m = n->info->conflicts; m; m = m->next)
		if (m->info->unselected_count == 1)
		  show_conflict (" ", m->info);
	    }
	  dyn_print ("===\n");
	}
    }
}

int
dpm_ws_add_candidate (dpm_package pkg, dpm_version ver)
{
  dpm_ws ws = dyn_get (cur_ws);
  pkg_info *p = get_pkg_info (ws, pkg);
  ver_info *v;

  if (p->pkg == NULL)
    {
      p->pkg = pkg;
      p->candidates = NULL;
      p->next = NULL;
      *(ws->tailp) = p;
      ws->tailp = &(p->next);
    }

  if (ver && ver != (void *)-1)
    {
      v = get_ver_info (ws, ver);
      if (v->ver)
	return 0;
      v->ver = ver;
    }
  else
    {
      for (ver_node *n = p->candidates; n; n = n->next)
	if (n->info->ver == ver)
	  return 0;
      v = obstack_alloc (&(ws->mem), sizeof (ver_info));
      v->ver = ver;
    }

  v->package = p;
  v->conflicts = NULL;
  v->forbidden_count = 0;

#ifdef DEBUG
  dyn_print (" candidate: ");
  show_ver_info (v);
  dyn_print ("\n");
#endif

  add_ver (ws, &(p->candidates), v);

  return 1;
}

void
dpm_ws_start_conflict ()
{
  dpm_ws ws = dyn_get (cur_ws);

  cfl_info *c = obstack_alloc (&ws->mem, sizeof (cfl_info));
  c->unselected_count = 0;
  c->versions = NULL;

  ws->conflict = c;
  
#ifdef DEBUG
  dyn_print ("--\n");
#endif
}

void
dpm_ws_add_conflict (dpm_package pkg, dpm_version ver)
{
  dpm_ws ws = dyn_get (cur_ws);
  ver_info *v = NULL;

  cfl_info *c = ws->conflict;
  c->unselected_count += 1;

  if (ver && ver != (void *)-1)
    {
      v = get_ver_info (ws, ver);
      if (v->ver == NULL)
	{
	  // This version is not a candidate for any package and thus
          // this conflict set will never be active.  The
	  // "unselected_count" has been increased, and we can just
	  // return here.

#ifdef DEBUG
	  dyn_print ("%r %r (not)\n",
		     dpm_pkg_name (dpm_ver_package (ver)),
		     dpm_ver_version (ver));
#endif
	  return;
	}
    }
  else
    {
      pkg_info *p = get_pkg_info (ws, pkg);
      for (ver_node *n = p->candidates; n; n = n->next)
	if (n->info->ver == ver)
	  {
	    v = n->info;
	    break;
	  }

      if (v == NULL)
	{
#ifdef DEBUG
	  dyn_print ("%r %s (not)\n",
		     dpm_pkg_name (pkg),
		     ver? "<virtual>" : "<null>");
#endif
	  return;
	}
    }

#ifdef DEBUG
  dyn_print ("%r ", dpm_pkg_name (pkg));
  show_ver_info (v);
  dyn_print ("\n");
#endif

  add_ver (ws, &(c->versions), v);
  add_cfl (ws, &(v->conflicts), c);
}

void
dpm_ws_search ()
{
  dpm_ws ws = dyn_get (cur_ws);
  dyn_print ("\nSearch:\n");
  if (setjmp (ws->search_done))
    {
      report (ws, "Solution");
      return;
    }
  search (ws, ws->head);
}

// Simple standard setup.  No attempts at optimizing the set of
// candidates, only one candidate considered, conflict sets don't look
// at versions, no thorough handling of virtual packages.

static void add_version_and_related (dpm_package pkg, int optional);

static void
add_conflicts_for_depends (dpm_package pkg, dpm_version candidate,
			   ss_val deps, int optional)
{
  if (deps == NULL)
    return;

  int n = ss_len (deps);
  for (int i = 0; i < n; i++)
    {
      dpm_relation rel = ss_ref (deps, i);
      int m = ss_len (rel);
      int alternative = (m > 3);

      for (int j = 0; j < m; j += 3)
	add_version_and_related (dpm_rel_package (rel, j),
				 optional || alternative);

      dpm_ws_start_conflict ();
      dpm_ws_add_conflict (pkg, candidate);
      for (int j = 0; j < m; j += 3)
	dpm_ws_add_conflict (dpm_rel_package (rel, j), NULL);
    }
}

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

static void show_relation (ss_val rel);

static void
add_conflicts_for_conflicts (dpm_package pkg, dpm_version candidate,
			     ss_val confs)
{
  if (confs == NULL)
    return;

  int n = ss_len (confs);
  for (int i = 0; i < n; i++)
    {
      dpm_relation rel = ss_ref (confs, i);
      dpm_package target = dpm_rel_package (rel, 0);
      dpm_version target_candidate = dpm_db_candidate (target);

      if (target_candidate
	  && dpm_db_check_versions (dpm_ver_version (target_candidate),
				    dpm_rel_op (rel, 0),
				    dpm_rel_version (rel, 0)))
	{
	  add_version_and_related (target, 1);
      	  dpm_ws_start_conflict ();
	  dpm_ws_add_conflict (pkg, candidate);
	  dpm_ws_add_conflict (target, target_candidate);
	}

      // Also conflict with versions that Provide the target, but only
      // if this Conflicts doesn't specify a version.

      if (dpm_rel_op (rel, 0) == DPM_ANY)
	{
	  ss_val reverse = dpm_db_reverse_relations (target);
	  if (reverse)
	    {
	      for (int i = 0; i < ss_len (reverse); i++)
		{
		  dpm_version ver = ss_ref (reverse, i);
		  dpm_relations rels = dpm_ver_relations (ver);
		  
		  if (ver != candidate
		      && has_target (dpm_rels_provides (rels), target))
		    {
		      add_version_and_related (dpm_ver_package (ver), 1);
		      dpm_ws_start_conflict ();
		      dpm_ws_add_conflict (pkg, candidate);
		      dpm_ws_add_conflict (dpm_ver_package (ver), ver);
		    }
		}
	    }
	}
    }
}

static void
add_version_and_related (dpm_package pkg, int optional)
{
  dpm_ws ws = dyn_get (cur_ws);

  pkg_info *p = get_pkg_info (ws, pkg);
  
  if (p->pkg)
    return;

  if (optional)
    dpm_ws_add_candidate (pkg, NULL);
      
  // dyn_print ("Adding %r %d\n", dpm_pkg_name (pkg), optional);

  dpm_version candidate = dpm_db_candidate (pkg);

  if (candidate)
    {
      if (dpm_ws_add_candidate (pkg, candidate))
	{
	  // For each Pre-Depends or Depends, we add a conflict set with our
	  // candidate and the null candidate of the target of each
	  // alternative.
	  
	  // For each Conflicts, we add a conflict set with our
	  // candidate and the candidate of the target package.
	  
	  dpm_relations rels = dpm_ver_relations (candidate);
	  add_conflicts_for_depends (pkg, candidate,
				     dpm_rels_pre_depends (rels),
				     optional);
	  add_conflicts_for_depends (pkg, candidate,
				     dpm_rels_depends (rels),
				     optional);
	  add_conflicts_for_conflicts (pkg, candidate,
				       dpm_rels_conflicts (rels));
	}
    }
  else
    {
      // Probably a virtual package.  Make it depend on the package
      // versions that provide it, as alternatives.  We also fake a
      // candidate for us.
      
      candidate = (void *)-1;

      int n_providers = 0;
      dpm_package providers[512];
      
      ss_val reverse = dpm_db_reverse_relations (pkg);
      if (reverse)
	{
	  for (int i = 0; i < ss_len(reverse); i++)
	    {
	      dpm_version ver = ss_ref (reverse, i);
	      dpm_package p = dpm_ver_package (ver);
	      dpm_relations rels = dpm_ver_relations (ver);
	      
	      if (has_target (dpm_rels_provides (rels), pkg))
		providers[n_providers++] = p;
	    }
	}

      if (n_providers > 0
	  && dpm_ws_add_candidate (pkg, candidate))
	{
	  for (int i = 0; i < n_providers; i++)
	    add_version_and_related (providers[i],
				     optional || (n_providers > 1));
	  
	  dpm_ws_start_conflict ();
	  dpm_ws_add_conflict (pkg, candidate);
	  for (int i = 0; i < n_providers; i++)
	    dpm_ws_add_conflict (providers[i], NULL);
	}
    }
}

void
dpm_ws_install (dpm_package pkg)
{
  add_version_and_related (pkg, 0);
}

static const char *opname[] = {
  [DPM_ANY] = "any",
  [DPM_EQ] = "=",
  [DPM_LESS] = "<<",
  [DPM_LESSEQ] = "<=",
  [DPM_GREATER] = ">>",
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
report (dpm_ws ws, const char *title)
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
	  else
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

  report (ws, title);
}

