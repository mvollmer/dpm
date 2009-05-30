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

#include "alg.h"
#include "db.h"

typedef struct relnode {
  struct relnode *next;
  
  int conflict;
  dpm_relation relation;
} relnode;

typedef struct {
  dpm_package package;
  dpm_version installed;

  int flags;

  // Other version that have declare relations to us that we care
  // about.
  //
  int n_others;
  dpm_version *others;

  int computing_broken;
  relnode *broken;
} pkg_info;

struct dpm_ws_struct {
  int n_packages;
  pkg_info *info;

  // number of info structs with info->broken != NULL.
  int n_broken;
};

static void
dpm_ws_unref (dyn_type *type, void *object)
{
  struct dpm_ws_struct *ws = object;
  for (int i = 0; i < ws->n_packages; i++)
    {
      pkg_info *info = ws->info + i;

      free (info->others);

      while (info->broken)
	{
	  relnode *next = info->broken->next;
	  free (info->broken);
	  info->broken = next;
	}
    }
  free (ws->info);
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

  ws->n_packages = dpm_db_package_count ();
  ws->info = calloc (ws->n_packages, sizeof(pkg_info));

  dyn_let (cur_ws, ws);
}

dpm_ws
dpm_ws_current ()
{
  return dyn_get (cur_ws);
}

// Package information

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
init_info (dpm_ws ws, dpm_package pkg)
{
  pkg_info *info = ws->info + dpm_pkg_id (pkg);

  info->package = pkg;
  info->installed = NULL;

  ss_val reverse = dpm_db_reverse_relations (pkg);
  if (reverse)
    {
      int n_others = 0;
      dpm_package others[ss_len(reverse)];
      for (int i = 0; i < ss_len(reverse); i++)
	{
	  dpm_version ver = ss_ref (reverse, i);
	  dpm_relations rels = dpm_ver_relations (ver);

	  if (has_target (dpm_rels_pre_depends (rels), pkg)
	      || has_target (dpm_rels_depends (rels), pkg)
	      || has_target (dpm_rels_conflicts (rels), pkg))
	    others[n_others++] = ver;
	}
      info->n_others = n_others;
      info->others = dyn_memdup (others, n_others*sizeof(dpm_version));
    }
  else
    {
      info->n_others = 0;
      info->others = NULL;
    }
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

      pkg_info *target_info = ws->info + dpm_pkg_id (target);
      if (target_info->installed && 
	  dpm_db_check_versions (dpm_ver_version (target_info->installed),
				 dpm_rel_op (rel, i),
				 dpm_rel_version (rel, i)))
	return conflict;
    }
  
  return !conflict;
}

static void
find_broken (dpm_ws ws, pkg_info *info, ss_val rels, int conflict)
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
	      relnode *n = dyn_malloc (sizeof(relnode));
	      n->next = info->broken;
	      info->broken = n;
	      n->conflict = conflict;
	      n->relation = rel;
	    }
	}
    }
}

static void
compute_broken (dpm_ws ws, dpm_package pkg)
{
  pkg_info *info = ws->info + dpm_pkg_id (pkg);
  
  if (info->broken)
    ws->n_broken -= 1;

  while (info->broken)
    {
      relnode *next = info->broken->next;
      free (info->broken);
      info->broken = next;
    }
  
  if (info->installed)
    {
      dpm_relations rels = dpm_ver_relations (info->installed);
      find_broken (ws, info, dpm_rels_pre_depends (rels), 0);
      find_broken (ws, info, dpm_rels_depends (rels), 0);
      find_broken (ws, info, dpm_rels_conflicts (rels), 1);
    }

  if (info->broken)
    ws->n_broken += 1;
}

static void
compute_broken_others (dpm_ws ws, dpm_package pkg)
{
  pkg_info *info = ws->info + dpm_pkg_id (pkg);

  // This is the bit that needs to be optimized by recomputing broken
  // relations incrementally.
  
  for (int i = 0; i < info->n_others; i++)
    {
      dpm_version other_ver = info->others[i];
      dpm_package other_pkg = dpm_ver_package (other_ver);
      pkg_info *other_info = ws->info + dpm_pkg_id (other_pkg);

      if (other_info->installed == other_ver)
	compute_broken (ws, other_pkg);
    }
}

void
dpm_ws_set_installed (dpm_package pkg, dpm_version ver)
{
  dpm_ws ws = dyn_get (cur_ws);

  pkg_info *info = ws->info + dpm_pkg_id (pkg);
  if (info->package == NULL)
    init_info (ws, pkg);
  
  info->installed = ver;
  compute_broken (ws, pkg);
  compute_broken_others (ws, pkg);
  // fprintf (stderr, "Broken: %d\n", ws->n_broken);
}

void
dpm_ws_do_broken (dpm_package pkg,
		  void (*func) (int conflict,
				dpm_relation rel,
				void *data),
		  void *data)
{
  dpm_ws ws = dyn_get (cur_ws);
  pkg_info *info = ws->info + dpm_pkg_id (pkg);
  // grrr, get rid of this
  int i = 0;
  int conflict[512];
  dpm_relation relation[512];
  for (relnode *n = info->broken; n; n = n->next)
    {
      conflict[i] = n->conflict;
      relation[i] = n->relation;
      i++;
    }

  for (int j = 0; j < i; j++)
    func (conflict[j], relation[j], data);
}

void
dpm_ws_flag (dpm_package pkg)
{
  dpm_ws ws = dyn_get (cur_ws);
  pkg_info *info = ws->info + dpm_pkg_id (pkg);
  info->flags = 1;
}

void
dpm_ws_unflag (dpm_package pkg)
{
  dpm_ws ws = dyn_get (cur_ws);
  pkg_info *info = ws->info + dpm_pkg_id (pkg);
  info->flags = 0;
}

int
dpm_ws_is_flagged (dpm_package pkg)
{
  dpm_ws ws = dyn_get (cur_ws);
  pkg_info *info = ws->info + dpm_pkg_id (pkg);
  return info->flags != 0;
}

void
dpm_ws_report ()
{
  dpm_ws ws = dyn_get (cur_ws);

  for (int i = 0; i < ws->n_packages; i++)
    {
      pkg_info *info = ws->info + i;

      if (info->installed)
	{
	  dpm_version ver = info->installed;
	  dyn_print ("%r %r\n",
		     dpm_pkg_name (dpm_ver_package (ver)),
		     dpm_ver_version (ver));
	}

      if (info->broken)
	{
	  if (info->installed == NULL)
	    dyn_print ("%r\n", dpm_pkg_name (info->package));

	  for (relnode *n = info->broken; n; n = n->next)
	    {
	      dyn_print (" %s ", (n->conflict? "conflicts with" : "needs"));
	      show_relation (n->relation);
	      dyn_print ("\n");
	    }
	}
    }
}
