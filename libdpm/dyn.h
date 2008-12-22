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
   dynamic extents, dynamic variables, and a simple dynamic type
   system with semi-automatic garbage collection.

   Nothing of this is particularily efficient.  Especially lists are
   horrible.  In fact, it's considerable less efficient than your
   typical dynamic language implementation, so use this sparingly.

   The dyn_begin and dyn_end functions delimit a dynamic extent.  They
   need to be called in a strictly nested fashion.  Within a dynamic
   extent, you can register certain actions that should be carried out
   when the dynamic extent ends.

   A dynamic variable is like a thread local variable, but it will
   revert to its previous value when a dynamic extent ends.

   Dynamic variables store "dynamic values", values with dynamic
   types.  A dynamic value can be nil, a string, a list, or a object.

   Memory for these values is handled semi-automatically via reference
   counting.  Refcounting works well with threads, is reasonably
   incremental, doesn't need to be tuned, the overhead is deemed
   tolerable for the mild use of this dynamic type system, and cycles
   will simply not be allowed.  Yep, the easy way out.

   A newly created dynamic value has a refcount of 1.  If the current
   dynamic extent ends exceptionally (via a throw), it is unreffed.

   You can directly return to an arbitrary point in the call stack
   with dyn_catch and dyn_throw.
 */

void dyn_begin ();
void dyn_end ();

void dyn_on_unwind (void (*func) (int for_throw, void *data), void *data);

struct dyn_type {
  const char *name;
  void (*unref) (struct dyn_type *type, void *object);
};

typedef struct dyn_type dyn_type;

int dyn_type_register (dyn_type *type);

typedef void *dyn_val;

dyn_val *dyn_alloc (int tag, size_t size);
dyn_val *dyn_ref (dyn_val val);
void dyn_unref (dyn_val val);
void dyn_unref_loc_on_unwind (dyn_val *var);

int dyn_is_null (dyn_val *val);
int dyn_is_string (dyn_val *val);
int dyn_is_pair (dyn_val *val);
int dyn_is_list (dyn_val *val);
int dyn_is_object (dyn_val *val, dyn_type *type);

const char *dyn_to_string (dyn_val *val);
dyn_val *dyn_from_string (const char *str);

dyn_val *dyn_first (dyn_val *val);
dyn_val *dyn_rest (dyn_val *val);
dyn_val *dyn_cons (dyn_val *first, dyn_val *rest);

typedef struct {
  dyn_val *val;
} dyn_var;

dyn_val dyn_get (dyn_var *var);
void dyn_set (dyn_var *var, dyn_val val);
void dyn_let (dyn_var *var, dyn_val val);

typedef struct {
  const char *name;
  void (*uncaught) (dyn_val value);
} dyn_condition;

dyn_val dyn_catch (dyn_condition *condition,
		   void (*func) (void *data), void *data);
void dyn_throw (dyn_condition *condition, dyn_val value);
void dyn_signal (dyn_condition *condition, dyn_val value);

#endif /* !DPM_DYN_H */
