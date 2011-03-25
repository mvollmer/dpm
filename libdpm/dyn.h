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

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>

/* Some mild dynamic language features for C: non-local control flow,
   dynamic extents, dynamic variables, and a simple dynamic type
   system with semi-automatic garbage collection.

   The dyn_begin and dyn_end functions delimit a dynamic extent.  They
   need to be called in a strictly nested fashion.  Within a dynamic
   extent, you can register certain actions that should be carried out
   when the dynamic extent ends.

   You can use the dyn_block macro instead of dyn_begin and dyn_end.

   A dynamic variable is like a thread local variable, but it will
   revert to its previous value when a dynamic extent ends.

   A "dynamic value" is a reference counted block of memory with a
   type tag.  You can define new types of values, and strings are
   defined here.

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

   You can mark a point in the call stack with dyn_catch, and you can
   directly return to such a point with dyn_throw.  When this happens,
   all intermediate dynamic extents are ended and their actions are
   executed.

   There are also little abstractions for streaming input and output.
   They do little more than manage a buffer on behalf of their client.
 */

void *dyn_malloc (size_t size);
void *dyn_calloc (size_t size);
void *dyn_realloc (void *old, size_t size);
void *dyn_mgrow (void *ptr, int *capacityp, size_t size, int min_capacity);
void *dyn_strdup (const char *str);
void *dyn_strndup (const char *str, int n);
void *dyn_memdup (void *mem, int n);

#define dyn_paste(a,b) dyn_paste2(a,b)
#define dyn_paste2(a,b) a##b

