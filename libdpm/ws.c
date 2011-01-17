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

#define obstack_chunk_alloc dyn_malloc
#define obstack_chunk_free free

typedef struct dpm_pkg_struct  *dpm_pkg;

struct dpm_ws_struct {
  struct obstack mem;

  int n_pkgs;
  dpm_pkg *pkgs;

  int n_vers;
  dpm_cand *ver_cands;
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

struct dpm_pkg_struct {
  dpm_package pkg;
  dpm_cand cands;
  dpm_cand null_cand;
  dpm_cand_node providers;
};

static dpm_pkg
pkg_new (dpm_ws ws)
{
  dpm_pkg p = obstack_alloc (&ws->mem, sizeof (struct dpm_pkg_struct));
  p->cands = NULL;
  p->null_cand = NULL;
  p->providers = NULL;
  return p;
}

struct dpm_cand_struct {
  dpm_cand next;
  dpm_pkg pkg;
  dpm_version ver;
  
  dpm_dep_node deps;
};

static dpm_cand
cand_new (dpm_ws ws)
{
  dpm_cand c = obstack_alloc (&ws->mem, sizeof (struct dpm_cand_struct));
  c->next = NULL;
  return c;
}

struct dpm_dep_struct {
  int n_alts;
  dpm_cand alts[0];
};

struct dpm_cfl_struct {
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

  ws->n_pkgs = dpm_db_package_max_id ();
  ws->pkgs = obstack_alloc (&ws->mem, ws->n_pkgs*sizeof(dpm_pkg));
  memset (ws->pkgs, 0, ws->n_pkgs*sizeof(dpm_pkg));

  ws->n_vers = dpm_db_version_max_id ();
  ws->ver_cands = obstack_alloc (&ws->mem, ws->n_vers*sizeof(dpm_cand));
  memset (ws->ver_cands, 0, ws->n_vers*sizeof(dpm_cand));

  dyn_let (cur_ws, ws);
}

dpm_ws
dpm_ws_current ()
{
  return dyn_get (cur_ws);
}

/* Adding candidates
 */

static dpm_pkg
get_pkg (dpm_ws ws, dpm_package pkg)
{
  dpm_pkg p = ws->pkgs[dpm_pkg_id(pkg)];
  if (p == NULL)
    {
      p = pkg_new (ws);
      p->pkg = pkg;
      ws->pkgs[dpm_pkg_id(pkg)] = p;
    }
  return p;
}

static void
cand_add (dpm_pkg p, dpm_cand c)
{
  c->pkg = p;
  c->next = p->cands;
  p->cands = c;
}

dpm_cand
dpm_ws_add_cand (dpm_version ver)
{
  dpm_ws ws = dpm_ws_current ();

  dpm_cand c = ws->ver_cands[dpm_ver_id(ver)];
  if (c == NULL)
    {
      c = cand_new (ws);
      c->ver = ver;
      cand_add (get_pkg (ws, dpm_ver_package (ver)), c);
      ws->ver_cands[dpm_ver_id(ver)] = c;
    }
  return c;
}

dpm_cand
dpm_ws_add_null_cand (dpm_package pkg)
{
  dpm_ws ws = dpm_ws_current ();

  dpm_pkg p = get_pkg (ws, pkg);
  dpm_cand c = p->null_cand;
  if (c == NULL)
    {
      c = cand_new (ws);
      c->ver = NULL;
      cand_add (p, c);
      p->null_cand = c;
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

/* Deps
 */

static void
find_providers (dpm_ws ws)
{
  for (int i = 0; i < ws->n_vers; i++)
    {
      dpm_cand c = ws->ver_cands[i];
      if (c && c->ver)
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
satisfies_dep (dpm_cand c, int op, ss_val version)
{
  return (c->ver
	  && dpm_db_check_versions (dpm_ver_version (c->ver),
				    op,
				    version));
}

static bool
provides_dep (dpm_cand c, int op, ss_val version)
{
  return c->ver;
}

static void
compute_deps (dpm_ws ws)
{
  for (int i = 0; i < ws->n_vers; i++)
    {
      dpm_cand c = ws->ver_cands[i];
      if (c && c->ver)
	{
	  void do_deps (ss_val deps)
	  {
	    dyn_foreach_ (dep, ss_elts, deps)
	      {
		int n_alts = 0;
		obstack_blank (&ws->mem, sizeof (struct dpm_dep_struct));
		
		dyn_foreach_iter (alt, dpm_db_alternatives, dep)
		  {
		    dpm_pkg p = get_pkg (ws, alt.package);
		    dyn_foreach_ (pc, dpm_pkg_cands, p)
		      if (satisfies_dep (pc, alt.op, alt.version))
			{
			  obstack_ptr_grow (&ws->mem, pc);
			  n_alts++;
			}
		    for (dpm_cand_node n = p->providers; n; n = n->next)
		      if (provides_dep (n->elt, alt.op, alt.version))
			{
			  obstack_ptr_grow (&ws->mem, n->elt);
			  n_alts++;
			}
		  }

		dpm_dep d = obstack_finish (&ws->mem);
		d->n_alts = n_alts;
		
		c->deps = cons_dep (ws, d, c->deps);
	      }
	  }
	  
	  do_deps (dpm_rels_pre_depends (dpm_ver_relations (c->ver)));
	  do_deps (dpm_rels_depends (dpm_ver_relations (c->ver)));
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

/* Cfls
 */

static void
compute_cfls ()
{
}

/* Starting
 */

void
dpm_ws_start ()
{
  dpm_ws ws = dpm_ws_current ();

  find_providers (ws);
  compute_deps (ws);
  compute_cfls (ws);
}

/* Dumping
 */

static void
dump_cand_hint (dpm_cand c)
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
	  dyn_print ("  ->");
	  dyn_foreach_ (a, dpm_dep_alts, d)
	    {
	      dyn_print (" ");
	      dump_cand_hint (a);
	    }
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
      dpm_pkg p = ws->pkgs[i];
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
