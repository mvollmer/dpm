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

#define _GNU_SOURCE

#include <stdbool.h>
#include <obstack.h>

#include "ws.h"
#include "alg.h"

#define obstack_chunk_alloc dyn_malloc
#define obstack_chunk_free free

typedef struct dpm_pkg_struct  *dpm_pkg;

struct dpm_ws_struct {
  struct obstack mem;

  int n_pkgs;
  struct dpm_pkg_struct *pkgs;

  int n_vers;
  struct dpm_cand_struct *ver_cands;

  int next_id;
};

static dpm_cand_node
cons_cand (dpm_ws ws, dpm_cand c, dpm_cand_node next)
{
  dpm_cand_node n = obstack_alloc (&ws->mem, sizeof (*n));
  n->next = next;
  n->elt = c;
  return n;
}

static dpm_dep_node
cons_dep (dpm_ws ws, dpm_dep d, dpm_dep_node next)
{
  dpm_dep_node n = obstack_alloc (&ws->mem, sizeof (*n));
  n->next = next;
  n->elt = d;
  return n;
}

struct dpm_cand_struct {
  dpm_cand next;
  dpm_pkg pkg;
  dpm_version ver;
  int id;

  dpm_dep_node deps;
  dpm_dep_node revdeps;
  int n_unsatisfied;
};

struct dpm_pkg_struct {
  dpm_package pkg;
  dpm_cand cands;
  dpm_cand selected;
  dpm_cand_node providers;
  struct dpm_cand_struct null_cand;
};

struct dpm_dep_struct {
  dpm_cand cand;
  dpm_relation rel;
  int n_alts;
  int n_selected;
  dpm_cand alts[0];
};

static dpm_pkg
get_pkg (dpm_ws ws, dpm_package pkg)
{
  return ws->pkgs + dpm_pkg_id(pkg);
}

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

  ws->n_pkgs = dpm_db_package_id_limit ();
  ws->pkgs = obstack_alloc (&ws->mem, ws->n_pkgs*sizeof(struct dpm_pkg_struct));
  memset (ws->pkgs, 0, ws->n_pkgs*sizeof(struct dpm_pkg_struct));

  ws->n_vers = dpm_db_version_id_limit ();
  ws->ver_cands =
    obstack_alloc (&ws->mem, ws->n_vers*sizeof(struct dpm_cand_struct));
  memset (ws->ver_cands, 0, ws->n_vers*sizeof(struct dpm_cand_struct));

  int id = 0;

  dyn_foreach_ (pkg, dpm_db_packages)
    {
      dpm_pkg p = get_pkg (ws, pkg);
      dpm_cand n = &(p->null_cand);
      p->pkg = pkg;
      n->pkg = p;
      n->id = id++;
      p->cands = n;
    }

  ws->next_id = id;

  dyn_let (cur_ws, ws);
}

dpm_ws
dpm_ws_current ()
{
  return dyn_get (cur_ws);
}

/* Adding candidates
 */

dpm_cand
dpm_ws_add_cand (dpm_version ver)
{
  dpm_ws ws = dpm_ws_current ();

  dpm_cand c = ws->ver_cands + dpm_ver_id(ver);
  if (c->pkg == NULL)
    {
      c->pkg = get_pkg (ws, dpm_ver_package (ver));
      c->ver = ver;
      c->next = c->pkg->cands;
      c->pkg->cands = c;
      c->id = ws->next_id++;
    }
  return c;
}

void
dpm_ws_cands_init (dpm_ws_cands *iter, dpm_package pkg)
{
  dpm_ws ws = dpm_ws_current ();
  dpm_pkg p = get_pkg (ws, pkg);
  iter->cur = p->cands;
}

void
dpm_ws_cands_fini (dpm_ws_cands *iter)
{
}

void
dpm_ws_cands_step (dpm_ws_cands *iter)
{
  iter->cur = iter->cur->next;
}

bool
dpm_ws_cands_done (dpm_ws_cands *iter)
{
  return iter->cur == NULL;
}

dpm_cand
dpm_ws_cands_elt (dpm_ws_cands *iter)
{
  return iter->cur;
}

DYN_DECLARE_STRUCT_ITER (dpm_cand, dpm_pkg_cands, dpm_pkg pkg)
{
  dpm_cand cur;
};

void
dpm_pkg_cands_init (dpm_pkg_cands *iter, dpm_pkg p)
{
  iter->cur = p->cands;
}

void
dpm_pkg_cands_fini (dpm_pkg_cands *iter)
{
}

void
dpm_pkg_cands_step (dpm_pkg_cands *iter)
{
  iter->cur = iter->cur->next;
}

bool
dpm_pkg_cands_done (dpm_pkg_cands *iter)
{
  return iter->cur == NULL;
}