#define dyn_foreach__impl(BODY,BODY_ARGS,ITER,ITER_ARGS...)	\
    auto void BODY BODY_ARGS;  \
    ITER (BODY, ## ITER_ARGS);      \
    void BODY BODY_ARGS

#define dyn_foreach_x(DECL,ITER,ARGS...) \
  dyn_foreach__impl (dyn_paste(__body__, __LINE__), DECL, ITER, ## ARGS)

#define dyn_foreach_iter(NAME, ITER, ARGS...)				\
  for (ITER NAME __attribute__ ((cleanup (ITER##_fini)))		\
	 = (ITER##_init (&NAME, ## ARGS), NAME);			\
       !ITER##_done (&NAME);						\
       ITER##_step (&NAME))

#define dyn_foreach(VAR, ITER, ARGS...)					\
  for (bool __c = true; __c;)						\
    for (ITER##_type VAR; __c; __c = false)				\
      for (ITER __i __attribute__ ((cleanup (ITER##_fini)))		\
	     = (ITER##_init (&__i, ## ARGS), __i); \
	   !ITER##_done (&__i) && (VAR = ITER##_elt (&__i), true);	\
	   ITER##_step (&__i))

#define DYN_DECLARE_STRUCT_ITER(TYPE, ITER, INIT_ARGS...)	\
  typedef TYPE ITER##_type;					\
  typedef struct ITER ITER;					\
  void ITER##_init (ITER *, ##INIT_ARGS);			\
  void ITER##_fini (ITER *);					\
  void ITER##_step (ITER *);					\
  bool ITER##_done (ITER *);					\
  TYPE ITER##_elt (ITER *);					\
  struct ITER

void dyn_begin ();
void dyn_end ();

void dyn_on_unwind (void (*func) (int for_throw, void *data), void *data);
void dyn_on_unwind_free (void *mem);

#define dyn_block for (int __i__ __attribute__ ((cleanup (dyn_end))) = (dyn_begin(), 1); __i__; __i__=0)

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

#define dyn_new(_sym) \
  ((struct _sym##_struct *)dyn_alloc (_sym##_type, \
				      sizeof (struct _sym##_struct)))

int dyn_is (dyn_val val, dyn_type *type);
const char *dyn_type_name (dyn_val val);

int dyn_is_string (dyn_val val);
const char *dyn_to_string (dyn_val val);
dyn_val dyn_from_string (const char *str);
dyn_val dyn_from_stringn (const char *str, int len);

int dyn_is_func (dyn_val val);
dyn_val dyn_func (void (*code) (), void *env, void (*free) (void *env));
void (*dyn_func_code (dyn_val val))();
void *dyn_func_env (dyn_val val);

int dyn_equal (dyn_val a, dyn_val b);
int dyn_eq (dyn_val a, const char *b);

/* Input Streams

   A input stream is used to read bytes from some source, such as a
   file.  The bytes are exposed to you in a memory buffer: You can get
   a pointer to the bytes at the current position in the stream with
   dyn_input_pos, and you can request the buffer to be of a certain
   size with the dyn_input_grow function.  The dyn_input_len function
   tells you have large the buffer actually is, and if it is shorter
   than what you have asked for, you have reached the end of the
   stream.

   You can set a new current position anywhere in the buffer with
   dyn_input_set_pos.

   There are a number of convenience functions to examine the context
   around the current position.  They manage make sure that the buffer
   is large enough.  For example, the function dyn_input_looking_at
   takes a string argument and checks whether the bytes at the current
   stream position match that string.  The function dyn_input_find
   advances the current position until it is at one of the given
   delimiter characters.

   You can also manage a secondary position in the stream, the 'mark'.
   The buffer is always includes the mark.  Thus, once you have found
   the beginning of something in the stream, you can set the mark to
   it and continue to find the end.  After this, the mark is still in
   the buffer and thus all bytes from mark to the current position are
   available to you in memory.
*/

DYN_DECLARE_TYPE (dyn_input);
DYN_DECLARE_TYPE (dyn_output);

int dyn_file_exists (const char *filename);

dyn_input dyn_open_file (const char *filename);
dyn_input dyn_open_string (const char *str, int len);
dyn_input dyn_open_zlib (dyn_input compressed);
dyn_input dyn_open_bz2 (dyn_input compressed);

void dyn_input_push_limit (dyn_input in, int len);
void dyn_input_pop_limit (dyn_input in);

void dyn_input_count_lines (dyn_input in);
int dyn_input_lineno (dyn_input in);

void dyn_input_set_mark (dyn_input in);
const char *dyn_input_mark (dyn_input in);
char *dyn_input_mutable_mark (dyn_input in);
int dyn_input_off (dyn_input in);

const char *dyn_input_pos (dyn_input in);
void dyn_input_set_pos (dyn_input in, const char *pos);
int dyn_input_grow (dyn_input in, int min);
int dyn_input_must_grow (dyn_input in, int n);

void dyn_input_advance (dyn_input in, int n);
int dyn_input_find (dyn_input in, const char *delims);
int dyn_input_find_after (dyn_input in, const char *delims);
void dyn_input_skip (dyn_input in, const char *chars);
int dyn_input_looking_at (dyn_input in, const char *str);

/* Output streams

   Like input streams, output streams manage a memory buffer.  You
   write into that buffer by first calling dyn_output_grow to make the
   buffer large enough, writing the bytes at the address returned by
   dyn_output_pos, and then advancing the current position with
   dyn_output_advance.

   Writing to a file output stream will send all bytes to a temporary
   file first.  Only when you call dyn_output_commit will the file
   appear with its final name (or replace the old file with that
   name).

   When you call dyn_output_abort, the temporary file will be deleted
   and nothing permanent will happen to the filesystem.
 */

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

/* Writing.

   The dyn_write function takes a formatting template much like
   printf, with the following formatting codes:

   %s - prints a '\0' terminated string, character by character.

   %S - prints a '\0' terminated string surrounded by quotes and with
        escape sequences inside.

   %ls, %lS - like %s and %S, but print a string with given length.

   %v - prints a dyn_val according to its type; strings are printed as
        with %s.

   %V - prints a dyn_val according to its type; strings are printed as
        with %S.

   %m - prints errno as a string.

   %d - prints a int as a decimal number.

   %x - prints a int as a hexadeximal number.

   %f - prints a double as with printf %g.

   %I - prints all bytes between the mark and position of a input
        stream.

   %B - takes a pointer and int as address and length and prints those bytes.
   
   %% - prints a single %.
 
   The function dyn_print is the same as dyn_write to stdout, and
   dyn_format returns the printing result as a string.
 */

void dyn_write (dyn_output out, const char *format, ...);
void dyn_writev (dyn_output out, const char *format, va_list args);
void dyn_print (const char *format, ...);

dyn_val dyn_format (const char *fmt, ...);
dyn_val dyn_formatv (const char *fmt, va_list ap);

typedef struct {
  dyn_val val;
} dyn_var;

dyn_val dyn_get (dyn_var *var);
void dyn_set (dyn_var *var, dyn_val val);
void dyn_let (dyn_var *var, dyn_val val);

typedef struct dyn_target dyn_target;

dyn_val dyn_catch (void (*func) (dyn_target *, void *data), void *data);
__attribute__ ((noreturn)) 
void dyn_throw (dyn_target *target, dyn_val value);

typedef struct {
  const char *name;
  dyn_var handler;
  void (*unhandled) (dyn_val value);
} dyn_condition;

__attribute__ ((noreturn)) 
void dyn_signal (dyn_condition *condition, dyn_val value);
void dyn_let_handler (dyn_condition *condition, dyn_val handler);
dyn_val dyn_catch_condition (dyn_condition *condition,
			     void (*func) (void *data), void *data);

extern dyn_condition dyn_condition_error;

dyn_val dyn_catch_error (void (*func) (void *data), void *data);
__attribute__ ((noreturn)) 
void dyn_error (const char *fmt, ...);
__attribute__ ((noreturn)) 
void dyn_errorv (const char *fmt, va_list args);

void dyn_ensure_init ();

#endif /* !DPM_DYN_H */
