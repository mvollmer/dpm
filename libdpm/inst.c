/*
 * Copyright (C) 2009 Marius Vollmer <marius.vollmer@gmail.com>
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

#include "dyn.h"
#include "db.h"
#include "inst.h"

bool
dpm_inst_can_unpack (dpm_version ver)
{
  return true;
}

bool
dpm_inst_can_install (dpm_version ver)
{
  return true;
}

static void
dpm_inst_unpack_or_setup (dpm_version ver, bool unpack)
{
  dpm_package pkg = dpm_ver_package (ver);
  dpm_status status = dpm_db_status (pkg);

  const char *msg = "";
  if (unpack)
    msg = " (unpack)";
  else if (dpm_stat_status (status) & DPM_STAT_UNPACKED)
    msg = " (setup)";

  if (dpm_stat_version (status))
    {
      ss_val old_version = dpm_ver_version (dpm_stat_version (status));
      ss_val new_version = dpm_ver_version (ver);
      int cmp = dpm_db_compare_versions (new_version, old_version);

      if (cmp > 0)
	dyn_print ("Upgrading %r %r to version %r%s\n",
		   dpm_pkg_name (pkg),
		   old_version,
		   new_version,
		   msg);
      else if (cmp < 0)
	dyn_print ("Downgrading %r %r to version %r%s\n",
		   dpm_pkg_name (pkg),
		   old_version,
		   new_version,
		   msg);
      else
	dyn_print ("Reinstalling %r %r%s\n",
		   dpm_pkg_name (pkg),
		   old_version,
		   msg);
    }
  else
    dyn_print ("Installing %r %r%s\n", 
	       dpm_pkg_name (pkg),
	       dpm_ver_version (ver),
	       msg);

  dpm_db_set_status (pkg, ver, unpack? DPM_STAT_UNPACKED : DPM_STAT_OK);
}

void
dpm_inst_unpack (dpm_version ver)
{
  dpm_inst_unpack_or_setup (ver, true);
}

void
dpm_inst_install (dpm_version ver)
{
  dpm_inst_unpack_or_setup (ver, false);
}

void
dpm_inst_remove (dpm_package pkg)
{
  dpm_status status = dpm_db_status (pkg);

  if (dpm_stat_version (status))
    dyn_print ("Removing %r %r\n",
	       dpm_pkg_name (pkg),
	       dpm_ver_version (dpm_stat_version (status)));
  else
    dyn_print ("No need to remove %r, it is not installed\n",
	       dpm_pkg_name (pkg));

  dpm_db_set_status (pkg, NULL, DPM_STAT_OK);
}

void
dpm_inst_set_manual (dpm_package pkg, bool manual)
{
  dpm_db_set_status_flags (pkg, manual? DPM_STAT_MANUAL : 0);
}
