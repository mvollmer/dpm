#define _GNU_SOURCE

#include <string.h>
#include <stdbool.h>

#include "conf.h"
#include "db.h"
#include "alg.h"
#include "acq.h"
#include "inst.h"
#include "store.h"

DPM_CONF_DECLARE (architectures, "architectures",
		  "(seq string ...)", "(i386)",
		  "The list of architectures.")

DPM_CONF_DECLARE (components, "components",
		  "(seq string ...)", "(main)",
		  "The list of components.")

DPM_CONF_DECLARE (distributions, "distributions",
		  "(seq string ...)", "(stable)",
		  "The list of distributions.")

DPM_CONF_DECLARE (sources, "sources",
		  "(seq string ...)", "(foo)",
		  "The sources.")

const char *target_dist;
int prefer_remove = 0;
int prefer_upgrade = 0;

void
usage ()
{
  fprintf (stderr, "Usage: test update\n");
  exit (1);
}

void
update (int force)
{
  dyn_val srcs = dpm_conf_get (sources);
  dyn_val dists = dpm_conf_get (distributions);
  dyn_val comps = dpm_conf_get (components);
  dyn_val archs = dpm_conf_get (architectures);

  dpm_db_open ();
  if (force)
    dpm_db_full_update (srcs, dists, comps, archs);
  else
    dpm_db_maybe_full_update (srcs, dists, comps, archs);
  dpm_db_done ();
}

void
show (const char *package, const char *version)
{
  if (package)
    {
      dpm_db_open ();
      
      dpm_package pkg = dpm_db_find_package (package);
      dpm_version ver_to_show = NULL;

      if (pkg)
	{
	  ss_val versions = dpm_db_available (pkg);
	  dyn_print ("%r [%d]:", dpm_pkg_name (pkg), dpm_pkg_id (pkg));
	  if (versions)
	    for (int i = 0; i < ss_len (versions); i++)
	      {
		dpm_version ver = ss_ref (versions, i);
		dyn_print (" %r (%r) [%d]",
			   dpm_ver_version (ver),
			   dpm_ver_architecture (ver),
			   dpm_ver_id (ver));
		if (!ver_to_show)
                  {
                    if (version == NULL
                        || ss_equal_blob (dpm_ver_version (ver),
                                          strlen (version), version))
                      ver_to_show = ver;
                  }
	      }
	  dyn_print ("\n");
	}

      if (ver_to_show)
	dpm_db_show_version (ver_to_show);

      dpm_db_done ();
    }
}

static void
info (const char *package)
{
  if (package)
    {
      dpm_db_open ();

      dpm_package pkg = dpm_db_find_package (package);
      
      if (pkg)
	{
	  ss_val versions = dpm_db_available (pkg);
	  dpm_version installed = dpm_db_installed (pkg);
	  int installed_shown = 0;

	  dyn_print ("%r:", dpm_pkg_name (pkg));
	  if (versions)
	    for (int i = 0; i < ss_len (versions); i++)
	      {
		dpm_version ver = ss_ref (versions, i);
		if (ver == installed)
		  {
		    dyn_print (" [%r]", dpm_ver_version (ver));
		    installed_shown = 1;
		  }
		else
		  dyn_print (" %r", dpm_ver_version (ver));

		dyn_foreach (dpm_package_index idx,
			     dpm_db_version_foreach_pkgindex, ver)
		  {
		    dpm_release_index release = dpm_pkgidx_release (idx);
		    if (release)
		      dyn_print (" (%r)", dpm_relidx_dist (release));
		  }
	      }
	  if (!installed_shown && installed)
	    dyn_print (" [%r]", dpm_ver_version (installed));
	  dyn_print ("\n");
	}

      dpm_db_done ();
    }
}


