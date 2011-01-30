#define _GNU_SOURCE

#include <string.h>
#include <stdbool.h>

#include "dpm.h"

void
usage ()
{
  fprintf (stderr, "Usage: dpm-tool [OPTIONS] update ORIGIN FILE\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] show [PACKAGE [VERSION]]\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] search STRING\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] tags EXPRESSION\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] relations PACKAGE\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] provides PACKAGE\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] stats\n");
  fprintf (stderr, "       dpm-tool [OPTIONS] dump\n");
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

  dyn_foreach_ (o, dpm_db_origins)
    {
      dyn_foreach_ (v, dpm_db_origin_package_versions, o, pkg)
	if (n_vo < 100)
	  {
	    vo[n_vo].ver = v;
	    vo[n_vo].org = o;
	    n_vo++;
	  }
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
      dyn_foreach_ (p, dpm_db_packages)
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
	  dyn_foreach_ (o, dpm_db_origins)
	    {
	      dyn_foreach_ (v, dpm_db_origin_package_versions, o, pkg)
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

  bool seen[dpm_db_package_max_id()];
  memset (seen, 0, sizeof(seen));

  dpm_version hits[dpm_db_version_max_id()];
  int n_hits = 0;

  dyn_foreach_ (v, dpm_db_versions)
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

void
dump (const char *origin)
{
  dpm_db_open ();
  dpm_ws_create ();

  dyn_foreach_iter (p, dpm_db_origin_packages, dpm_db_origin_find (origin))
    {
      dyn_foreach_ (v, ss_elts, p.versions)
	dpm_ws_add_cand (v);
    }

  dpm_ws_start ();
  dpm_ws_dump ();
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
  else if (strcmp (argv[1], "tags") == 0)
    tags (argv[2]);
  else if (strcmp (argv[1], "relations") == 0)
    list_reverse_relations (argv[2]);
  else if (strcmp (argv[1], "provides") == 0)
    list_provides (argv[2]);
  else if (strcmp (argv[1], "dump") == 0)
    dump (argv[2]);
  else
    usage ();

  return 0;
}
