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

#include <stdarg.h>

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

void *dyn_malloc (size_t size);
void *dyn_realloc (void *old, size_t size);
void *dyn_strdup (const char *str);
void *dyn_strndup (const char *str, int n);

void dyn_begin ();
void dyn_end ();

void dyn_on_unwind (void (*func) (int for_throw, void *data), void *data);

struct dyn_type {
  int tag;
  const char *name;
  void (*unref) (struct dyn_type *type, void *object);
  int (*equal) (void *a, void *b);
};

typedef struct dyn_type dyn_type;

void dyn_type_register (dyn_type *type);

#define DYN_DECLARE_TYPE(_sym)                 \
  typedef struct _sym##_struct *_sym;          \
  extern dyn_type _sym##_type[1];

#define DYN_DEFINE_TYPE(_sym, _name)         \
  dyn_type _sym##_type[1] = { {		     \
    .name = _name,                           \
    .unref = _sym##_unref,		     \
    .equal = _sym##_equal                    \
  } };	                                     \
  __attribute__ ((constructor))              \
  static void _sym##__register ()            \
  {                                          \
    DYN_ENSURE_TYPE(_sym);                   \
  }

#define DYN_ENSURE_TYPE(_sym)                \
    dyn_type_register (_sym##_type)

typedef void *dyn_val;

dyn_val dyn_alloc (dyn_type *type, size_t size);
dyn_val dyn_ref (dyn_val val);
void dyn_unref (dyn_val val);
void dyn_unref_on_unwind (dyn_val val);
dyn_val dyn_end_with (dyn_val val);

#define dyn_new(_sym) ((_sym)dyn_alloc (_sym##_type, \
                                        sizeof (struct _sym##_struct)))

DYN_DECLARE_TYPE (dyn_string);
DYN_DECLARE_TYPE (dyn_pair);
DYN_DECLARE_TYPE (dyn_dict);
DYN_DECLARE_TYPE (dyn_func);

int dyn_is (dyn_val val, dyn_type *type);
const char *dyn_type_name (dyn_val val);

int dyn_is_string (dyn_val val);
const char *dyn_to_string (dyn_val val);
dyn_val dyn_from_string (const char *str);
dyn_val dyn_from_stringn (const char *str, int len);

int dyn_is_pair (dyn_val val);
int dyn_is_list (dyn_val val);
dyn_val dyn_first (dyn_val val);
dyn_val dyn_rest (dyn_val val);
dyn_val dyn_cons (dyn_val first, dyn_val rest);

int dyn_is_dict (dyn_val val);
dyn_val dyn_lookup (dyn_val dict, dyn_val key);
dyn_val dyn_assoc (dyn_val dict, dyn_val key, dyn_val val);

int dyn_is_func (dyn_val val);
dyn_val dyn_lambda (void (*func) (), void *data, void (*free) (void *data));
void (*dyn_func_func (dyn_val val))();
void *dyn_func_data (dyn_val val);

int dyn_equal (dyn_val a, dyn_val b);
int dyn_eq (dyn_val a, const char *b);

typedef struct {
  void *opaque[4];
} dyn_list_builder[1];

void dyn_list_start (dyn_list_builder builder);
void dyn_list_append (dyn_list_builder builder, dyn_val val);
dyn_val dyn_list_finish (dyn_list_builder builder);

#define DYN_EOL ((dyn_val)-1)

dyn_val dyn_list (dyn_val first, ...);

/* Schemas.

   A schema is a little grammar for dynamic values, represented as a
   dynamic value itself.  Applying a schema to a dynamic value
   validates that value against the schema, and maybe provides
   defaults for missing values.  Thus, after applying a schema to a
   value, you can be sure of its structure and don't need to check
   yourself when you traverse it.

   - any, null, string, dict, func, pair, NAME
   A value of the given type. "any" will match any type, including
   null.

   - (value VALUE)
   A value matching VALUE exactly.

   - (list SCHEMA1 SCHEMA2 ... [...])
   List with elements matching SCHEMA1, SCHEMA2 etc.  If the "..."
   element is present, the list can be any length and the last SCHEMA
   is used for the remaining elements. If the input list is too short,
   it is padded with null values.

   - (dict (KEY1 SCHEMA1) (KEY2 SCHEMA2) ... [...])
   A dict where the values match the given SCHEMAs.  If the "..." is
   present, the dict can have additional keys.  Otherwise any
   additional keys are an error.

   - (defaulted SCHEMA VALUE)
   Either null, or a value matching SCHEMA.  In the former case, VALUE
   is used for the result.  The test for null is done before checking
   SCHEMA.

   - (not SCHEMA)
   A value that does not match SCHEMA.

   - (or SCHEMA1 SCHEMA2 ...)
   A value matching any of the SCHEMAs.

   - (if SCHEMA1 VALUE1 SCHEMA2 VALUE2 ... [SCHEMAN])
   If the value matches SCHEMA1, VALUE1 is the result; and so on.  If
   none of the SCHEMAs match, and there is a last SCHEMAN, the value
   is matched against that schema.  If SCHEMAN is not present, the
   matching fails.

   - (let (NAME1 SCHEMA1) 
          ...
        SCHEMA)
   A value matching SCHEMA, where SCHEMA1 can be referenced as NAME1,
   etc.  See 'schema'.  The binding is recursive: SCHEMA1 etc can
   refer to NAME1 etc itself.

   - (schema NAME)
   A value matching the schema named NAME.  See 'let'.   

   
   The schema of schemas, as an example:

   (let ((schema
          (or string
              (list (value value) any)
              (list (value list) (or (schema schema) (value ...)) ...)
              (list (value dict) (or (list string (schema schema))
	                             (value ...))
                                 ...)
              (list (value defaulted) (schema schema) any)
              (list (value not) (schema schema))
              (list (value or) (schema schema) ...)
              (list (value if) (or (schema schema) any) ...)
              (list (value let) (or (list string (schema schema))
	                            (schema schema))
                                ...)
              (list (value schema) string))))
     schema)

   As you can see, this schema allows values that are not valid
   schemas, such as '(list any ... string)'.  The schema grammar can
   not express that there must be at most one '...' and that it must
   be last.  This must be dealt with when processing the schema.
 */

dyn_val dyn_apply_schema (dyn_val val, dyn_val schema);

DYN_DECLARE_TYPE (dyn_input);
DYN_DECLARE_TYPE (dyn_output);

dyn_input dyn_open_file (const char *filename);
dyn_input dyn_open_string (const char *str, int len);
dyn_input dyn_open_zlib (dyn_input compressed);
dyn_input dyn_open_bz2 (dyn_input compressed);

void dyn_input_push_limit (dyn_input in, int len);
void dpm_input_pop_limit (dyn_input in);

void dyn_input_count_lines (dyn_input in);
int dyn_input_lineno (dyn_input in);

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
dyn_output dyn_create_output_fd (int fd);
dyn_output dyn_create_output_string ();

extern dyn_output dyn_stdout;

void dyn_output_flush (dyn_output out);
void dyn_output_abort (dyn_output out);
dyn_val dyn_output_commit (dyn_output out);

int dyn_output_grow (dyn_output out, int min);
char *dyn_output_pos (dyn_output out);
void dyn_output_advance (dyn_output out, int n);

dyn_val dyn_read (dyn_input in);
dyn_val dyn_read_string (const char *str);
int dyn_is_eof (dyn_val val);

void dyn_write (dyn_output out, const char *format, ...);
void dyn_writev (dyn_output out, const char *format, va_list args);
void dyn_print (const char *format, ...);

dyn_val dyn_format (const char *fmt, ...);
dyn_val dyn_formatv (const char *fmt, va_list ap);

dyn_val dyn_eval (dyn_val form);
dyn_val dyn_load (dyn_input in);

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

extern dyn_condition dyn_condition_error;

dyn_val dyn_catch_error (void (*func) (void *data), void *data);
__attribute__ ((noreturn)) 
void dyn_error (const char *fmt, ...);
__attribute__ ((noreturn)) 
void dyn_errorv (const char *fmt, va_list args);

#endif /* !DPM_DYN_H */
