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
#include "pol.h"

#define obstack_chunk_alloc dyn_malloc
#define obstack_chunk_free free

#define MAX_UNIVERSES 2

typedef struct dpm_pkg_struct  *dpm_pkg;

struct dpm_cand_struct {
  dpm_cand next;
  dpm_seat seat;
  dpm_version ver;
  int id;

  dpm_dep_node deps;
  dpm_dep_node revdeps;
  bool deps_added;

  int n_unsatisfied[MAX_UNIVERSES];
};

struct dpm_seat_struct {
  dpm_package pkg;
  int id;

  dpm_cand cands;
  dpm_cand_node providers;
  struct dpm_cand_struct null_cand;
  bool providers_added;
  bool relevant;

  dpm_cand selected[MAX_UNIVERSES];
};

struct dpm_dep_struct {
  dpm_cand cand;
  dpm_relation rel;
  bool for_unpack;
  int n_alts;
  int n_selected[MAX_UNIVERSES];
  dpm_cand alts[0];
};

struct dpm_ws_struct {
  struct obstack mem;

  int n_pkgs;
  struct dpm_seat_struct *pkg_seats;

  int n_vers;
  struct dpm_cand_struct *ver_cands;

  struct dpm_seat_struct goal_seat;
  struct dpm_cand_struct goal_cand;
  dpm_candspec goal_spec;