void
search (const char *pattern)
{
  int pattern_len = strlen (pattern);

  void package (dpm_package pkg)
  {
    ss_val versions = dpm_db_available (pkg);

    if (memmem (ss_blob_start (pkg), ss_len (pkg), pattern, pattern_len))
      goto found;

    if (versions)
      for (int i = 0; i < ss_len (versions); i++)
	{
	  dpm_version ver = ss_ref (versions, i);
	  ss_val desc = dpm_db_version_get (ver, "Description");
	  if (desc &&
	      memmem (ss_blob_start (desc), ss_len (desc),
		      pattern, pattern_len))
	    goto found;
	}
    
    return;
    
  found:
    {
      ss_val desc = NULL;
      if (versions && ss_len (versions) > 0)
	desc = dpm_ver_shortdesc (ss_ref (versions, 0));
      
      dyn_print ("%r - %r\n", dpm_pkg_name (pkg), desc);
    }
  }

  if (pattern)
    {
      dpm_db_open ();
      dpm_db_foreach_package (package);
      dpm_db_done ();
    }
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
list_versions (ss_val versions, dpm_package rev, bool only_candidates)
{
  if (versions)
    for (int i = 0; i < ss_len (versions); i++)
      {
	dpm_version ver = ss_ref (versions, i);

        if (only_candidates)
          {
            dpm_version cand = dpm_ws_candidate (dpm_ver_package (ver));

            if (ver != cand)
              continue;
          }

	dyn_print ("%r %r (%r) - %r\n",
		   dpm_pkg_name (dpm_ver_package (ver)),
		   dpm_ver_version (ver),
		   dpm_ver_architecture (ver),
		   dpm_ver_shortdesc (ver));
	if (rev)
	  {
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

void
query (const char *exp)
{
  if (exp)
    {
      dpm_db_open ();
      list_versions (dpm_db_query_tag (exp), NULL, 0);
      dpm_db_done ();
    }
}

void
list_reverse_relations (const char *package)
{
  if (package)
    {
      dpm_db_open ();
      dpm_ws_create ();
      dpm_ws_policy_set_distribution_pin (target_dist);
      dpm_ws_policy_set_prefer_remove (prefer_remove);
      dpm_ws_policy_set_prefer_upgrade (prefer_upgrade);

      dpm_package pkg = dpm_db_find_package (package);
      ss_val versions = dpm_db_reverse_relations (pkg);
      if (versions)
	list_versions (versions, pkg, true);
      dpm_db_done ();
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

static void
list_provides (const char *package)
{
  if (package)
    {
      dpm_db_open ();
      dpm_package pkg = dpm_db_find_package (package);
      ss_val versions = dpm_db_reverse_relations (pkg);
      if (versions)
	for (int i = 0; i < ss_len (versions); i++)
	  {
	    dpm_version ver = ss_ref (versions, i);
	    dpm_relations rels = dpm_ver_relations (ver);
	    if (has_target (dpm_rels_provides (rels), pkg))
	      dyn_print ("%r %r\n",
			 dpm_pkg_name (dpm_ver_package (ver)),
			 dpm_ver_version (ver));
	  }
      dpm_db_done ();
    }
}

static void
base_mark_install ()
{
  void package (dpm_package pkg)
  {
    dpm_version ver = dpm_ws_candidate (pkg);
    if (ver)
      {
	if (ss_streq (dpm_db_version_get (ver, "Priority"), "required")
	    || ss_streq (dpm_db_version_get (ver, "Priority"), "important"))
	  dpm_ws_mark_install (pkg);
      }
  }
  dpm_db_foreach_package (package);
}

void
fun (char **argv, int simulate)
{
  dyn_begin ();

  dpm_db_open ();
  dpm_ws_create ();
  dpm_ws_policy_set_distribution_pin (target_dist);
  dpm_ws_policy_set_prefer_remove (prefer_remove);
  dpm_ws_policy_set_prefer_upgrade (prefer_upgrade);

  for (int i = 0; argv[i]; i++)
    {
      if (strcmp (argv[i], "base") == 0)
	base_mark_install ();
      else
	{
	  dpm_package pkg = dpm_db_find_package (argv[i]);
	  if (pkg)
	    dpm_ws_mark_install (pkg);
	  else
	    dyn_print ("Package %s not found\n", argv[i]);
	}
    }

  dpm_ws_setup_finish ();
  if (dpm_ws_search ())
    dpm_ws_realize (simulate);

  dpm_db_done ();
  dyn_end ();
}

void
nuf (char **argv, int simulate)
{
  dyn_begin ();

  dpm_db_open ();
  dpm_ws_create ();
  dpm_ws_policy_set_distribution_pin (target_dist);
  dpm_ws_policy_set_prefer_remove (prefer_remove);
  dpm_ws_policy_set_prefer_upgrade (prefer_upgrade);

  for (int i = 0; argv[i]; i++)
    {
      dpm_package pkg = dpm_db_find_package (argv[i]);
      if (pkg)
	dpm_ws_mark_remove (pkg);
      else
	dyn_print ("Package %s not found\n", argv[i]);
    }

  dpm_ws_setup_finish ();
  if (dpm_ws_search ())
    dpm_ws_realize (simulate);

  dpm_db_done ();
  dyn_end ();
}

static void
raw_install (char **argv)
{
  dyn_begin ();
  dpm_db_open ();

  dpm_ws_create ();
  dpm_ws_policy_set_distribution_pin (target_dist);
  dpm_ws_policy_set_prefer_remove (prefer_remove);
  dpm_ws_policy_set_prefer_upgrade (prefer_upgrade);

  for (int i = 0; argv[i]; i++)
    {
      dpm_package pkg = dpm_db_find_package (argv[i]);
      dpm_version ver = pkg? dpm_ws_candidate (pkg) : NULL;

      if (ver)
	dpm_install (ver);
      else
	dyn_print ("No installation candidate for %r\n",
		   dpm_pkg_name (pkg));
    }

  dpm_db_checkpoint ();  
  dpm_db_done ();
  dyn_end ();
}

static void
raw_remove (char **argv)
{
  dyn_begin ();
  dpm_db_open ();

  for (int i = 0; argv[i]; i++)
    {
      dpm_package pkg = dpm_db_find_package (argv[i]);

      if (pkg)
	dpm_remove (pkg);
      else
	dyn_print ("Package %s is not installed.\n",
		   argv[i]);
    }

  dpm_db_checkpoint ();  
  dpm_db_done ();
  dyn_end ();
}

static void
remove_all ()
{
  dyn_begin ();
  dpm_db_open ();

  void remove (dpm_package pkg)
  {
    if (dpm_db_installed (pkg))
      dpm_remove (pkg);
  }
  dpm_db_foreach_package (remove);

  dpm_db_checkpoint ();  
  dpm_db_done ();
  dyn_end ();
}

static void
check ()
{
  dyn_begin ();
  dpm_db_open ();
  dpm_ws_create ();
  dpm_ws_policy_set_distribution_pin (target_dist);
  dpm_ws_policy_set_prefer_remove (prefer_remove);
  dpm_ws_policy_set_prefer_upgrade (prefer_upgrade);

  dpm_ws_import ();
  dpm_ws_select_installed ();
  dpm_ws_report ("Check");

  dpm_db_done ();
  dyn_end ();  
}

static void
fix ()
{
  dyn_begin ();
  dpm_db_open ();
  dpm_ws_create ();
  dpm_ws_policy_set_distribution_pin (target_dist);
  dpm_ws_policy_set_prefer_remove (prefer_remove);
  dpm_ws_policy_set_prefer_upgrade (prefer_upgrade);

  dpm_ws_import ();
  dpm_ws_setup_finish ();
  if (dpm_ws_search ())
    dpm_ws_realize (0);

  dpm_db_done ();
  dyn_end ();  
}

int
main (int argc, char **argv)
{
  if (argc < 2)
    usage ();

  if (dyn_file_exists ("test-db.conf"))
    dpm_conf_parse ("test-db.conf");

  while (1)
    {
      if (strcmp (argv[1], "-t") == 0)
	{
	  target_dist = argv[2];
	  argv += 2;
	}
      else if (strcmp (argv[1], "-R") == 0)
	{
	  prefer_remove = 1;
	  argv += 1;
	}
      else if (strcmp (argv[1], "-U") == 0)
	{
	  prefer_upgrade = 1;
	  argv += 1;
	}
      else
	break;
    }

  if (strcmp (argv[1], "update") == 0)
    update (0);
  else if (strcmp (argv[1], "force-update") == 0)
    update (1);
  else if (strcmp (argv[1], "show") == 0)
    show (argv[2], argv[3]);
  else if (strcmp (argv[1], "info") == 0)
    info (argv[2]);
  else if (strcmp (argv[1], "stats") == 0)
    stats ();
  else if (strcmp (argv[1], "search") == 0)
    search (argv[2]);
  else if (strcmp (argv[1], "query") == 0)
    query (argv[2]);
  else if (strcmp (argv[1], "reverse") == 0)
    list_reverse_relations (argv[2]);
  else if (strcmp (argv[1], "provides") == 0)
    list_provides (argv[2]);
  else if (strcmp (argv[1], "fun") == 0)
    fun (argv+2, 1);
  else if (strcmp (argv[1], "real-fun") == 0)
    fun (argv+2, 0);
  else if (strcmp (argv[1], "nuf") == 0)
    nuf (argv+2, 1);
  else if (strcmp (argv[1], "real-nuf") == 0)
    nuf (argv+2, 0);
  else if (strcmp (argv[1], "raw-install") == 0)
    raw_install (argv+2);
  else if (strcmp (argv[1], "raw-remove") == 0)
    raw_remove (argv+2);
  else if (strcmp (argv[1], "remove-all") == 0)
    remove_all (argv+2);
  else if (strcmp (argv[1], "check") == 0)
    check ();
  else if (strcmp (argv[1], "fix") == 0)
    fix ();
  else
    usage ();

  return 0;
}
