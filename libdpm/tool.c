#define _GNU_SOURCE

#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include "dpm.h"

bool flag_simulate = false;

void
usage ()
{
  fprintf (stderr, "Usage: dpm-tool [OPTIONS] update ORIGIN FILE\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] show [PACKAGE [VERSION]]\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] search STRING\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] tags EXPRESSION\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] relations PACKAGE\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] provides PACKAGE\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] install PACKAGE\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] remove PACKAGE\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] stats\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] dump\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] gc\n");
  exit (1);
}

void
update_origin (const char *origin, const char *file)
{
  dyn_input in = dyn_open_file (file);

  dpm_db_open ();
  dpm_origin o = dpm_db_origin_find (origin);
  dpm_db_origin_update (o, in);
  dpm_db_checkpoint ();
  dpm_db_done ();
}

void
show_versions (dpm_package pkg)
{
  typedef struct {
    dpm_version ver;
    dpm_origin org;
  } verorg;

  verorg vo[100];
  int n_vo = 0;

  dyn_foreach (o, dpm_db_origins)
    {
      dyn_foreach (v, dpm_db_origin_package_versions, o, pkg)
	if (n_vo < 100)
	  {
	    vo[n_vo].ver = v;
	    vo[n_vo].org = o;
	    n_vo++;
	  }
    }

  if (n_vo == 1)
    {
      dpm_db_version_show (vo[0].ver);
      return;
    }

  int cmp (const void *_a, const void *_b)
  {
    const verorg *a = _a, *b = _b;

    int c = dpm_db_compare_versions (dpm_ver_version (b->ver),
				     dpm_ver_version (a->ver));
    if (c == 0)
      c = b - a;

    return c;
  }

  qsort (vo, n_vo, sizeof (verorg), cmp);

  int max_version_len = 0;
  for (int i = 0; i < n_vo; i++)
    {
      int l = ss_len (dpm_ver_version (vo[i].ver));
      if (l > max_version_len)
	max_version_len = l;
    }

  static const char padding[] = "                               ";

  for (int i = 0; i < n_vo; i++)
    {
      int pad = max_version_len - ss_len (dpm_ver_version (vo[i].ver));
      dyn_print ("%r %r%ls (%r",
		 dpm_pkg_name (pkg),
		 dpm_ver_version (vo[i].ver),
		 padding, pad,
		 dpm_origin_label (vo[i].org));
      while (i+1 < n_vo && vo[i+1].ver == vo[i].ver)
	{
	  dyn_print (", %r", dpm_origin_label (vo[i+1].org));
	  i++;
	}
      dyn_print (")\n");
    }
}

void
show (const char *package, const char *version)
{
  dpm_db_open ();

  if (package == NULL)
    {
      dyn_foreach (p, dpm_db_packages)
        dyn_print ("%r\n", dpm_pkg_name (p));
    }
  else
    {
      dpm_package pkg = dpm_db_package_find (package);

      if (version == NULL)
	show_versions (pkg);
      else
	{
	  ss_val interned_version = dpm_db_intern (version);

	  bool need_blank_line = false;
	  dyn_foreach (o, dpm_db_origins)
	    {
	      dyn_foreach (v, dpm_db_origin_package_versions, o, pkg)
		{
		  if (dpm_ver_version (v) == interned_version)
		    {
		      if (need_blank_line)
			dyn_print ("\n");
		      dyn_print ("Origin: %r\n", dpm_origin_label (o));
		      dpm_db_version_show (v);
		      need_blank_line = true;
		    }
		}
	    }
	}
    }

  dpm_db_done ();
}

void
stats ()
{
  dpm_db_open ();
  dpm_db_stats ();
  dpm_db_done ();
}

const char *relname[] = {
  [DPM_EQ] = "=",
  [DPM_LESS] = "<<",
  [DPM_LESSEQ] = "<=",
  [DPM_GREATER] = ">>",
  [DPM_GREATEREQ] = ">="
};

static void
show_relation_part (ss_val rel, int i)
{
  int op = dpm_rel_op (rel, i);
  dyn_print ("%r", dpm_pkg_name (dpm_rel_package (rel, i)));
  if (op != DPM_ANY)
    dyn_print (" (%s %r)", relname[op], dpm_rel_version (rel, i));
}

static void
show_relation (ss_val rel)
{
  for (int i = 0; i < ss_len (rel); i += 3)
    {
      if (i > 0)
	dyn_print (" | ");
      show_relation_part (rel, i);
    }
}

