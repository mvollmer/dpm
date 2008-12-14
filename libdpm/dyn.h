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

   The dyn_begin and dyn_end functions delimit a dynamic extent.  They
   need to be called in a strictly nested fashion.  Within a dynamic
   extent, you can register certain actions that should be carried out
   when the dynamic extent ends.

   Dynamic extents can also be used together with dynamic variables.
   A dynamic variable is like a thread local variable, but it will
   revert to its previous value when a dynamic extent ends.

   You can directly return to an arbitrary point in the call stack
   with dyn_catch and dyn_throw.
 */

void dyn_begin ();
void dyn_end ();

void dyn_wind (void (*func) (int for_throw, void *data), void *data);

typedef struct {
  void *opaque[1];
} dyn_var;

void *dyn_get (dyn_var *var);
void dyn_set (dyn_var *var, void *value);
void dyn_let (dyn_var *var, void *value);

void dyn_free (void *mem);

typedef struct {
  const char *name;
  void (*free) (void *value);
  void (*uncaught) (void *value);
} dyn_condition;

void *dyn_catch (dyn_condition *condition,
		 void (*func) (void *data), void *data);

void dyn_throw (dyn_condition *condition, void *value);

#endif /* !DPM_DYN_H */
