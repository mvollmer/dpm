#define _GNU_SOURCE

#include <string.h>

#include "conf.h"
#include "db.h"
#include "alg.h"
#include "acq.h"

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
show (const char *package)
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
		  ver_to_show = ver;
	      }
	  dyn_print ("\n");
	}

      if (ver_to_show)
	dpm_db_show_version (ver_to_show);

      dpm_db_done ();
    }
}

static void
search_package (dpm_package pkg, void *data)
{
  const char *pattern = data;
  int pattern_len = strlen (pattern);

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

void
search (const char *pattern)
{
  if (pattern)
    {
      dpm_db_open ();
      dpm_db_foreach_package (search_package, (void *)pattern);
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
show_filtered_relations (const char *field, ss_val rels, dpm_package pkg)
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
list_versions (ss_val versions, dpm_package rev)
{
  if (versions)
    for (int i = 0; i < ss_len (versions); i++)
      {
	dpm_version ver = ss_ref (versions, i);
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
      list_versions (dpm_db_query_tag (exp), NULL);
      dpm_db_done ();
    }
}

void
list_reverse_relations (const char *package)
{
  if (package)
    {
      dpm_db_open ();
      dpm_package pkg = dpm_db_find_package (package);
      ss_val versions = dpm_db_reverse_relations (pkg);
      if (versions)
	list_versions (versions, pkg);
      dpm_db_done ();
    }
}

void install_pkg (dpm_package pkg);

void
try_fix (int conflict, dpm_relation rel, void *unused)
{
  // Ignore conflicts and only install the first of an alternative.

  if (conflict)
    return;

#if 0
  dyn_print ("Fixing ");
  show_relation_part (rel, 0);
  dyn_print ("\n");
#endif

  dpm_package target = dpm_rel_package (rel, 0);
  if (!dpm_ws_is_flagged (target))
    install_pkg (target);
}

void
install_pkg (dpm_package pkg)
{
  dpm_ws_flag (pkg);
  dpm_ws_set_installed (pkg, dpm_db_candidate (pkg));
  dpm_ws_do_broken (pkg, try_fix, NULL);
  dpm_ws_unflag (pkg);
}

void
fun (char **argv)
{
  dyn_begin ();

  dpm_db_open ();
  dpm_ws_create ();

  for (int i = 0; argv[i]; i++)
    {
      dpm_package pkg = dpm_db_find_package (argv[i]);
      if (pkg)
	install_pkg (pkg);
      else
	dyn_print ("Package %s not found\n", argv[i]);
    }
  
  dpm_ws_report ();

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

  if (strcmp (argv[1], "update") == 0)
    update (0);
  else if (strcmp (argv[1], "force-update") == 0)
    update (1);
  else if (strcmp (argv[1], "show") == 0)
    show (argv[2]);
  else if (strcmp (argv[1], "stats") == 0)
    stats ();
  else if (strcmp (argv[1], "search") == 0)
    search (argv[2]);
  else if (strcmp (argv[1], "query") == 0)
    query (argv[2]);
  else if (strcmp (argv[1], "reverse") == 0)
    list_reverse_relations (argv[2]);
  else if (strcmp (argv[1], "fun") == 0)
    fun (argv+2);
  else
    usage ();

  return 0;
}