dpm_cand
dpm_pkg_cands_elt (dpm_pkg_cands *iter)
{
  return iter->cur;
}

dpm_package
dpm_cand_package (dpm_cand c)
{
  return c->pkg->pkg;
}

dpm_version
dpm_cand_version (dpm_cand c)
{
  return c->ver;
}

int
dpm_cand_id (dpm_cand c)
{
  return c->id;
}

int
dpm_ws_cand_id_limit ()
{
  dpm_ws ws = dpm_ws_current ();
  return ws->next_id;
}

/* Deps
 */

static void
find_providers (dpm_ws ws)
{
  for (int i = 0; i < ws->n_vers; i++)
    {
      dpm_cand c = ws->ver_cands + i;
      if (c->ver)
	{
	  dyn_foreach_ (prv, ss_elts,
			dpm_rels_provides (dpm_ver_relations (c->ver)))
	    {
	      dpm_pkg p = get_pkg (ws, dpm_rel_package (prv, 0));
	      p->providers = cons_cand (ws, c, p->providers);
	    }
	}
    }
}

static bool
satisfies_rel (dpm_cand c, bool conf, int op, ss_val version)
{
  bool res = (c->ver
	      && dpm_db_check_versions (dpm_ver_version (c->ver),
					op,
					version));
  if (conf)
    return !res;
  else
    return res;
}

static bool
provides_rel (dpm_cand c, bool conf, int op, ss_val version)
{
  bool res = c->ver != NULL;

  if (conf)
    return !res;
  else
    return res;
}

static void
compute_deps (dpm_ws ws)
{
  dyn_block
    {
      dpm_candset alt_set = dpm_candset_new ();

      for (int i = 0; i < ws->n_vers; i++)
	{
	  dpm_cand c = ws->ver_cands + i;
	  if (c->ver)
	    {
	      void do_rels (ss_val rels, bool conf)
	      {
		dyn_foreach_ (rel, ss_elts, rels)
		  {
		    int n_alts = 0;
		    obstack_blank (&ws->mem, sizeof (struct dpm_dep_struct));
		    
		    void add_alt (dpm_cand c)
		    {
		      if (!dpm_candset_has (alt_set, c))
			{
			  dpm_candset_add (alt_set, c);
			  obstack_ptr_grow (&ws->mem, c);
			  n_alts++;
			}
		    }

		    dyn_foreach_iter (alt, dpm_db_alternatives, rel)
		      {
			dpm_pkg p = get_pkg (ws, alt.package);
			
			bool all_satisfy = true;
			dyn_foreach_ (pc, dpm_pkg_cands, p)
			  if (satisfies_rel (pc, conf, alt.op, alt.version))
			    add_alt (pc);
			  else
			    all_satisfy = false;
		    
			if (all_satisfy)
			  {
			    // This dependency does not need to be
			    // recorded since it is always satisfied, no
			    // matter with candidate of of
			    // P is selected.
			    //
			    n_alts = -1;
			    break;
			  }
		    
			for (dpm_cand_node n = p->providers; n; n = n->next)
			  if (provides_rel (n->elt, conf, alt.op, alt.version))
			    add_alt (n->elt);
		      }

		    dpm_dep d = obstack_finish (&ws->mem);
		    if (n_alts < 0)
		      obstack_free (&ws->mem, d);
		    else
		      {
			d->cand = c;
			d->rel = rel;
			d->n_selected = 0;
			d->n_alts = n_alts;
			
			c->deps = cons_dep (ws, d, c->deps);
			c->n_unsatisfied++;

			for (int i = 0; i < n_alts; i++)
			  d->alts[i]->revdeps =
			    cons_dep (ws, d, d->alts[i]->revdeps);
		      }
		    
		    dpm_candset_reset (alt_set);
		  }
	      }
	  
	      do_rels (dpm_rels_pre_depends (dpm_ver_relations (c->ver)),
		       false);
	      do_rels (dpm_rels_depends (dpm_ver_relations (c->ver)),
		       false);
	      do_rels (dpm_rels_conflicts (dpm_ver_relations (c->ver)),
		       true);
	      do_rels (dpm_rels_breaks (dpm_ver_relations (c->ver)),
		       true);
	    }
	}
    }
}

void
dpm_cand_deps_init (dpm_cand_deps *iter, dpm_cand c)
{
  iter->n = c->deps;
}

void
dpm_cand_deps_fini (dpm_cand_deps *iter)
{
}

void
dpm_cand_deps_step (dpm_cand_deps *iter)
{
  iter->n = iter->n->next;
}

bool
dpm_cand_deps_done (dpm_cand_deps *iter)
{
  return iter->n == NULL;
}