  int next_seat_id;
  int next_cand_id;
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

static dpm_seat
get_seat (dpm_ws ws, dpm_package pkg)
{
  dpm_seat s = ws->pkg_seats + dpm_pkg_id(pkg);
  if (s->cands == NULL)
    {
      dpm_cand n = &(s->null_cand);
      s->cands = n;
      for (int i = 0; i < MAX_UNIVERSES; i++)
	s->selected[i] = n;
    }
  return s;
}

static void
dpm_ws_unref (dyn_type *type, void *object)
{
  struct dpm_ws_struct *ws = object;
  obstack_free (&ws->mem, NULL);
  dyn_unref (ws->goal_spec);
}

static int
dpm_ws_equal (void *a, void *b)
{
  return 0;
}

DYN_DEFINE_TYPE (dpm_ws, "workspace");

static dyn_var cur_ws[1];

void
dpm_ws_create (int n_universe)
{
  dpm_ws ws = dyn_new (dpm_ws);

  obstack_init (&ws->mem);

  ws->n_pkgs = dpm_db_package_id_limit ();
  ws->pkg_seats = obstack_alloc (&ws->mem,
				 ws->n_pkgs*sizeof(struct dpm_seat_struct));
  memset (ws->pkg_seats, 0, ws->n_pkgs*sizeof(struct dpm_seat_struct));

  ws->n_vers = dpm_db_version_id_limit ();
  ws->ver_cands =
    obstack_alloc (&ws->mem, ws->n_vers*sizeof(struct dpm_cand_struct));
  memset (ws->ver_cands, 0, ws->n_vers*sizeof(struct dpm_cand_struct));

  int cand_id = 0;
  int seat_id = 0;

  {
    dpm_seat s = &(ws->goal_seat);
    s->id = seat_id++;
    dpm_cand n = &(s->null_cand);
    n->seat = s;
    n->id = cand_id++;
    s->cands = n;
    for (int i = 0; i < MAX_UNIVERSES; i++)
      s->selected[i] = n;

    dpm_cand g = &(ws->goal_cand);
    g->seat = s;
    g->ver = NULL;
    g->id = cand_id++;
    g->next = s->cands;
    s->cands = g;
  }

  dyn_foreach (pkg, dpm_db_packages)
    {
      dpm_seat s = ws->pkg_seats + dpm_pkg_id (pkg);
      s->id = seat_id++;
      dpm_cand n = &(s->null_cand);
      s->pkg = pkg;
      n->seat = s;
      n->id = cand_id++;
    }

  ws->next_cand_id = cand_id;
  ws->next_seat_id = seat_id;

  dyn_let (cur_ws, ws);
}

dpm_ws
dpm_ws_current ()
{
  return dyn_get (cur_ws);
}

/* Candspecs
 */

struct candspec_alt {
  struct candspec_alt *next;
  dpm_package pkg;
  int op;
  char *ver;
};

struct candspec_rel {
  struct candspec_rel *next;
  bool conf;
  struct candspec_alt *alts;
};

struct dpm_candspec_struct {
  struct obstack mem;
  struct candspec_rel *rels;
};

static void
dpm_candspec_unref (dyn_type *type, void *obj)
{
  dpm_candspec spec = obj;
  obstack_free (&(spec->mem), NULL);
}

static int
dpm_candspec_equal (void *a, void *b)
{
  return 0;
}

DYN_DEFINE_TYPE (dpm_candspec, "candspec");

dpm_candspec
dpm_candspec_new ()
{
  dpm_candspec spec = dyn_new (dpm_candspec);
  obstack_init (&spec->mem);
  spec->rels = NULL;
  return spec;
}

void
dpm_candspec_begin_rel (dpm_candspec spec, bool conf)
{
  struct candspec_rel *rel = obstack_alloc (&spec->mem,
					    sizeof (struct candspec_rel));
  rel->conf = conf;
  rel->alts = NULL;
  rel->next = spec->rels;
  spec->rels = rel;
}

void
dpm_candspec_add_alt (dpm_candspec spec,
		      dpm_package pkg, int op, const char *ver)
{
  struct candspec_alt *alt = obstack_alloc (&spec->mem,
					    sizeof (struct candspec_alt));
  alt->pkg = pkg;
  alt->op = op;
  if (ver)
    alt->ver = obstack_copy0 (&spec->mem, ver, strlen (ver));
  else
    alt->ver = NULL;
  alt->next = spec->rels->alts;
  spec->rels->alts = alt;
}


/* Adding candidates
 */

dpm_cand
dpm_ws_add_cand (dpm_version ver)
{
  dpm_ws ws = dpm_ws_current ();

  dpm_cand c = ws->ver_cands + dpm_ver_id(ver);
  if (c->seat)
    return c;
  
  c->seat = get_seat (ws, dpm_ver_package (ver));
  c->ver = ver;
  c->next = c->seat->cands;
  c->seat->cands = c;
  c->id = ws->next_cand_id++;
  return c;
}

static void
add_relation_cands (dpm_ws ws, dpm_relation rel)
{
  dyn_foreach_iter (a, dpm_db_alternatives, rel)
    {
      bool accept_by_rel (dpm_version ver)
      {
	return dpm_db_check_versions (dpm_ver_version (ver),
				      a.op,
				      a.version);
      }

      dpm_version ver = dpm_pol_get_best_version (a.package, accept_by_rel);
      if (ver)
	dpm_ws_add_cand_and_deps (ver);

      dpm_seat s = get_seat (ws, a.package);
      if (!s->providers_added)
	{
	  bool accept_providers (dpm_version ver)
	  {
	    dyn_foreach (r, ss_elts,
			  dpm_rels_provides (dpm_ver_relations (ver)))
	      if (dpm_rel_package (r, 0) == a.package)
		return true;
	    return false;
	  }

	  s->providers_added = true;
	  dyn_foreach (r, ss_elts, dpm_db_provides (a.package))
	    {
	      dpm_version ver =
		dpm_pol_get_best_version (dpm_ver_package (r),
					  accept_providers);
	      if (ver)
		dpm_ws_add_cand_and_deps (ver);
	    }
	}
    }
}

static void
add_candspec_relation_cands (dpm_ws ws, struct candspec_rel *r)
{
  for (struct candspec_alt *a = r->alts; a; a = a->next)
    {
      bool accept_by_rel (dpm_version ver)
      {
	return dpm_db_check_versions_str (dpm_ver_version (ver),
					  a->op,
					  a->ver, a->ver? strlen (a->ver) : 0);
      }

      dpm_version ver = dpm_pol_get_best_version (a->pkg, accept_by_rel);
      if (ver)
	dpm_ws_add_cand_and_deps (ver);

      dpm_seat s = get_seat (ws, a->pkg);
      if (!s->providers_added)
	{
	  bool accept_providers (dpm_version ver)
	  {
	    dyn_foreach (r, ss_elts,
			  dpm_rels_provides (dpm_ver_relations (ver)))
	      if (dpm_rel_package (r, 0) == a->pkg)
		return true;
	    return false;
	  }

	  s->providers_added = true;
	  dyn_foreach (r, ss_elts, dpm_db_provides (a->pkg))
	    {
	      dpm_version ver =
		dpm_pol_get_best_version (dpm_ver_package (r),
					  accept_providers);
	      if (ver)
		dpm_ws_add_cand_and_deps (ver);
	    }
	}
    }
}

void
dpm_ws_add_cand_deps (dpm_cand cand)
{
  dpm_ws ws = dpm_ws_current ();

  if (cand->deps_added)
    return;

  cand->deps_added = true;

  if (cand == &(ws->goal_cand))
    {
      if (ws->goal_spec)
	{
	  for (struct candspec_rel *r = ws->goal_spec->rels; r; r = r->next)
	    if (!r->conf)
	      add_candspec_relation_cands (ws, r);
	}
    }
  else if (cand->ver)
    {
      void do_rels (ss_val rels)
      {
	dyn_foreach (rel, ss_elts, rels)
	  add_relation_cands (ws, rel);
      }
      do_rels (dpm_rels_pre_depends (dpm_ver_relations (cand->ver)));
      do_rels (dpm_rels_depends (dpm_ver_relations (cand->ver)));
    }
}

dpm_cand
dpm_ws_add_cand_and_deps (dpm_version ver)
{
  dpm_cand c = dpm_ws_add_cand (ver);
  dpm_ws_add_cand_deps (c);
  return c;
}

void
dpm_ws_add_installed ()
{
  dyn_foreach (p, dpm_db_packages)
    {
      dpm_version inst = dpm_db_installed (p);
      if (inst)
	{
	  dpm_cand c = dpm_ws_add_cand (inst);
	  for (int i = 0; i < MAX_UNIVERSES; i++)
	    dpm_ws_select (c, i);
	}
    }
}

void
dpm_ws_set_goal_candspec (dpm_candspec spec)
{
  dpm_ws ws = dpm_ws_current ();
  dyn_ref (spec);
  dyn_unref (ws->goal_spec);
  ws->goal_spec = spec;
}

dpm_cand
dpm_ws_get_goal_cand ()
{
  dpm_ws ws = dpm_ws_current ();
  return &(ws->goal_cand);
}

void
dpm_ws_seats_init (dpm_ws_seats *iter, dpm_package pkg)
{
  dpm_ws ws = dpm_ws_current ();
  iter->cur = get_seat (ws, pkg);
}

void
dpm_ws_seats_fini (dpm_ws_seats *iter)
{
}

void
dpm_ws_seats_step (dpm_ws_seats *iter)
{
  iter->cur = NULL;
}

bool
dpm_ws_seats_done (dpm_ws_seats *iter)
{
  return iter->cur == NULL;
}

dpm_seat
dpm_ws_seats_elt (dpm_ws_seats *iter)
{
  return iter->cur;
}

void
dpm_seat_cands_init (dpm_seat_cands *iter, dpm_seat s)
{
  iter->cur = s->cands;
}

void
dpm_seat_cands_fini (dpm_seat_cands *iter)
{
}

void
dpm_seat_cands_step (dpm_seat_cands *iter)
{
  iter->cur = iter->cur->next;
}

bool
dpm_seat_cands_done (dpm_seat_cands *iter)
{
  return iter->cur == NULL;
}

dpm_cand
dpm_seat_cands_elt (dpm_seat_cands *iter)
{
  return iter->cur;
}

dpm_seat
dpm_cand_seat (dpm_cand c)
{
  return c->seat;
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

dpm_package
dpm_seat_package (dpm_seat s)
{
  return s->pkg;
}

int
dpm_ws_cand_id_limit ()
{
  dpm_ws ws = dpm_ws_current ();
  return ws->next_cand_id;
}

int
dpm_ws_seat_id_limit ()
{
  dpm_ws ws = dpm_ws_current ();
  return ws->next_seat_id;
}

int
dpm_seat_id (dpm_seat s)
{
  return s->id;
}

bool
dpm_seat_relevant (dpm_seat s)
{
  return s->relevant;
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
	  dyn_foreach (prv, ss_elts,
			dpm_rels_provides (dpm_ver_relations (c->ver)))
	    {
	      dpm_seat s = get_seat (ws, dpm_rel_package (prv, 0));
	      s->providers = cons_cand (ws, c, s->providers);
	    }
	}
    }
}

