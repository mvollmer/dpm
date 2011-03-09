/*
 * Copyright (C) 2011 Marius Vollmer <marius.vollmer@gmail.com>
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

#include "pol.h"

dpm_version
dpm_pol_get_best_version (dpm_package pkg, 
			  bool (*accept) (dpm_version ver))
{
  dpm_version best = NULL;

  dyn_foreach_ (o, dpm_db_origins)
    dyn_foreach_ (v, dpm_db_origin_package_versions, o, pkg)
      {
	if (accept != NULL && !accept (v))
	  continue;

	if (best == NULL
	    || dpm_db_compare_versions (dpm_ver_version (v),
					dpm_ver_version (best)) > 0)
	  best = v;
      }

  return best;
}
