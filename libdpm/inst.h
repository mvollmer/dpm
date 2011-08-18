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

#ifndef DPM_INST_H
#define DPM_INST_H

#include "dyn.h"
#include "store.h"
#include "db.h"

/* Installing and removing packages.

   This does nothing for now except recording the action in the
   database.
*/

bool dpm_inst_can_unpack (dpm_version ver);
bool dpm_inst_can_install (dpm_version ver);
bool dpm_inst_can_remove (dpm_package pkg);

void dpm_inst_unpack (dpm_version ver);
void dpm_inst_install (dpm_version ver);
void dpm_inst_remove (dpm_package pkg);

#endif /* !DPM_INST_H */
