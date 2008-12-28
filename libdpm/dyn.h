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
   types.  A dynamic value can be null, a string, a list, a function,
   or a object.

   You can directly return to an arbitrary point in the call stack
   with dyn_catch and dyn_throw.

   Memory for dynamic values is handled semi-automatically via
   reference counting.  Refcounting works well with threads, is
   reasonably incremental, doesn't need to be tuned, the overhead is
   deemed tolerable for the mild use of this dynamic type system, and
   cycles will simply not be allowed.  Yep, the easy way out.
   
   To allow nicer code with less explicit ref/unref calls, newly
   created dynamic values are owned by the current dynamic extent.  A
   new value has a reference count of one, but this count represents
   the reference from the dynamic extent to the value, not the
   reference returned from the constructor function to the value.

   This allows to collect the dynamic values that haven't found their
   home yet when the dynamic extent ends prematurely, for example.

   If you want to explicitly create a reference from the current
   dynamic context to a value, use dyn_on_unwind_unref.  This is
   typically done in constructors.

   As a coding guideline, ownership of references to dynamic values is
   never transferred implicitly.  Or in other words, dyn_ref and
   dyn_unref is always called in pairs by a single party (where
   dyn_alloc counts as dyn_ref).  For example, when a dynamic value is
   passed as a parameter, the called function must always use dyn_ref
   if it needs the value to stay alive longer than the call, and must
   consequently call dyn_unref eventually.  Likewise, a dynamic value
   returned by a function is not owned by the caller.  The lifetime of
   the returned value is determined by the rules of the function that
   returns the value.  In general, it stays alive long enough for an
   immediate dyn_ref call if the value has at least one reference
   owned by the current thread.  Usually, the value stays alive for
   the current dynamic extent.
 */

void dyn_begin ();
void dyn_end ();

void dyn_on_unwind (void (*func) (int for_throw, void *data), void *data);

struct dyn_type {
  int tag;
  const char *name;
  void (*unref) (struct dyn_type *type, void *object);
};

typedef struct dyn_type dyn_type;

void dyn_type_register (dyn_type *type);

typedef void *dyn_val;

dyn_val dyn_alloc (dyn_type *type, size_t size);
dyn_val dyn_ref (dyn_val val);
void dyn_unref (dyn_val val);
void dyn_unref_on_unwind (dyn_val val);
dyn_val dyn_end_with (dyn_val val);

int dyn_is_string (dyn_val val);
int dyn_is_pair (dyn_val val);
int dyn_is_list (dyn_val val);
int dyn_is_func (dyn_val val);
int dyn_is_object (dyn_val val, dyn_type *type);
const char *dyn_type_name (dyn_val val);

const char *dyn_to_string (dyn_val val);
dyn_val dyn_from_string (const char *str);
dyn_val dyn_from_stringn (const char *str, int len);

dyn_val dyn_first (dyn_val val);
dyn_val dyn_rest (dyn_val val);
dyn_val dyn_cons (dyn_val first, dyn_val rest);

dyn_val dyn_lambda (void (*func) (), void *data, void (*free) (void *data));
void (*dyn_func_func (dyn_val val))();
void *dyn_func_data (dyn_val val);

typedef struct {
  dyn_val val;
} dyn_var;

dyn_val dyn_get (dyn_var *var);
void dyn_set (dyn_var *var, dyn_val val);
void dyn_let (dyn_var *var, dyn_val val);

void dyn_on_unwind_free (void *mem);

typedef struct {
  const char *name;
  dyn_var handler;
  void (*uncaught) (dyn_val value);
  void (*unhandled) (dyn_val value);
} dyn_condition;

dyn_val dyn_catch (dyn_condition *condition,
		   void (*func) (void *data), void *data);
__attribute__ ((noreturn)) 
void dyn_throw (dyn_condition *condition, dyn_val value);
__attribute__ ((noreturn)) 
void dyn_signal (dyn_condition *condition, dyn_val value);
void dyn_let_handler (dyn_condition *condition, dyn_val handler);

#endif /* !DPM_DYN_H */
