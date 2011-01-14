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
#include <assert.h>
#include <setjmp.h>

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

typedef struct dpm_cand_node_struct {
  struct dpm_cand_node_struct *next;
  dpm_cand elt;
} *dpm_cand_node;

static dpm_cand_node
cons_cand (dpm_ws ws, dpm_cand c, dpm_cand_node next)
{
  dpm_cand_node n = obstack_alloc (&ws->mem, sizeof (*n));
  n->next = next;
  n->elt = c;
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
  return p;
}

struct dpm_cand_struct {
  dpm_cand next;
  dpm_pkg pkg;
  dpm_version ver;
};

static dpm_cand
cand_new (dpm_ws ws)
{
  dpm_cand c = obstack_alloc (&ws->mem, sizeof (struct dpm_cand_struct));
  c->next = NULL;
  return c;
}

struct dpm_dep_struct {
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

static void
compute_deps (dpm_ws ws)
{
  find_providers (ws);
}

/* Cfls
 */

void
dpm_ws_compute_deps_and_cfls ()
{
  dpm_ws ws = dpm_ws_current ();
  compute_deps (ws);
}
