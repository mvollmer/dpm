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

typedef struct dpm_pkg_struct  *dpm_pkg;

struct dpm_cand_struct {
  dpm_cand next;
  dpm_seat seat;
  dpm_version ver;
  int id;

  dpm_dep_node deps;
  dpm_dep_node revdeps;
  int n_unsatisfied;

  bool deps_added;
};

struct dpm_seat_struct {
  dpm_package pkg;
  int id;

  dpm_cand cands;
  dpm_cand selected;
  dpm_cand_node providers;
  struct dpm_cand_struct null_cand;
  bool providers_added;
  bool relevant;
};

struct dpm_dep_struct {
  dpm_cand cand;
  dpm_relation rel;
  int n_alts;
  int n_selected;
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
      s->selected = n;
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
dpm_ws_create ()
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
    s->selected = n;

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
		dyn_foreach (rel, ss_elts, rels)
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
			dpm_seat s = get_seat (ws, alt.package);
			
			bool all_satisfy = true;
			dyn_foreach (pc, dpm_seat_cands, s)
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
		    
			for (dpm_cand_node n = s->providers; n; n = n->next)
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

			for (int i = 0; i < n_alts; i++)
			  if (d->alts[i]->seat->selected == d->alts[i])
			    d->n_selected++;

			if (d->n_selected == 0)
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

static void
compute_goal_deps (dpm_ws ws)
{
  /* XXX - This much code duplication is a crime, of course.
   */

  if (ws->goal_spec == NULL)
    return;

  dyn_block
    {
      dpm_candset alt_set = dpm_candset_new ();

      dpm_cand c = &(ws->goal_cand);

      for (struct candspec_rel *r = ws->goal_spec->rels; r; r = r->next)
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
	    
	  for (struct candspec_alt *a = r->alts; a; a = a->next)
	    {
	      dpm_seat s = get_seat (ws, a->pkg);
	      
	      bool all_satisfy = true;
	      dyn_foreach (pc, dpm_seat_cands, s)
		if (satisfies_rel_str (pc, r->conf, a->op, a->ver))
		  add_alt (pc);
		else
		  all_satisfy = false;
	      
	      if (all_satisfy)
		{
		  // This dependency does not need to be
		  // recorded since it is always satisfied, no
		  // matter with candidate of P is selected.
		  //
		  n_alts = -1;
		  break;
		}
	      
	      for (dpm_cand_node n = s->providers; n; n = n->next)
		if (provides_rel_str (n->elt, r->conf, a->op, a->ver))
		  add_alt (n->elt);
	    }
	  
	  dpm_dep d = obstack_finish (&ws->mem);
	  if (n_alts < 0)
	    obstack_free (&ws->mem, d);
	  else
	    {
	      d->cand = c;
	      d->rel = NULL;
	      d->n_selected = 0;
	      d->n_alts = n_alts;
	      
	      c->deps = cons_dep (ws, d, c->deps);
	      
	      for (int i = 0; i < n_alts; i++)
		if (d->alts[i]->seat->selected == d->alts[i])
		  d->n_selected++;
	      
	      if (d->n_selected == 0)
		c->n_unsatisfied++;
		
	      for (int i = 0; i < n_alts; i++)
		d->alts[i]->revdeps =
		  cons_dep (ws, d, d->alts[i]->revdeps);
	    }
	  
	  dpm_candset_reset (alt_set);
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

/* Starting
 */

void
dpm_ws_start ()
{
  dpm_ws ws = dpm_ws_current ();
  
  find_providers (ws);
  compute_deps (ws);
  compute_goal_deps (ws);
  mark_relevant (ws);
}

/* Selecting
 */

void
dpm_ws_select (dpm_cand c)
{
  dpm_seat s = c->seat;
  
  if (s->selected == c)
    return;

  dyn_foreach (d, dpm_cand_revdeps, s->selected)
    {
      d->n_selected--;
      if (d->n_selected == 0)
	d->cand->n_unsatisfied++;
    }

  s->selected = c;

  dyn_foreach (d, dpm_cand_revdeps, c)
    {
      if (d->n_selected == 0)
	d->cand->n_unsatisfied--;
      d->n_selected++;
    }
}

dpm_cand
dpm_seat_selected (dpm_seat s)
{
  return s->selected;
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

bool
dpm_cand_selected (dpm_cand cand)
{
  return cand->seat->selected == cand;
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
dump_seat (dpm_ws ws, dpm_seat s)
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
      
      if (dpm_cand_selected (c))
	{
	  if (!dpm_cand_satisfied (c))
	    dyn_print (" XXX");
	  else
	    dyn_print (" ***");
	}
	
      dyn_print ("\n");

      dyn_foreach (d, dpm_cand_deps, c)
	{
	  dyn_print ("  >");
	  if (!dpm_dep_satisfied (d))
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
dpm_ws_dump ()
{
  dpm_ws ws = dpm_ws_current ();

  dump_seat (ws, &(ws->goal_seat));
  dyn_print ("\n");

  for (int i = 0; i < ws->n_pkgs; i++)
    {
      dpm_seat s = ws->pkg_seats + i;
      if (s->cands)
	{
	  dump_seat (ws, s);
	  dyn_print ("\n");
	}
    }
}

void
dpm_ws_dump_pkg (dpm_package p)
{
  dpm_ws ws = dpm_ws_current ();
  dump_seat (ws, get_seat (ws, p));
}
