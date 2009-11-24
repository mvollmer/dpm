/*
 * Copyright (C) 2009 Marius Vollmer <marius.vollmer@gmail.com>
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

#include <stdbool.h>
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
typedef struct dep_info dep_info;
typedef struct rpkg_info rpkg_info;
typedef struct rver_info rver_info;

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
  int conflicts_initialized;
  dep_info *dependencies;
  rpkg_info *reverse_dependencies;
};

struct pkg_info {
  pkg_info *next;

  dpm_package pkg;
  dpm_version available_version;
  dpm_version installed_version;

  ver_node *candidates;
  ver_node *providers;
  int providers_initialized;

  int free_count;
  ver_info *selected;
};

struct cfl_info {
  int unselected_count;
  ver_node *versions;
};

struct dep_info {
  dep_info *next;
  dpm_relation dep;
  int n_packages;
  pkg_info **packages;
};

struct rpkg_info {
  rpkg_info *next;
  pkg_info *source_package;
  rver_info *source_versions;
};

struct rver_info {
  rver_info *next;
  dep_info *dep;
  ver_info *ver;
};

struct dpm_ws_struct {
  struct obstack mem;

  const char *target_dist;
  int prefer_remove;
  int prefer_upgrade;
  int available_versions_initialized;

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

  ws->target_dist = NULL;
  ws->prefer_remove = 0;
  ws->prefer_upgrade = 0;
  ws->available_versions_initialized = 0;

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

void
dpm_ws_policy_set_distribution_pin (const char *dist)
{
  dpm_ws ws = dpm_ws_current ();
  ws->target_dist = dist;
}

void
dpm_ws_policy_set_prefer_remove (int prefer_remove)
{
  dpm_ws ws = dpm_ws_current ();
  ws->prefer_remove = prefer_remove;
}

void
dpm_ws_policy_set_prefer_upgrade (int prefer_upgrade)
{
  dpm_ws ws = dpm_ws_current ();
  ws->prefer_upgrade = prefer_upgrade;
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
do_rels (ss_val rels, void (*proc) (dpm_relation rel))
{
  if (rels)
    for (int i = 0; i < ss_len (rels); i++)
      proc (ss_ref (rels, i));
}

// Infos

static pkg_info *get_pkg_info (dpm_ws ws, dpm_package pkg);

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
      p->next = NULL;
      p->candidates = NULL;
      p->providers = NULL;
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

static const char *opname[] = {
  [DPM_ANY] = "any",
  [DPM_EQ] = "=",
  [DPM_LESS] = "<",
  [DPM_LESSEQ] = "<=",
  [DPM_GREATER] = ">",
  [DPM_GREATEREQ] = ">="
};

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

/* Setup

   Package and version infos are set up on demand, when they are
   needed to setup other infos.

   1) Available and installed dpm_version records for package infos.
      This happens first and en-gross since distribution pinning is
      best done by enumerating all versions in a package indices, not
      by finding a given version among all the package indices.

   2) List of candidate version infos for package info.  This needs 1
      of the given package.

   3) List of provider version infos for a package info.  This needs 2
      of all packages that have provider versions since only
      candidates are included in the list of providers.

   4) Dependency infos for a version info.  This needs 3 for all the
      target package infos since virtual packages are replaced with
      their providers.

   5) Conflict sets for a version info.  This needs 4 since conflict
      sets are created from dpendency infos.
*/

