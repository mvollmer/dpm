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
   types.  A dynamic value can be null, a string, a list, a
   dictionary, a function, or a object.

   Memory for dynamic values is handled semi-automatically via
   reference counting.  Refcounting works well with threads, is
   reasonably incremental, doesn't need to be tuned, the overhead is
   deemed tolerable for the mild use of this dynamic type system, and
   cycles will simply not be allowed.  Yep, the easy way out.
   
   A dynamic value has one or more owners.  A piece of code that calls
   dyn_ref becomes a owner, and the same piece is responsible for
   calling dyn_unref eventually.  Ownership is never implicitly
   transferred in a function call.  The caller of a function must
   guarantee that all dynamic values have at least one owner for the
   duration of the whole call.  Likewise, if a dynamic value is
   returned from a function call, the caller does not automatically
   become a owner.  The called function will give certain guarantees
   about the validity of the return dynamic value.  Usually, a dynamic
   value remains valid as long as some other value, or as long as the
   current dynamic extent.  The returned value is always guaranteed to
   be valid long enough for an immediate dyn_ref.

   If you want to explicitly create a reference from the current
   dynamic context to a value, use dyn_on_unwind_unref.  This is
   typically done in constructors.

   There is a standard external representation for dynamic values.
   This is useful for simple user interactions, such as in
   configuration files.  All dynamic values can be written out, but
   only strings, lists and dictionaries can be read back in.

   For kicks and completeness, there are also standard evaluation
   semantics for dynamic values, inspired by Scheme.  This is not
   intended to be useful, it's just there for paedagocic purposes, and
   because it is fun.  By adding some more primitive functions and
   numeric data types, a useful little language would result.

   You can mark a point in the call stack with dyn_catch, and you can
   directly return to such a point with dyn_throw.  When this happens,
   all intermediate dynamic extents are ended and their actions are
   executed.

   There are also little abstractions for streaming input and output.
   They do little more than manage a buffer on behalf of their client.
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

#define DYN_DECLARE_TYPE(_sym)         \
  typedef struct _sym##_struct *_sym;  \
  extern dyn_type _sym##_type[1];

#define DYN_DEFINE_TYPE(_sym, _name, _unref) \
  dyn_type _sym##_type[1] = {		     \
    .name = _name,                           \
    .unref = _unref                          \
  };					     \
  __attribute__ ((constructor))              \
  static void _sym##__register ()            \
  {                                          \
    dyn_type_register (_sym##_type);         \
  }

typedef void *dyn_val;

dyn_val dyn_alloc (dyn_type *type, size_t size);
dyn_val dyn_ref (dyn_val val);
void dyn_unref (dyn_val val);
void dyn_unref_on_unwind (dyn_val val);
dyn_val dyn_end_with (dyn_val val);

int dyn_is_string (dyn_val val);
int dyn_is_pair (dyn_val val);
int dyn_is_list (dyn_val val);
int dyn_is_dict (dyn_val val);
int dyn_is_func (dyn_val val);
int dyn_is_object (dyn_val val, dyn_type *type);
const char *dyn_type_name (dyn_val val);

const char *dyn_to_string (dyn_val val);
dyn_val dyn_from_string (const char *str);
dyn_val dyn_from_stringn (const char *str, int len);

dyn_val dyn_first (dyn_val val);
dyn_val dyn_rest (dyn_val val);
dyn_val dyn_cons (dyn_val first, dyn_val rest);

dyn_val dyn_get (dyn_val dict, dyn_val key);
dyn_val dyn_getp (dyn_val dict, dyn_val key, dyn_val position_keys);
dyn_val dyn_assoc (dyn_val dict, dyn_val key, dyn_val val);

dyn_val dyn_lambda (void (*func) (), void *data, void (*free) (void *data));
void (*dyn_func_func (dyn_val val))();
void *dyn_func_data (dyn_val val);

DYN_DECLARE_TYPE (dyn_input);
DYN_DECLARE_TYPE (dyn_output);

dyn_input dyn_open_file (const char *filename);
dyn_input dyn_open_string (const char *str, int len);
dyn_input dyn_open_zlib (dyn_input compressed);
dyn_input dyn_open_bz2 (dyn_input compressed);

void dyn_input_push_limit (dyn_input in, int len);
void dpm_input_pop_limit (dyn_input in);

void dyn_input_set_mark (dyn_input in);
char *dyn_input_mark (dyn_input in);

const char *dyn_input_pos (dyn_input in);
void dyn_input_set_pos (dyn_input in, const char *pos);
int dyn_input_grow (dyn_input in, int min);

void dyn_input_advance (dyn_input in, int n);
int dyn_input_find (dyn_input in, const char *delims);
int dyn_input_find_after (dyn_input in, const char *delims);
void dyn_input_skip (dyn_input in, const char *chars);
int dyn_input_looking_at (dyn_input in, const char *str);

dyn_output dyn_create_file (const char *filename);
void dyn_output_commit (dyn_output out);

int dyn_output_grow (dyn_output out, int min);
char *dyn_output_pos (dyn_output out);
void dyn_output_advance (dyn_output out, int n);

dyn_val dyn_read (dyn_input in);
void dyn_write (dyn_output out, const char *format, ...);
void dyn_writev (dyn_output out, const char *format, va_list args);

dyn_val dyn_eval (dyn_val form, dyn_val env);
dyn_val dyn_load (dyn_input in, dyn_val env);

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
