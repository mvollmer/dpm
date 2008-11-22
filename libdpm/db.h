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
   Instead, a set of accessor functions is used to maintain it.

   
   Most functions work on the 'current database'.  The current
   database is kept in a dynamic variable, and can thus be controlled
   with dpm_dyn_begin and dpm_dyn_end.
 */

void dpm_db_begin ();
void dpm_db_checkpoint ();
void dpm_db_end ();

void dpm_db_import_apt_sources ();

typedef ss_val dpm_package;
typedef ss_val dpm_version;
typedef ss_val dpm_vector;

dpm_package dpm_db_find_package (const char *name);
dpm_vector  dpm_db_get_available (dpm_package pkg);

ss_val dpm_db_get_field (dpm_version ver);

#endif /* !DPM_DB_H */