static void
show_filtered_relations (const char *field,
			 ss_val rels, dpm_package pkg)
{
  if (rels)
    {
      int first = 1;
      for (int i = 0; i < ss_len (rels); i++)
	{
	  ss_val rel = ss_ref (rels, i);
	  for (int j = 0; j < ss_len (rel); j += 3)
	    if (dpm_rel_package (rel, j) == pkg)
	      {
		if (first)
		  dyn_print ("  %s: ", field);
		else
		  dyn_print (", ");
		show_relation (rel);
		first = 0;
	      }
	}
      if (!first)
	dyn_print ("\n");
    }
}

static void
list_versions (dpm_version *versions, int n_versions,
	       dpm_package rev)
{
  int cmp (const void *_a, const void *_b)
  {
    dpm_version a = *(dpm_version *)_a;
    dpm_version b = *(dpm_version *)_b;
    return ss_strcmp (dpm_pkg_name (dpm_ver_package (a)),
		      dpm_pkg_name (dpm_ver_package (b)));
  }

  qsort (versions, n_versions, sizeof (dpm_version), cmp);

  int max_len = 0;
  for (int i = 0; i < n_versions; i++)
    {
      ss_val name = dpm_pkg_name (dpm_ver_package (versions[i]));
      if (ss_len (name) < 30 && ss_len (name) > max_len)
	max_len = ss_len (name);
    }
  
  static const char padding[30] = "                              ";

  for (int i = 0; i < n_versions; i++)
    {
      dpm_version ver = versions[i];
      
      if (rev == NULL)
	{
	  dpm_package pkg = dpm_ver_package (ver);
	  if (i+1 < n_versions && dpm_ver_package (versions[i+1]) == pkg)
	    continue;

	  ss_val name = dpm_pkg_name (pkg);
	  int pad = max_len - ss_len (name);
	  if (pad < 0)
	    pad = 0;

	  dyn_print ("%r%ls - %r\n",
		     name,
		     padding, pad,
		     dpm_ver_shortdesc (ver));
	}
      else
	{
	  dyn_print ("%r %r - %r\n",
		     dpm_pkg_name (dpm_ver_package (ver)),
		     dpm_ver_version (ver),
		     dpm_ver_shortdesc (ver));

	  ss_val rels_rec = dpm_ver_relations (ver);
	  show_filtered_relations ("Pre-Depends",
				   dpm_rels_pre_depends (rels_rec), rev);
	  show_filtered_relations ("Depends",
				   dpm_rels_depends (rels_rec), rev);
	  show_filtered_relations ("Conflicts",
				   dpm_rels_conflicts (rels_rec), rev);
	  show_filtered_relations ("Provides",
				   dpm_rels_provides (rels_rec), rev);
	  show_filtered_relations ("Replaces",
				   dpm_rels_replaces (rels_rec), rev);
	  show_filtered_relations ("Breaks",
				   dpm_rels_breaks (rels_rec), rev);
	  show_filtered_relations ("Recommends",
				   dpm_rels_recommends (rels_rec), rev);
	  show_filtered_relations ("Enhances",
				   dpm_rels_enhances (rels_rec), rev);
	  show_filtered_relations ("Suggests",
				   dpm_rels_suggests (rels_rec), rev);
	}
    }
}

static void
list_ss_versions (ss_val versions, dpm_package rev)
{
  if (versions)
    {
      int n = ss_len(versions);
      dpm_version v[n];
      for (int i = 0; i < n; i++)
	v[i] = ss_ref (versions, i);
      list_versions (v, n, rev);
    }
}

void
search (const char *pattern)
{
  int pattern_len = strlen (pattern);

  dpm_db_open ();

  bool seen[dpm_db_package_id_limit()];
  memset (seen, 0, sizeof(seen));

  dpm_version hits[dpm_db_version_id_limit()];
  int n_hits = 0;

  dyn_foreach (v, dpm_db_versions)
    {
      dpm_package p = dpm_ver_package (v);
      if (seen[dpm_pkg_id(p)])
	continue;
      
      seen[dpm_pkg_id(p)] = true;
      ss_val name = dpm_pkg_name (p);
      ss_val desc;
      if (memmem (ss_blob_start (name), ss_len (name), pattern, pattern_len)
	  || ((desc = dpm_db_version_get (v, "Description"))
	      && memmem (ss_blob_start (desc), ss_len (desc),
			 pattern, pattern_len)))
	hits[n_hits++] = v;
    }
  
  list_versions (hits, n_hits, NULL);

  dpm_db_done ();
}

