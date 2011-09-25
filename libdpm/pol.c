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

dyn_var dpm_pol_origin[1];

dpm_version
dpm_pol_get_best_version (dpm_package pkg, 
			  bool (*accept) (dpm_version ver))
{
  /* XXX - this better be fast, which it isn't right now.
   */

  dyn_val origin = dyn_get (dpm_pol_origin);
  int origin_len = origin? strlen (origin) : 0;

  dpm_version best = NULL;
  int best_score = 0;

  dyn_foreach (o, dpm_db_origins)
    dyn_foreach (v, dpm_db_origin_package_versions, o, pkg)
      {
	if (accept != NULL && !accept (v))
	  continue;

	int score = 0;
	if (origin
	    && ss_equal_blob (dpm_origin_label (o), origin_len, origin))
	  score = 500;

	if (best == NULL
	    || score > best_score
	    || (score == best_score
		&& dpm_db_compare_versions (dpm_ver_version (v),
					    dpm_ver_version (best)) > 0))
	  {
	    best = v;
	    best_score = score;
	  }
      }

  return best;
}
