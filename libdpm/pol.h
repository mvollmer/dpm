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

#ifndef DPM_POL_H
#define DPM_POL_H

#include "dyn.h"
#include "db.h"

/* Policy
 */

extern dyn_var dpm_pol_origin[1];

dpm_version dpm_pol_get_best_version (dpm_package pkg,
				      bool (*accept) (dpm_version ver));

#endif /* !DPM_POL_H */