static void
setup_package_available_and_installed (dpm_ws ws)
{
  if (ws->available_versions_initialized)
    return;

  if (ws->target_dist)
    {
      bool found_any = false;

      dyn_foreach (dpm_package_index idx,
		   dpm_db_foreach_package_index)
	{
	  dpm_release_index release = dpm_pkgidx_release (idx);
	  if (release && ss_streq (dpm_relidx_dist (release), ws->target_dist))
	    {
	      ss_val versions = dpm_pkgidx_versions (idx);
	      found_any = true;
	      for (int i = 0; i < ss_len (versions); i++)
		{
		  dpm_version ver = ss_ref (versions, i);
		  pkg_info *p = get_pkg_info (ws, dpm_ver_package (ver));
		  
		  if (p->available_version == NULL
		      || dpm_db_compare_versions (dpm_ver_version (ver),
						  dpm_ver_version (p->available_version)) > 0)
		    p->available_version = ver;
		}
	    }
	}

      if (!found_any)
	dyn_error ("No such distribution: %s", ws->target_dist);
    }
  else
    {
      dyn_foreach (dpm_package pkg,
		   dpm_db_foreach_package)
	{
	  pkg_info *p = get_pkg_info (ws, pkg);
	  ss_val versions = dpm_db_available (pkg);
	  if (versions)
	    {
	      for (int i = 0; i < ss_len (versions); i++)
		{
		  dpm_version ver = ss_ref (versions, i);
		  if (p->available_version == NULL
		      || dpm_db_compare_versions (dpm_ver_version (ver),
						  dpm_ver_version (p->available_version)) > 0)
		    p->available_version = ver;
		}
	    }
	}
    }

  dyn_foreach_x ((dpm_package pkg, dpm_version ver),
		 dpm_db_foreach_installed)
    {
      pkg_info *p = get_pkg_info (ws, pkg);
      p->installed_version = ver;
    }

  ws->available_versions_initialized = 1;
}

