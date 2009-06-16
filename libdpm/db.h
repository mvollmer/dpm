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

#ifndef DPM_DB_H
#define DPM_DB_H

#include "dyn.h"
#include "store.h"

/* Dpm stores all information in a single struct-store, in a specific
   format.  This store is called the Dpm database. These are the
   functions for creating and accessing the database.

   The database stores "package installation status" and "package
   version information" records.

   The information for a package version contains everything that is
   known about a deb file, from the deb itself and from the repository
   indices: its description, its dependencies, its list of files,
   where the deb can be found, etc.  This information is stored in a
   record.  Updating this information is done by creating new records.

   The status for a package contains everything that is specific to
   the local installation: which version is installed if any, which
   versions are available, which files are installed (this can be
   different from the list of files in the package), etc.  The status
   is not available in a single record, it is too volatile for this.
   Instead, a set of accessor functions is used to maintain it
 */

DYN_DECLARE_TYPE (dpm_db);
dpm_db dpm_db_current ();

typedef ss_val dpm_package;

#define dpm_pkg_id(v)           ss_ref_int(v,0)
#define dpm_pkg_name(v)         ss_ref(v,1)

typedef ss_val dpm_version;

#define dpm_ver_id(v)           ss_ref_int(v,0)
#define dpm_ver_package(v)      ss_ref(v,1)
#define dpm_ver_version(v)      ss_ref(v,2)
#define dpm_ver_architecture(v) ss_ref(v,3)
#define dpm_ver_relations(v)    ss_ref(v,4)
#define dpm_ver_tags(v)         ss_ref(v,5)
#define dpm_ver_shortdesc(v)    ss_ref(v,6)
#define dpm_ver_fields(v)       ss_ref(v,7)

typedef ss_val dpm_relations;

#define dpm_rels_pre_depends(r) ss_ref(r,0)
#define dpm_rels_depends(r)     ss_ref(r,1)
#define dpm_rels_conflicts(r)   ss_ref(r,2)
#define dpm_rels_provides(r)    ss_ref(r,3)
#define dpm_rels_replaces(r)    ss_ref(r,4)
#define dpm_rels_breaks(r)      ss_ref(r,5)
#define dpm_rels_recommends(r)  ss_ref(r,6)
#define dpm_rels_enhances(r)    ss_ref(r,7)
#define dpm_rels_suggests(r)    ss_ref(r,8)

typedef ss_val dpm_relation;

#define dpm_rel_op(r,i)         ss_ref_int((r),(i))
#define dpm_rel_package(r,i)    ss_ref((r),(i)+1)
#define dpm_rel_version(r,i)    ss_ref((r),(i)+2)

enum {
  DPM_ANY,
  DPM_EQ,
  DPM_LESS,
  DPM_LESSEQ,
  DPM_GREATER,
  DPM_GREATEREQ
};

int dpm_db_compare_versions (ss_val a, ss_val b);
int dpm_db_check_versions (ss_val a, int op, ss_val b);

void dpm_db_open ();
void dpm_db_checkpoint ();
void dpm_db_done ();

ss_val dpm_db_intern (const char *string);

void dpm_db_full_update (dyn_val sources, dyn_val dists,
			 dyn_val comps, dyn_val archs);

void dpm_db_maybe_full_update (dyn_val sources, dyn_val dists,
			       dyn_val comps, dyn_val archs);

void dpm_db_set_installed (dpm_package pkg, dpm_version ver);

int dpm_db_package_count ();
int dpm_db_version_count ();

void dpm_db_foreach_package (void (*func) (dpm_package pkg));

dpm_package dpm_db_find_package (const char *name);

ss_val      dpm_db_available (dpm_package pkg);
dpm_version dpm_db_installed (dpm_package pkg);
dpm_version dpm_db_candidate (dpm_package pkg);

ss_val dpm_db_version_get (dpm_version ver, const char *field);
ss_val dpm_db_version_shortdesc (dpm_version ver);

ss_val dpm_db_query_tag (const char *tag);
ss_val dpm_db_reverse_relations (dpm_package pkg);

void dpm_db_show_version (dpm_version ver);

void dpm_db_stats ();

#endif /* !DPM_DB_H */