void
tags (const char *exp)
{
  if (exp)
    {
      dpm_db_open ();
      list_ss_versions (dpm_db_query_tag (exp), NULL);
      dpm_db_done ();
    }
}

void
list_reverse_relations (const char *package)
{
  if (package)
    {
      dpm_db_open ();

      dpm_package pkg = dpm_db_package_find (package);
      ss_val versions = dpm_db_reverse_relations (pkg);
      list_ss_versions (versions, pkg);
      dpm_db_done ();
    }
}

static void
list_provides (const char *package)
{
  if (package)
    {
      dpm_db_open ();
      dpm_package pkg = dpm_db_package_find (package);
      dyn_foreach (ver, ss_elts, dpm_db_provides (pkg))
	dyn_print ("%r %r\n",
		   dpm_pkg_name (dpm_ver_package (ver)),
		   dpm_ver_version (ver));
      dpm_db_done ();
    }
}

void
dump_store_reference (ss_store ss, ss_val o)
{
  int i;

  if (o == NULL)
    printf (" nil\n");
  else if (ss_is_int (o))
    printf (" %d\n", ss_to_int (o));
  else if (ss_is_blob (o))
    {
      int l = ss_len (o);
      char *b = ss_blob_start (o);
      printf (" (b%d) ", ss_id (ss, o));
      for (i = 0; i < l; i++)
	printf ("%c", isprint(b[i])? b[i] : '.');
      printf ("\n");
    }
  else
    printf (" r%d (%d)\n", ss_id (ss, o), ss_tag (o));
}

void
dump_store_object (ss_store ss, ss_val o)
{
  int i;

  if (o == NULL)
    printf ("NULL\n");
  else if (ss_is_int (o))
    printf ("%d\n", ss_to_int (o));
  else if (ss_is_blob (o))
    {
      int l = ss_len (o);
      printf ("b%d: (blob, %d bytes)\n", ss_id (ss, o), l);
      dump_store_reference (ss, o);
    }
  else
    {
      int n = ss_len (o);
      printf ("r%d: (tag %d, %d fields)\n", ss_id (ss, o), ss_tag (o), n);
      for (i = 0; i < n; i++)
	dump_store_reference (ss, ss_ref (o, i));
      if (n > 0)
	{
	  for (i = 0; i < n; i++)
	    {
	      ss_val r = ss_ref (o, i);
	      if (r && !ss_is_int (r) && !ss_is_blob (r))
		{
		  printf ("\n");
		  dump_store_object (ss, r);
		}
	    }
	}
    }
}

void
dump_store (const char *name)
{
  ss_store st = ss_open (name, SS_READ);
  dump_store_object (st, ss_get_root (st));
}

void
dump_origin (const char *origin)
{
  dpm_db_open ();
  dpm_ws_create (1);

  dyn_foreach_iter (p, dpm_db_origin_packages, dpm_db_origin_find (origin))
    {
      dyn_foreach (v, ss_elts, p.versions)
	dpm_ws_add_cand (v);
    }

  dpm_ws_start ();
  dpm_ws_dump (0);
}

void
dump (const char *origin)
{
  if (origin == NULL)
    dump_store (dyn_to_string (dyn_get (dpm_database_name)));
  else
    dump_origin (origin);
}

void
cmd_install (char **packages,
	     bool show_deps, bool execute, bool remove, bool manual)
{
  dpm_package pkg;

  dpm_db_open ();
  dpm_ws_create (1);

  dpm_ws_add_installed ();

  dpm_candspec spec = dpm_candspec_new ();
  while (*packages)
    {
      pkg = dpm_db_package_find (*packages);
      if (pkg == NULL)
	dyn_error ("No such package: %s", *packages);
  
      dpm_candspec_begin_rel (spec, false);
      if (remove)
	dpm_candspec_add_alt (spec, pkg, DPM_EQ, NULL);
      else
	dpm_candspec_add_alt (spec, pkg, DPM_ANY, NULL);
      packages++;
    }

  dpm_ws_set_goal_candspec (spec);
  dpm_ws_add_cand_deps (dpm_ws_get_goal_cand ());

  dpm_ws_start ();
  if (dpm_alg_install_naively ())
    {
      if (execute)
	{
	  if (manual)
	    dyn_foreach (d, dpm_cand_deps, dpm_ws_get_goal_cand ())
	      dyn_foreach (a, dpm_dep_alts, d)
	        {
		  if (dpm_ws_is_selected (a))
		    dpm_inst_set_manual (dpm_seat_package (dpm_cand_seat (a)),
					 true);
		}
	  dpm_alg_order_lax (dpm_alg_install_component);
	  if (!flag_simulate)
	    dpm_db_checkpoint ();
	  else
	    dyn_print ("... but not really.\n");
	}
    }
  else
    dpm_ws_show_broken (0);
  
  if (show_deps)
    dpm_ws_dump (0);
}