dpm_dep
dpm_cand_deps_elt (dpm_cand_deps *iter)
{
  return iter->n->elt;
}

void
dpm_dep_alts_init (dpm_dep_alts *iter, dpm_dep d)
{
  iter->d = d;
  iter->i = 0;
}

void
dpm_dep_alts_fini (dpm_dep_alts *iter)
{
}

void
dpm_dep_alts_step (dpm_dep_alts *iter)
{
  iter->i++;
}

bool
dpm_dep_alts_done (dpm_dep_alts *iter)
{
  return iter->i >= iter->d->n_alts;
}

dpm_cand
dpm_dep_alts_elt (dpm_dep_alts *iter)
{
  return iter->d->alts[iter->i];
}

void
dpm_cand_revdeps_init (dpm_cand_revdeps *iter, dpm_cand c)
{
  iter->n = c->revdeps;
}

void
dpm_cand_revdeps_fini (dpm_cand_revdeps *iter)
{
}

void
dpm_cand_revdeps_step (dpm_cand_revdeps *iter)
{
  iter->n = iter->n->next;
}

bool
dpm_cand_revdeps_done (dpm_cand_revdeps *iter)
{
  return iter->n == NULL;
}

dpm_dep
dpm_cand_revdeps_elt (dpm_cand_revdeps *iter)
{
  return iter->n->elt;
}

/* Starting
 */

void
dpm_ws_start ()
{
  dpm_ws ws = dpm_ws_current ();
  
  find_providers (ws);
  compute_deps (ws);
}

/* Selecting
 */

static void
pkg_unselect (dpm_pkg p)
{
  if (p->selected)
    dyn_foreach_ (d, dpm_cand_revdeps, p->selected)
      {
	d->n_selected--;
	if (d->n_selected == 0)
	  d->cand->n_unsatisfied++;
      }
  p->selected = NULL;
}

void
dpm_ws_select (dpm_cand c)
{
  dpm_pkg p = c->pkg;
  
  if (p->selected == c)
    return;

  pkg_unselect (p);
  p->selected = c;
  dyn_foreach_ (d, dpm_cand_revdeps, c)
    {
      if (d->n_selected == 0)
	d->cand->n_unsatisfied--;
      d->n_selected++;
    }
}

void
dpm_ws_unselect (dpm_package pkg)
{
  dpm_ws ws = dpm_ws_current ();
  dpm_pkg p = get_pkg (ws, pkg);
  pkg_unselect (p);
}

dpm_cand
dpm_ws_selected (dpm_package pkg)
{
  dpm_ws ws = dpm_ws_current ();
  dpm_pkg p = get_pkg (ws, pkg);
  return p->selected;
}

bool
dpm_ws_is_selected (dpm_cand cand)
{
  return cand->pkg->selected == cand;
}

bool
dpm_dep_satisfied (dpm_dep d)
{
  return d->n_selected > 0;
}

bool
dpm_cand_satisfied (dpm_cand c)
{
  return c->n_unsatisfied == 0;
}

/* Dumping
 */

void
dpm_cand_print_id (dpm_cand c)
{
  ss_val n = dpm_pkg_name (c->pkg->pkg);
  if (c->ver)
    dyn_print ("%r_%r", n, dpm_ver_version (c->ver));
  else
    dyn_print ("%r_null", n);
}

static void
dump_pkg (dpm_pkg p)
{
  dyn_print ("%r:\n", dpm_pkg_name (p->pkg));
  dyn_foreach_ (c, dpm_pkg_cands, p)
    {
      if (c->ver)
	dyn_print (" %r\n", dpm_ver_version (c->ver));
      else
	dyn_print (" null\n");
      dyn_foreach_ (d, dpm_cand_deps, c)
	{
	  dyn_print ("  >");
	  dyn_foreach_ (a, dpm_dep_alts, d)
	    {
	      dyn_print (" ");
	      dpm_cand_print_id (a);
	    }
	  dyn_print ("\n");
	  dyn_print ("    ");
	  dpm_dump_relation (d->rel);
	  dyn_print ("\n");
	}
      dyn_foreach_ (r, dpm_cand_revdeps, c)
	{
	  dyn_print ("  < ");
	  dpm_cand_print_id (r->cand);
	  dyn_print ("\n");
	}
    }
}

void
dpm_ws_dump ()
{
  dpm_ws ws = dpm_ws_current ();

  for (int i = 0; i < ws->n_pkgs; i++)
    {
      dpm_pkg p = ws->pkgs + i;
      if (p)
	{
	  dump_pkg (p);
	  dyn_print ("\n");
	}
    }
}

void
dpm_ws_dump_pkg (dpm_package p)
{
  dpm_ws ws = dpm_ws_current ();
  dump_pkg (get_pkg (ws, p));
}
