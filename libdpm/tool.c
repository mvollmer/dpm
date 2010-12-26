#define _GNU_SOURCE

#include <string.h>
#include <stdbool.h>

#include "dpm.h"

void
usage ()
{
  fprintf (stderr, "Usage: dpm-tool [OPTIONS] update ORIGIN FILE\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] show [PACKAGE [VERSION]]\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] stats\n");
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
show (const char *package, const char *version)
{
  dpm_db_open ();

  if (package == NULL)
    {
      dyn_foreach_ (p, dpm_db_packages)
        dyn_print ("%r\n", dpm_pkg_name (p));
    }
  else
    {
      dpm_package pkg = dpm_db_package_find (package);
      ss_val interned_version = version? dpm_db_intern (version) : NULL;

      dyn_foreach_ (o, dpm_db_origins)
        {
          bool origin_shown = false;
          bool need_blank_line = false;
          dyn_foreach_ (v, dpm_db_origin_package_versions, o, pkg)
            {
              if (version == NULL)
                {
                  if (!origin_shown)
                    dyn_print ("From %r:\n", o);
                  origin_shown = true;
                  dyn_print ("  %r %r\n",
                             dpm_pkg_name (dpm_ver_package (v)),
                             dpm_ver_version (v));
                }
              else if (dpm_ver_version (v) == interned_version)
                {
                  if (need_blank_line)
                    dyn_print ("\n");
                  dpm_db_version_show (v);
                  need_blank_line = true;
                }
            }
        }
    }

  dpm_db_done ();
}

void
search (const char *pattern)
{
#if 0
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
#endif
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

      dpm_package pkg = dpm_db_package_find (package);
      ss_val versions = dpm_db_reverse_relations (pkg);
      if (versions)
	list_versions (versions, pkg);
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
      dpm_package pkg = dpm_db_package_find (package);
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
  else if (strcmp (argv[1], "query") == 0)
    query (argv[2]);
  else if (strcmp (argv[1], "reverse") == 0)
    list_reverse_relations (argv[2]);
  else if (strcmp (argv[1], "provides") == 0)
    list_provides (argv[2]);
  else
    usage ();

  return 0;
}
