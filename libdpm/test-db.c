#include "conf.h"
#include "db.h"
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
update ()
{
  dyn_val srcs = dpm_conf_get (sources);
  dyn_val dists = dpm_conf_get (distributions);
  dyn_val comps = dpm_conf_get (components);
  dyn_val archs = dpm_conf_get (architectures);

  dpm_db_open ();
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
	  dyn_print ("%r:", pkg);
	  if (versions)
	    for (int i = 0; i < ss_len (versions); i++)
	      {
		dpm_version ver = ss_ref (versions, i);
		dyn_print (" %r (%r)", ss_ref (ver, 1), ss_ref (ver, 2));
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
      desc = dpm_db_version_shortdesc (ss_ref (versions, 0));
    
    dyn_print ("%r - %r\n", pkg, desc);
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

static void
list_versions (ss_val versions)
{
  if (versions)
    for (int i = 0; i < ss_len (versions); i++)
      {
	dpm_version ver = ss_ref (versions, i);
	dyn_print ("%r %r (%r) - %r\n",
		   ss_ref (ver, 0),
		   ss_ref (ver, 1),
		   ss_ref (ver, 2),
		   dpm_db_version_shortdesc (ver));
      }
}

void
query (const char *exp)
{
  if (exp)
    {
      dpm_db_open ();
      list_versions (dpm_db_query_tag (exp));
      dpm_db_done ();
    }
}

int
main (int argc, char **argv)
{
  if (argc < 2)
    usage ();

  if (dyn_file_exists ("test-db.conf"))
    dpm_conf_parse ("test-db.conf");

  if (strcmp (argv[1], "update") == 0)
    update ();
  else if (strcmp (argv[1], "show") == 0)
    show (argv[2]);
  else if (strcmp (argv[1], "stats") == 0)
    stats ();
  else if (strcmp (argv[1], "search") == 0)
    search (argv[2]);
  else if (strcmp (argv[1], "query") == 0)
    query (argv[2]);
  else
    usage ();

  return 0;
}
