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

#ifndef STRUCT_STORE_H

/* A struct-store is a file-backed region of memory.

   A struct-store is not portable across different architectures.
   Only a single client can have a given struct-store open.  (But it
   should be easy to remove the last restriction.)

   It contains three kinds of objects: null, small integers, records
   and blobs.  Records contain references to other objects, and blobs
   contain arbitrary amounts of bytes.  Small integers are limited to
   30 bits and are mainly an optimization that allows one to avoid
   creating blobs in many common cases.  There is a single null object
   that can be used to indicate the absence of a real object.  One of
   the objects is designated as the root.

   You can not change objects, only create new ones.  Setting a new
   root is guaranteed to leave the struct-store in a consistent state
   on disk: either the new root has been set and all referenced
   objects are present, or the old root is still set.
   
   A garbage collection is performed from time to time to remove
   unreferenced objects.

   None of the struct-store functions return failure indications: they
   either succeed or abort.  You can specify an error callback if you
   want to control how to abort, but abort you must.

   Accessing store objects is generally done without checking whether
   the access is valid.  I.e., getting a record field of an object
   that is actually a small integer will likely crash.
   

   There is special support for string tables and dictionaries. 

   A string table keeps blobs (usually strings) with the same content
   unique.  A dictionary maps objects to objects.

   These tables and dictionaries are also immutable, of course; adding
   or removing entries produces a new dictionary.  However, when using
   the offered API, dictionaries are not always stored completely in
   the store: they are only made permanent when you actually ask for
   the store object that represents them.

   The dictionaries can use 'weak' references.  A blob is
   automatically removed from the string table when it is no longer
   referenced in a 'strong' way.  Other dictionaries can be instructed
   to have weak keys: when the object used as the key is no longer
   strongly referenced, the key/value association is removed.
 */

struct ss_store;
typedef struct ss_store ss_store;

typedef void ss_error_callback (ss_store *ss, const char *message);

#define SS_READ  0
#define SS_WRITE 1
#define SS_TRUNC 2

ss_store *ss_open (const char *filename, int mode,
		   ss_error_callback *on_error);
void ss_close (ss_store *ss);

void ss_abort (ss_store *ss, const char *fmt, ...);

void ss_reset (ss_store *ss);
ss_store *ss_gc (ss_store *ss);

struct ss_object;
typedef struct ss_object ss_object;

ss_object *ss_get_root (ss_store *ss);
void ss_set_root (ss_store *ss, ss_object *root);

#define SS_BLOB_TAG 0x7F

int ss_tag (ss_object *o);
int ss_len (ss_object *o);
int ss_is (ss_object *o, int tag);
void ss_assert (ss_object *o, int tag, int min_len);

int ss_is_int (ss_object *);
ss_object *ss_from_int (int x);
int ss_to_int (ss_object *);

ss_object *ss_ref (ss_object *obj, int index);
int ss_ref_int (ss_object *obj, int index);

int ss_is_blob (ss_object *o);
void *ss_blob_start (ss_object *b);

ss_object *ss_new (ss_store *ss, int tag, int n, ...);
ss_object *ss_newv (ss_store *ss, int tag, int n, ss_object **refs);
ss_object *ss_blob_new (ss_store *ss, int blob_len, void *blob);

ss_store *ss_find_object_store (ss_object *);

struct ss_string_table;
typedef struct ss_string_table ss_string_table;

ss_string_table *ss_string_table_get (ss_store *ss, ss_object *obj);
ss_object *ss_string_table_intern (ss_string_table *st, int len, void *blob);
ss_object *ss_string_table_object (ss_string_table *st);

#endif /* !STRUCT_STORE_H */