static ver_info *
add_candidate (dpm_ws ws, pkg_info *p, dpm_version ver, int append)
{
  ver_info *v;

  for (ver_node *n = p->candidates; n; n = n->next)
    if (n->info->ver == ver)
      return n->info;

  if (ver)
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
setup_package_candidates (dpm_ws ws, pkg_info *p)
{
  if (p->candidates)
    return;

  setup_package_available_and_installed (ws);

  if (p->installed_version)
    {
      add_candidate (ws, p, p->installed_version, 0);
      if (p->available_version)
	add_candidate (ws, p, p->available_version, !ws->prefer_upgrade);
      add_candidate (ws, p, NULL, !ws->prefer_remove);
    }
  else
    {
      add_candidate (ws, p, NULL, 0);
      if (p->available_version)
	add_candidate (ws, p, p->available_version, 1);
    }
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
setup_package_providers (dpm_ws ws, pkg_info *p)
{
  if (p->providers_initialized)
    return;

  void provider (dpm_version prov)
  {
    ver_info *v = get_ver_info (ws, prov);
    setup_package_candidates (ws, v->package);

    for (ver_node *pv = p->providers; pv; pv = pv->next)
      if (pv->info == v)
	return;

    ver_node *n;
    for (n = v->package->candidates; n; n = n->next)
      {
	if (n->info == v)
	  {
#ifdef DEBUG
	    dyn_print ("Accepted %r %r for %r\n", 
		       dpm_pkg_name (dpm_ver_package (prov)),
		       dpm_ver_version (prov),
		       dpm_pkg_name (p->pkg));
#endif
	    add_ver (ws, &(p->providers), v);
	    break;
	  }
      }
  }
  do_providers (p->pkg, provider);

  p->providers_initialized = 1;
}

static void
do_targets (dpm_ws ws, dpm_relation rel, int for_conflict,
	    void (*func) (pkg_info *p, int op, ss_val version))
{
  {
    dpm_package target = dpm_rel_package (rel, 0);
    pkg_info *t = get_pkg_info (ws, target);

    setup_package_available_and_installed (ws);
    setup_package_providers (ws, t);

    if (t->available_version == NULL
	&& ss_len (rel) > 3)
      {
	dpm_package pkg = NULL;
	for (ver_node *pv = t->providers; pv; pv = pv->next)
	  if (pkg && pkg != dpm_ver_package (pv->info->ver))
	    {
	      dyn_print ("warning - first alternative is a virtual package: ");
	      show_relation (rel);
	      dyn_print ("\n");
	      for (ver_node *pv = t->providers; pv; pv = pv->next)
		dyn_print (" %r %r\n",
			   dpm_pkg_name (dpm_ver_package (pv->info->ver)),
			   dpm_ver_version (pv->info->ver));
	      break;
	    }
	  else
	    pkg = dpm_ver_package (pv->info->ver);
      }
  }
    
  for (int i = 0; i < ss_len (rel); i += 3)
    {
      dpm_package target = dpm_rel_package (rel, i);
      pkg_info *t = get_pkg_info (ws, target);
      int op = dpm_rel_op (rel, i);

      setup_package_providers (ws, t);

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

static int
satisfies_relation (dpm_ws ws, ver_info *v, dpm_relation rel)
{
  for (int i = 0; i < ss_len (rel); i += 3)
    {
      dpm_package target = dpm_rel_package (rel, i);
      pkg_info *t = get_pkg_info (ws, target);
      int op = dpm_rel_op (rel, i);

      if (v->package == t
	  && satisfies (v, op, dpm_rel_version (rel, i)))
	return 1;

      for (ver_node *p = t->providers; p; p = p->next)
	if (p->info == v)
	  return 1;
    }

  return 0;
}

static void
add_reverse_dependency (dpm_ws ws, ver_info *target, ver_info *source, dep_info *d)
{
  rpkg_info *rp;
  for (rp = target->reverse_dependencies; rp; rp = rp->next)
    if (rp->source_package == source->package)
      break;

  if (rp == NULL)
    {
      rp = obstack_alloc (&(ws->mem), sizeof (rpkg_info));
      rp->next = target->reverse_dependencies;
      target->reverse_dependencies = rp;
      rp->source_package = source->package;
      rp->source_versions = NULL;
    }

  rver_info *rv = obstack_alloc (&(ws->mem), sizeof (rver_info));
  rv->next = rp->source_versions;
  rp->source_versions = rv;
  rv->dep = d;
  rv->ver = source;
}

static void
setup_version_dependencies (dpm_ws ws, ver_info *v)
{
  if (v->dependencies || v->ver == NULL) 
    return;

  void dependency (dpm_relation dep)
  {
    int n_packages = 0;
    pkg_info *packages[200];

    dep_info *d = obstack_alloc (&(ws->mem), sizeof (dep_info));
    d->dep = dep;
    d->n_packages = 0;
    d->packages = NULL;

    void target (pkg_info *p, int op, ss_val version)
    {
      for (int i = 0; i < n_packages; i++)
	if (packages[i] == p)
	  return;
      setup_package_candidates (ws, p);
      for (ver_node *n = p->candidates; n; n = n->next)
	{
	  if (satisfies_relation (ws, n->info, dep))
	    {
	      add_reverse_dependency (ws, n->info, v, d);
	      packages[n_packages++] = p;
	      break;
	    }
	}
    }
    do_targets (ws, dep, 0, target);

    d->n_packages = n_packages;
    d->packages = obstack_copy (&(ws->mem), packages, 
				n_packages * sizeof(pkg_info *));

    d->next = v->dependencies;
    v->dependencies = d;
  }

  dpm_relations rels = dpm_ver_relations (v->ver);
  do_rels (dpm_rels_pre_depends (rels), dependency);
  do_rels (dpm_rels_depends (rels), dependency);
}

static void
start_conflict (dpm_ws ws)
{
  cfl_info *c = obstack_alloc (&ws->mem, sizeof (cfl_info));
  c->unselected_count = 0;
  c->versions = NULL;

  ws->conflict = c;
  
#ifdef DEBUG
  dyn_print ("(--\n");
#endif
}

static void
add_conflict (dpm_ws ws, ver_info *v)
{
  cfl_info *c = ws->conflict;

  for (ver_node *n = c->versions; n; n = n->next)
    if (n->info == v)
      return;

#ifdef DEBUG
  dyn_print ("%r ", dpm_pkg_name (v->package->pkg));
  show_ver_info (v);
  dyn_print ("\n");
#endif

  c->unselected_count += 1;
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

#ifdef DEBUG
  dyn_print ("--)\n");
#endif
}

static void setup_package (dpm_ws ws, pkg_info *p);

/* Dependencies with alternative targets are tricky; it's not obvious
   how to decide which of the alternatives to prefer, and once that
   has been decided, it's not obvious how to express that with
   conflict sets.

   Our rule is as follows:

   - Candidates are only allowed to be installed when they are needed.
     At least one other candidate that has a single-target dependency
     on this candidate must be installed.  If this candidate is the
     preferred choice in a multi-target dependency, then it is also
     enough that the source of that dependency is installed.

     A0
     A1 d B1
     f: A0 B1

     A0
     A1 d B1
     C0
     C1 d B1
     f: A0 C0 B1

     A0
     A1 d B1
     A2 d B1
     f: A0 B1

     A0
     A1 d B1
     A2
     f: A0 B1
     f: A2 B1


   - A candidate that is the preferred choice in multi-target
     dependencies is not allowed to be installed if for each of the
     depedencies another target has been installed already.  In other
     words, it is allowed to be installed when there is at least one
     dependency that is not satisfied yet.

     Thus, we create one conflict set for each possible combination
     with one other candidate from all dependencies, and add the
     candidate to it.

     A depends X or a or b
     B depends X or c or d

     forbidden: X, a, c
                X, a, d
                X, b, c
                X, b, d

   For a dependency with explicit alternatives, the first one is the
   preferred choice.  For a dependency on a virtual package, user
   input is needed to find the preferred one.
*/

static void
setup_version_conflicts (dpm_ws ws, ver_info *v)
{
  if (v->conflicts_initialized || v->ver == NULL)
    return;

  setup_version_dependencies (ws, v);
  
  v->conflicts_initialized = 1;

  for (dep_info *d = v->dependencies; d; d = d->next)
    {
      ver_info *candidates[d->n_packages];
      
      void do_candidates (int i)
      {
	if (i < d->n_packages)
	  {
	    setup_package (ws, d->packages[i]);
	    for (ver_node *c = d->packages[i]->candidates; c; c = c->next)
	      {
		if (!satisfies_relation (ws, c->info, d->dep))
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
	    for (int j = 0; j < d->n_packages; j++)
	      add_conflict (ws, candidates[j]);
	    end_conflict (ws);
	  }
      }
  
#ifdef DEBUG
      log_rel ("DEP", d->dep);
#endif

      if (d->n_packages <= 1 || true)
	do_candidates (0);
      else
	{
	  log_rel ("SKIP", d->dep);
	  for (int i = 0; i < d->n_packages; i++)
	    dyn_print (" %r\n", dpm_pkg_name (d->packages[i]->pkg));
	}
    }

  void conflict (dpm_relation conf)
  {
    void target (pkg_info *p, int op, ss_val version)
    {
      setup_package (ws, p);
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
  do_rels (dpm_rels_conflicts (dpm_ver_relations (v->ver)), conflict);

  /* Make sure that we are only selected when we are needed.
   */
  {
    ver_info *candidates[2000]; // XXX

    void do_candidates (rpkg_info *rp, int n)
    {
      if (rp)
	{
	  pkg_info *p = rp->source_package;
	  for (ver_node *c = p->candidates; c; c = c->next)
	    {
	      ver_info *s = c->info;
	      int found = 0;
	      for (rver_info *rv = rp->source_versions; rv; rv = rv->next)
		if (rv->ver == s)
		  {
		    found = 1;
		    break;
		  }
	      if (!found)
		{
		  candidates[n] = s;
		  do_candidates (rp->next, n + 1);
		}
	    }
	}
      else
	{
	  start_conflict (ws);
	  add_conflict (ws, v);
	  for (int i = 0; i < n; i++)
	    add_conflict (ws, candidates[i]);
	  end_conflict (ws);
	}
    }

#ifdef DEBUG
    dyn_print ("REV %r %r\n", dpm_pkg_name (v->package->pkg), v->ver? dpm_ver_version (v->ver) : NULL);
#endif
    // do_candidates (v->reverse_dependencies, 0);
#ifdef DEBUG
    dyn_print ("VER %r %r\n", dpm_pkg_name (v->package->pkg), v->ver? dpm_ver_version (v->ver) : NULL);
#endif
  }
}

static void
setup_package (dpm_ws ws, pkg_info *p)
{
  setup_package_candidates (ws, p);
  for (ver_node *n = p->candidates; n; n = n->next)
    setup_version_conflicts (ws, n->info);
}

// Search

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

  for (pkg_info *p = ws->head; p; p = p->next)
    {
      if (p->selected == NULL)
	{
	  if (best == NULL
	      || p->free_count < best->free_count)
	    {
	      best = p;
	      if (best->free_count == 0)
		break;
	    }
	}
    }

#ifdef DEBUG
  if (best)
    dyn_print ("Best: %d\n", best->free_count);
#endif

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

int
dpm_ws_search ()
{
  dpm_ws ws = dyn_get (cur_ws);

  for (pkg_info *p = ws->head; p; p = p->next)
    p->selected = NULL;

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
  dyn_print ("Failed.\n");
  return 0;
}

/* Setup interface
 */

void
dpm_ws_mark_install (dpm_package pkg)
{
  dpm_ws ws = dyn_get (cur_ws);
  pkg_info *p = get_pkg_info (ws, pkg);
  setup_package (ws, p);
  
  ver_info *n = add_candidate (ws, p, NULL, 1);
  start_conflict (ws);
  add_conflict (ws, n);
  end_conflict (ws);
}

void
dpm_ws_mark_remove (dpm_package pkg)
{
  dpm_ws ws = dyn_get (cur_ws);
  pkg_info *p = get_pkg_info (ws, pkg);
  setup_package (ws, p);
  
  for (ver_node *n = p->candidates; n; n = n->next)
    {
      if (n->info->ver)
	{
	  start_conflict (ws);
	  add_conflict (ws, n->info);
	  end_conflict (ws);
	}
    }
}

void
dpm_ws_setup_finish ()
{
#ifdef DEBUG
  dpm_ws ws = dyn_get (cur_ws);
  report (ws, "Setup", 1);
#endif
}

static int
is_broken (dpm_ws ws, ver_info *v, dpm_relation rel, int conflict)
{
  int is_satisfied = 0;

  void target (pkg_info *p, int op, ss_val version)
  {
    if (p->selected
	&& (p->selected->ver != v->ver || !conflict)
	&& satisfies (p->selected, op, version))
      is_satisfied = 1;
  }
  do_targets (ws, rel, conflict, target);

  return conflict? is_satisfied : !is_satisfied;
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
	  int announced = 0;

	  void check_broken (ss_val rels, int conflict)
	  {
	    if (rels)
	      {
		int len = ss_len (rels);
		for (int i = 0; i < len; i++)
		  {
		    dpm_relation rel = ss_ref (rels, i);
		    if (is_broken (ws, v, rel, conflict))
		      {
			if (!announced)
			  {
			    dyn_print ("%r %r\n",
				       dpm_pkg_name (dpm_ver_package (ver)),
				       dpm_ver_version (ver));
			    announced = 1;
			  }
			dyn_print (" %s ",
				   (conflict? "conflicts with" : "needs"));
			show_relation (rel);
			dyn_print ("\n");
		      }
		  }
	      }
	  }

	  if (ver)
	    {
	      if (verbose)
		{
		  dyn_print ("%r %r\n",
			     dpm_pkg_name (dpm_ver_package (ver)),
			     dpm_ver_version (ver));
		  announced = 1;
		}
	      dpm_relations rels = dpm_ver_relations (ver);
	      check_broken (dpm_rels_pre_depends (rels), 0);
	      check_broken (dpm_rels_depends (rels), 0);
	      check_broken (dpm_rels_conflicts (rels), 1);
	    }
	  else if (verbose)
	    dyn_print ("%r not installed\n", dpm_pkg_name (p->pkg));
	}
      else if (verbose)
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

dpm_version
dpm_ws_candidate (dpm_package pkg)
{
  dpm_ws ws = dpm_ws_current ();
  setup_package_available_and_installed (ws);
  pkg_info *p = get_pkg_info (ws, pkg);
  return p->available_version;
}

void
dpm_ws_import ()
{
  dpm_ws ws = dyn_get (cur_ws);

  dyn_foreach (dpm_package pkg,
	       dpm_db_foreach_installed_package)
    {
      pkg_info *p = get_pkg_info (ws, pkg);
      setup_package (ws, p);
    }
}

void
dpm_ws_select_installed ()
{
  dpm_ws ws = dyn_get (cur_ws);

  dyn_foreach (dpm_package pkg,
	       dpm_db_foreach_installed_package)
    {
      pkg_info *p = get_pkg_info (ws, pkg);
      setup_package (ws, p);
      if (p->installed_version)
	p->selected = add_candidate (ws, p, p->installed_version, 0);
    }
}

void
dpm_ws_realize (int simulate)
{
  dpm_ws ws = dyn_get (cur_ws);
  int done_something = 0;

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
	      done_something = 1;
	    }
	}
    }
  
  if (!done_something)
    {
      dyn_print ("Nohing to do.\n");
      return;
    }

  if (!simulate && done_something)
    dpm_db_checkpoint ();
  else
    dyn_print ("But not really.\n");
}