void
status (char **packages)
{
  dpm_db_open ();

  while (*packages)
    {
      dpm_package pkg = dpm_db_package_find (*packages);
      if (pkg)
	{
	  void pkg_status (dpm_package pkg)
	  {
	    dpm_status status = dpm_db_status (pkg);
	    dpm_version ver = dpm_stat_version (status);
	    if (ver)
	      dyn_print ("%{ver}", ver);
	    else
	      dyn_print ("%{pkg} not installed", pkg);
	    switch (dpm_stat_status (status))
	      {
	      case DPM_STAT_OK:
		break;
	      case DPM_STAT_UNPACKED:
		dyn_print (", unpacked");
		break;
	      }
	    int f = dpm_stat_flags (status);
	    if (f & DPM_STAT_MANUAL)
	      dyn_print (", manual");
	    dyn_print ("\n");
	  }

	  pkg_status (pkg);
	  dyn_foreach (ver, ss_elts, dpm_db_provides (pkg))
	    pkg_status (dpm_ver_package (ver));
	}
      else
	dyn_print ("%s not known", *packages);
      packages++;
    }
}

void
reset ()
{
  dpm_db_open ();

  dyn_foreach (p, dpm_db_packages)
    {
      dpm_db_set_status (p, NULL, DPM_STAT_OK);
      dpm_db_set_status_flags (p, 0);
    }

  dpm_db_checkpoint ();
}

void
print_path (const char *a, const char *b)
{
  dpm_db_open ();
  dpm_ws_create ();

  dpm_ws_add_installed ();
  dpm_ws_start ();

  dpm_package a_pkg = dpm_db_package_find (a);
  dpm_package b_pkg = dpm_db_package_find (b);
  
  dpm_seat a_seat = NULL;
  dyn_foreach (s, dpm_ws_package_seats, a_pkg)
    a_seat = s;

  dpm_seat b_seat = NULL;
  dyn_foreach (s, dpm_ws_package_seats, b_pkg)
    b_seat = s;

  dpm_alg_print_path (a_seat, b_seat);
}

void
cmd_gc ()
{
  dpm_db_open ();
  dpm_db_gc_and_done ();
}

int
main (int argc, char **argv)
{
  while (argv[1] && argv[1][0] == '-')
    {
      if (strcmp (argv[1], "--db") == 0)
        {
          dyn_set (dpm_database_name, dyn_from_string (argv[2]));
          argv += 2;
        }
      else if (strcmp (argv[1], "--simulate") == 0)
	{
	  flag_simulate = true;
          argv += 1;
	}
      else
        usage ();
    }

  if (argv[1] == NULL)
    usage ();

  if (strcmp (argv[1], "update") == 0 && argv[2] && argv[3])
    update_origin (argv[2], argv[3]);
  else if (strcmp (argv[1], "show") == 0)
    show (argv[2], argv[3]);
  else if (strcmp (argv[1], "stats") == 0)
    stats ();
  else if (strcmp (argv[1], "search") == 0)
    search (argv[2]);
  else if (strcmp (argv[1], "tags") == 0)
    tags (argv[2]);
  else if (strcmp (argv[1], "relations") == 0)
    list_reverse_relations (argv[2]);
  else if (strcmp (argv[1], "provides") == 0)
    list_provides (argv[2]);
  else if (strcmp (argv[1], "install") == 0)
    cmd_install (argv+2, false, true, false, true);
  else if (strcmp (argv[1], "remove") == 0)
    cmd_install (argv+2, false, true, true, false);
  else if (strcmp (argv[1], "reset") == 0)
    reset ();
  else if (strcmp (argv[1], "status") == 0)
    status (argv+2);
  else if (strcmp (argv[1], "path") == 0)
    print_path (argv[2], argv[3]);
  else if (strcmp (argv[1], "deps") == 0)
    cmd_install (argv+2, true, false, false, false);
  else if (strcmp (argv[1], "dump") == 0)
    dump (argv[2]);
  else if (strcmp (argv[1], "gc") == 0)
    cmd_gc ();
  else
    usage ();

  return 0;
}
