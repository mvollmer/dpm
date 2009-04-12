/*
 * Copyright (C) 2008, 2009 Marius Vollmer <marius.vollmer@gmail.com>
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

#ifndef DPM_ACQ_H
#define DPM_ACQ_H

#include "dyn.h"

/* Acquiring things.

   For now, we simply use wget.  We also need some kind of
   transactions so that only if all files are downloaded successfully,
   any of them will overwrite what we already have.
 */

typedef enum {
  DPM_ACQ_NOT_FOUND,
  DPM_ACQ_CHANGED,
  DPM_ACQ_UNCHANGED
} dpm_acq_code;

dpm_acq_code dpm_acquire (const char *filename);

dyn_input dpm_acq_open (const char *filename);
dyn_input dpm_acq_open_local (const char *filename);

#endif /* !DPM_ACQ_H */