static bool
satisfies_rel_str (dpm_cand c, bool conf, int op, const char *version)
{
  bool res = (c->ver
	      && dpm_db_check_versions_str (dpm_ver_version (c->ver),
					    op,
					    version,
					    version? strlen (version) : 0));
  if (conf)
    return !res;
  else
    return res;
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
provides_rel_str (dpm_cand c, bool conf, int op, const char *version)
{
  bool res = c->ver != NULL;

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

/* Dep builder
 */

typedef struct depb_struct {
  dpm_ws ws;
  dpm_candset alt_set;
  int n_alts;
} depb;

static void
depb_init (depb *db)
{
  db->ws = dpm_ws_current ();
  db->alt_set = dpm_candset_new ();
}

static void
depb_start (depb *db)
{
  db->n_alts = 0;
  dpm_candset_reset (db->alt_set);
  obstack_blank (&(db->ws->mem), sizeof (struct dpm_dep_struct));
}

bool
depb_collect_seat_alts (depb *db, dpm_seat s,
			bool (*satisfies) (dpm_cand c),
			bool (*provides) (dpm_cand c))
{
  void add_alt (dpm_cand c)
  {
    if (!dpm_candset_has (db->alt_set, c))
      {
	dpm_candset_add (db->alt_set, c);
	obstack_ptr_grow (&(db->ws->mem), c);
	db->n_alts++;
      }
  }

  if (db->n_alts < 0)
    return false;

  bool all_satisfy = true;

  dyn_foreach (c, dpm_seat_cands, s)
    if (satisfies (c))
      add_alt (c);
    else
      all_satisfy = false;
  
  if (all_satisfy)
    {
      // This dependency does not need to be
      // recorded since it is always satisfied, no
      // matter with candidate of of
      // P is selected.
      //
      db->n_alts = -1;
      return false;
    }
  
  for (dpm_cand_node n = s->providers; n; n = n->next)
    if (provides (n->elt))
      add_alt (n->elt);

  return true;
}

void
depb_finish (depb *db, dpm_cand c, dpm_relation rel, bool for_unpack)
{
  dpm_dep d = obstack_finish (&db->ws->mem);
  if (db->n_alts < 0)
    obstack_free (&(db->ws->mem), d);
  else
    {
      d->cand = c;
      d->rel = rel;
      d->for_unpack = for_unpack;
      d->n_alts = db->n_alts;
      
      c->deps = cons_dep (db->ws, d, c->deps);
      
      for (int u = 0; u < MAX_UNIVERSES; u++)
	{
	  d->n_selected[u] = 0;
	  for (int i = 0; i < db->n_alts; i++)
	    if (d->alts[i]->seat->selected[u] == d->alts[i])
	      d->n_selected[u]++;
	  
	  if (d->n_selected[u] == 0)
	    c->n_unsatisfied[u]++;
	}
      
      for (int i = 0; i < db->n_alts; i++)
	d->alts[i]->revdeps =
	  cons_dep (db->ws, d, d->alts[i]->revdeps);
    }
}

static void
compute_deps ()
{
  dpm_ws ws = dpm_ws_current ();

  dyn_block
    {
      depb db;
      depb_init (&db);

      for (int i = 0; i < ws->n_vers; i++)
	{
	  dpm_cand c = ws->ver_cands + i;
	  if (c->ver)
	    {
	      void do_rels (ss_val rels, bool conf, bool for_unpack)
	      {
		dyn_foreach (rel, ss_elts, rels)
		  {
		    depb_start (&db);

		    dyn_foreach_iter (alt, dpm_db_alternatives, rel)
		      {
			bool satisfies (dpm_cand c)
			{
			  return satisfies_rel (c, conf, alt.op, alt.version);
			}

			bool provides (dpm_cand c)
			{
			  return provides_rel (c, conf, alt.op, alt.version);
			}

			dpm_seat s = get_seat (ws, alt.package);
			depb_collect_seat_alts (&db, s, satisfies, provides);
		      }

		    depb_finish (&db, c, rel, for_unpack);
		  }
	      }
	  
	      do_rels (dpm_rels_pre_depends (dpm_ver_relations (c->ver)),
		       false, true);
	      do_rels (dpm_rels_depends (dpm_ver_relations (c->ver)),
		       false, false);
	      do_rels (dpm_rels_conflicts (dpm_ver_relations (c->ver)),
		       true, true);
	      do_rels (dpm_rels_breaks (dpm_ver_relations (c->ver)),
		       true, false);
	    }
	}
    }
}

static void
compute_goal_deps ()
{
  dpm_ws ws = dpm_ws_current ();

  if (ws->goal_spec == NULL)
    return;

  dyn_block
    {
      depb db;
      depb_init (&db);

      dpm_cand c = &(ws->goal_cand);

      for (struct candspec_rel *r = ws->goal_spec->rels; r; r = r->next)
	{
	  depb_start (&db);

	  for (struct candspec_alt *a = r->alts; a; a = a->next)
	    {
	      bool satisfies (dpm_cand c)
	      {
		return satisfies_rel_str (c, r->conf, a->op, a->ver);
	      }
	      
	      bool provides (dpm_cand c)
	      {
		return provides_rel_str (c, r->conf, a->op, a->ver);
	      }

	      dpm_seat s = get_seat (ws, a->pkg);
	      depb_collect_seat_alts (&db, s, satisfies, provides);
	    }

	  depb_finish (&db, c, NULL, false);
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

bool
dpm_dep_for_unpack (dpm_dep d)
{
  return d->for_unpack;
}

bool
dpm_dep_for_setup (dpm_dep d)
{
  return true;
}

/* Starting
 */


static void
mark_relevant (dpm_ws ws)
{
  void mark (dpm_cand c)
  {
    if (c->seat->relevant)
      return;
    
    c->seat->relevant = true;
    dyn_foreach (d, dpm_cand_deps, c)
      dyn_foreach (a, dpm_dep_alts, d)
        mark (a);
  }

  mark (&(ws->goal_cand));
}

void
dpm_ws_start ()
{
  dpm_ws ws = dpm_ws_current ();
  
  find_providers (ws);
  compute_deps ();
  compute_goal_deps ();
  mark_relevant (ws);
}

/* Selecting
 */

void
dpm_ws_select (dpm_cand c, int universe)
{
  dpm_seat s = c->seat;
  
  if (s->selected[universe] == c)
    return;

  dyn_foreach (d, dpm_cand_revdeps, s->selected[universe])
    {
      d->n_selected[universe]--;
      if (d->n_selected[universe] == 0)
	d->cand->n_unsatisfied[universe]++;
    }

  s->selected[universe] = c;

  dyn_foreach (d, dpm_cand_revdeps, c)
    {
      if (d->n_selected[universe] == 0)
	d->cand->n_unsatisfied[universe]--;
      d->n_selected[universe]++;
    }
}

dpm_cand
dpm_ws_selected (dpm_seat s, int universe)
{
  return s->selected[universe];
}

bool
dpm_dep_satisfied (dpm_dep d, int universe)
{
  return d->n_selected[universe] > 0;
}

bool
dpm_cand_satisfied (dpm_cand c, int universe)
{
  return c->n_unsatisfied[universe] == 0;
}

bool
dpm_ws_is_selected (dpm_cand cand, int universe)
{
  return cand->seat->selected[universe] == cand;
}

/* Dumping
 */

void
dpm_cand_print_id (dpm_cand c)
{
  if (c->seat->pkg)
    {
      ss_val n = dpm_pkg_name (c->seat->pkg);
      if (c->ver)
	dyn_print ("%r_%r", n, dpm_ver_version (c->ver));
      else
	dyn_print ("%r_null", n);
    }
  else
    dyn_print ("goal-cand");
}

static void
dump_seat (dpm_ws ws, dpm_seat s, int u)
{
  if (s->pkg)
    dyn_print ("%r", dpm_pkg_name (s->pkg));
  else
    dyn_print ("goal");

  if (s->relevant)
    dyn_print (" (relevant)");
  
  dyn_print ("\n");

  dyn_foreach (c, dpm_seat_cands, s)
    {
      if (c->ver)
	dyn_print (" %r", dpm_ver_version (c->ver));
      else if (c == &(ws->goal_cand))
	dyn_print (" goal-cand");
      else
	dyn_print (" null");
      
      if (dpm_ws_is_selected (c, u))
	{
	  if (!dpm_cand_satisfied (c, u))
	    dyn_print (" XXX");
	  else
	    dyn_print (" ***");
	}
	
      dyn_print ("\n");

      dyn_foreach (d, dpm_cand_deps, c)
	{
	  dyn_print ("  >");
	  if (!dpm_dep_satisfied (d, u))
	    dyn_print (" !!!");
	  dyn_foreach (a, dpm_dep_alts, d)
	    {
	      dyn_print (" ");
	      dpm_cand_print_id (a);
	    }
	  dyn_print ("\n");
	  if (d->rel)
	    {
	      dyn_print ("    ");
	      dpm_dump_relation (d->rel);
	      dyn_print ("\n");
	    }
	}
      dyn_foreach (r, dpm_cand_revdeps, c)
	{
	  dyn_print ("  < ");
	  dpm_cand_print_id (r->cand);
	  dyn_print ("\n");
	}
    }
}

void
dpm_ws_dump (int universe)
{
  dpm_ws ws = dpm_ws_current ();

  dump_seat (ws, &(ws->goal_seat), universe);
  dyn_print ("\n");

  for (int i = 0; i < ws->n_pkgs; i++)
    {
      dpm_seat s = ws->pkg_seats + i;
      if (s->cands)
	{
	  dump_seat (ws, s, universe);
	  dyn_print ("\n");
	}
    }
}

static void
dump_broken_seat (dpm_ws ws, dpm_seat s, int u)
{
  dpm_cand c = dpm_ws_selected (s, u);
  if (!dpm_cand_satisfied (c, u))
    {
      dpm_cand_print_id (c);
      dyn_print (" is broken\n");

      dyn_foreach (d, dpm_cand_deps, c)
	{
	  if (!dpm_dep_satisfied (d, u))
	    {
	      dyn_print (" it depends on");
	      bool first = true;
	      dyn_foreach (a, dpm_dep_alts, d)
		{
		  if (!first)
		    dyn_print (", or");
		  dyn_print (" ");
		  dpm_cand_print_id (a);
		  first = false;
		}
	      dyn_print (", but none of them is selected.\n");
	    }
	}
      dyn_print ("\n");
    }
}

void
dpm_ws_show_broken (int universe)
{
  dpm_ws ws = dpm_ws_current ();
  dump_broken_seat (ws, &(ws->goal_seat), universe);

  for (int i = 0; i < ws->n_pkgs; i++)
    {
      dpm_seat s = ws->pkg_seats + i;
      if (s->cands)
	dump_broken_seat (ws, s, universe);
    }
}

void
dpm_ws_dump_pkg (dpm_package p, int universe)
{
  dpm_ws ws = dpm_ws_current ();
  dump_seat (ws, get_seat (ws, p), universe);
}
