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

#ifndef DPM_DYN_H
#define DPM_DYN_H

/* Some mild dynamic language features for C: non-local control flow,
   dynamic extents, and useful global state.  Sadly, no garbage
   collection.  Maybe later.

   The dpm_dyn_begin and dpm_dyn_end function delimit a dynamic
   extent.  They need to be called in a strictly nested fashion.
   Within a dynamic extent, you can register certain actions that
   should be carried out when the dynamic extent ends.  This is useful
   for cleanup actions.

   Dynamic extents can also be use together with dynamic variables.  A
   dynamic variable is like a thread local variable, but it will
   revert to its previous value when a dynamic extent ends.

   You can directly return to an arbitrary point in the call stack
   with dpm_dyn_throw and dpm_dyn_error.  These functions will return
   control to the most recent scm_dyn_catch.  Such a catch point is
   also delimited by dynamic extents.

   Before returning control, all intervening dynammic extents are
   ended.
 */

void dpm_dyn_begin ();
void dpm_dyn_end ();

void dpm_dyn_wind (void (*func) (int for_throw, void *data), void *data);

typedef struct {
  void *opaque[1];
} dpm_dyn_var;

void *dpm_dyn_get (dpm_dyn_var *var);
void dpm_dyn_set (dpm_dyn_var *var, void *value);
void dpm_dyn_let (dpm_dyn_var *var, void *value);

void dpm_dyn_free (void *mem);

char *dpm_dyn_catch (void (*func) (void *data), void *data);

void dpm_dyn_throw (char *message);
void dpm_error (const char *fmt, ...);

#endif /* !DPM_DYN_H */
