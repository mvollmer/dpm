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

/* Dpm stores all permanent information in a single struct-store, in a
   specific format.  This store is called the Dpm database. These are
   the functions for creating and accessing the database.

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

extern dyn_var dpm_database_name[1];

DYN_DECLARE_TYPE (dpm_db);
dpm_db dpm_db_current ();

int dpm_db_compare_versions (ss_val a, ss_val b);
int dpm_db_compare_versions_str (ss_val a, const char *b, int b_len);
int dpm_db_check_versions (ss_val a, int op, ss_val b);
int dpm_db_check_versions_str (ss_val a, int op, const char *b, int b_len);

void dpm_db_open ();
void dpm_db_checkpoint ();
void dpm_db_done ();
void dpm_db_gc_and_done ();

int dpm_db_package_id_limit ();
int dpm_db_version_id_limit ();

ss_val dpm_db_intern (const char *string);

void dpm_db_stats ();

/* Packages
 */

typedef ss_val dpm_package;

#define dpm_pkg_id(v)           ss_ref_int(v,0)
#define dpm_pkg_name(v)         ss_ref(v,1)

dpm_package dpm_db_package_find (const char *name);

DYN_DECLARE_STRUCT_ITER (dpm_package, dpm_db_packages)
{
  dpm_db db;
  ss_dict_entries packages;
  dpm_package package;
};

/* Versions
 */

typedef ss_val dpm_version;

#define dpm_ver_id(v)           ss_ref_int(v,0)
#define dpm_ver_package(v)      ss_ref(v,1)
#define dpm_ver_version(v)      ss_ref(v,2)
#define dpm_ver_architecture(v) ss_ref(v,3)
#define dpm_ver_relations(v)    ss_ref(v,4)
#define dpm_ver_tags(v)         ss_ref(v,5)
#define dpm_ver_shortdesc(v)    ss_ref(v,6)
#define dpm_ver_fields(v)       ss_ref(v,7)
#define dpm_ver_checksum(v)     ss_ref(v,8)

typedef ss_val dpm_relations;

enum {
  DPM_PRE_DEPENDS,
  DPM_DEPENDS, 
  DPM_CONFLICTS,
  DPM_PROVIDES,
  DPM_REPLACES,
  DPM_BREAKS,
  DPM_RECOMMENDS,
  DPM_ENHANCES,
  DPM_SUGGESTS,
  DPM_NUM_RELATION_TYPES
};

#define dpm_rels_pre_depends(r) ss_ref(r,DPM_PRE_DEPENDS)
#define dpm_rels_depends(r)     ss_ref(r,DPM_DEPENDS)
#define dpm_rels_conflicts(r)   ss_ref(r,DPM_CONFLICTS)
#define dpm_rels_provides(r)    ss_ref(r,DPM_PROVIDES)
#define dpm_rels_replaces(r)    ss_ref(r,DPM_REPLACES)
#define dpm_rels_breaks(r)      ss_ref(r,DPM_BREAKS)
#define dpm_rels_recommends(r)  ss_ref(r,DPM_RECOMMENDS)
#define dpm_rels_enhances(r)    ss_ref(r,DPM_ENHANCES)
#define dpm_rels_suggests(r)    ss_ref(r,DPM_SUGGESTS)

typedef ss_val dpm_relation;

#define dpm_rel_op(r,i)         ss_ref_int((r),(i))
#define dpm_rel_package(r,i)    ss_ref((r),(i)+1)
#define dpm_rel_version(r,i)    ss_ref((r),(i)+2)

int dpm_rel_type (dpm_relation r);

DYN_DECLARE_STRUCT_ITER (void, dpm_db_alternatives, dpm_relation rel)
{
  ss_val rel;
  int i;

  int op;
  dpm_package package;
  ss_val version;
};

enum {
  DPM_ANY,
  DPM_EQ,
  DPM_LESS,
  DPM_LESSEQ,
  DPM_GREATER,
  DPM_GREATEREQ
};

ss_val dpm_db_version_get (dpm_version ver, const char *field);
ss_val dpm_db_version_shortdesc (dpm_version ver);

void dpm_db_version_show (dpm_version ver);

DYN_DECLARE_STRUCT_ITER (dpm_version, dpm_db_versions)
{
  dpm_db db;
  ss_tab_entries versions;
};

/* Origins
 */

typedef ss_val dpm_origin;

#define dpm_origin_label(o) o

dpm_origin dpm_db_origin_find (const char *label);

DYN_DECLARE_STRUCT_ITER (dpm_origin, dpm_db_origins)
{
  dpm_db db;
  ss_dict_entries origins;
  dpm_origin origin;
};

DYN_DECLARE_STRUCT_ITER (dpm_package, dpm_db_origin_packages, dpm_origin)
{
  dpm_db db;
  ss_dict *dict;
  ss_dict_entries packages;
  dpm_package package;
  ss_val versions;
};

DYN_DECLARE_STRUCT_ITER (dpm_version, dpm_db_origin_package_versions,
                         dpm_origin, dpm_package)
{
  dpm_db db;
  ss_elts versions;
};

void dpm_db_origin_update (dpm_origin origin,
			   dyn_input in);

/* Indexed queries
 */

ss_val dpm_db_query_tag (const char *tag);
ss_val dpm_db_reverse_relations (dpm_package pkg);
ss_val dpm_db_provides (dpm_package pkg);

/* Status
 */

typedef ss_val dpm_status;

#define dpm_stat_version(s)  ss_ref (s, 0)
#define dpm_stat_status(s)   ss_ref_int (s, 1)
#define dpm_stat_flags(s)    ss_ref_int (s, 2)

// All the tricky half-states come later...
#define DPM_STAT_OK        0
#define DPM_STAT_UNPACKED  1

#define DPM_STAT_MANUAL    0x01

void dpm_db_set_status (dpm_package pkg, dpm_version ver, int status);
void dpm_db_set_status_flags (dpm_package pkg, int flags);
dpm_status dpm_db_status (dpm_package pkg);

/* Dumping
 */

void dpm_dump_relation (dpm_relation rel);


#endif /* !DPM_DB_H */
