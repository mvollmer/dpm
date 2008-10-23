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
   Only a single client can have a given struct-store open.

   It contains three kinds of values: null, small integers, records
   and blobs.  Four.  Four kinds of values.

   Records contain references to other values, and blobs contain
   arbitrary bytes.  Small integers are limited to 30 bits and are
   mainly an optimization that allows one to avoid creating blobs in
   many common cases.  There is a single null value that can be used
   to indicate the absence of a real value.  One of the values is
   designated as the root.

   You can not change values, only create new ones.  Setting a new
   root is guaranteed to leave the struct-store in a consistent state
   on disk: either the new root has been set and all referenced
   values are present, or the old root is still set.
   
   A garbage collection is performed from time to time to remove
   unreferenced values.

   None of the struct-store functions return failure indications: they
   either succeed or abort.  You can specify an error callback if you
   want to control how to abort, but abort you must.

   Accessing store values is generally done without checking whether
   the access is valid.  I.e., getting a record field of an value
   that is actually a small integer will likely crash.
   
   Each record has a 'tag'.  A tag is a very small integer that you
   can use as you see fit.

   There are 127 different tags. [...]

   There is special support for 'tables' and 'dictionaries'.

   A table keeps values (usually blobs representing string) with the
   same content unique.  A dictionary maps values to other values.

   These tables and dictionaries are also immutable, of course; adding
   or removing entries produces a new dictionary.  However, when using
   the offered API, dictionaries are not always stored completely in
   the store: they are only made permanent when you actually ask for
   the store value that represents them.  In this way, creation of
   garbage is kept somewhat under control.

   A value is automatically removed from all tables when it is no
   longer referenced from any other place.  Dictionaries can
   optionally have weak keys or weak value sets.  When the last
   non-weak reference to a key disappears, it is removed (together
   with its values) from all dictionaries.  When the last non-weak
   reference to a value disappears, it is removed from all weak value
   sets.  If the set becomes empty, it and its key are removed from
   the dictionary.

   This removal of table and dictionary entries is purely done to
   collect garbage.  You should not rely on it in the design of your
   data structures and algorithms.
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

ss_store *ss_maybe_gc (ss_store *ss);
ss_store *ss_gc (ss_store *ss);

struct ss_opaque;
typedef struct ss_opaque *ss_val;

ss_val ss_get_root (ss_store *ss);
void ss_set_root (ss_store *ss, ss_val root);

#define SS_BLOB_TAG 0x7F

int ss_tag (ss_val v);
int ss_len (ss_val v);
int ss_is (ss_val v, int tag);
void ss_assert (ss_val o, int tag, int min_len);

int ss_is_int (ss_val v);
ss_val ss_from_int (int x);
int ss_to_int (ss_val v);

ss_val ss_ref (ss_val v, int index);
int ss_ref_int (ss_val v, int index);

int ss_is_blob (ss_val v);
void *ss_blob_start (ss_val b);

ss_val ss_new (ss_store *ss, int tag, int n, ...);
ss_val ss_newv (ss_store *ss, int tag, int n, ss_val *refs);
ss_val ss_blob_new (ss_store *ss, int blob_len, void *blob);

ss_store *ss_find_object_store (ss_val v);

ss_val ss_copy (ss_store *ss, ss_val v);
ss_val ss_insert (ss_store *ss, ss_val obj, int index, ss_val v);
ss_val ss_insert_many (ss_store *ss, ss_val obj, int index, int n, ...);

struct ss_tab;
typedef struct ss_tab ss_tab;

ss_tab *ss_tab_init (ss_store *ss, ss_val tab);
ss_val ss_tab_finish (ss_tab *ot);
ss_val ss_tab_intern (ss_tab *ot, ss_val v);
ss_val ss_tab_intern_blob (ss_tab *ot, int len, void *blob);
ss_val ss_tab_intern_soft (ss_tab *ot, int len, void *blob);

struct ss_dict;
typedef struct ss_dict ss_dict;

#define SS_DICT_STRONG          0
#define SS_DICT_WEAK_KEYS       1
#define SS_DICT_WEAK_VALUE_SETS 2

ss_dict *ss_dict_init (ss_store *ss, ss_val dict, int weak);
ss_val ss_dict_finish (ss_dict *d);
void ss_dict_set (ss_dict *d, ss_val key, ss_val val);
ss_val ss_dict_get (ss_dict *d, ss_val key);
void ss_dict_add (ss_dict *d, ss_val key, ss_val val);
void ss_dict_del (ss_dict *d, ss_val key, ss_val val);
void ss_dict_foreach (ss_dict *d,
		      void (*func) (ss_val key, ss_val val, void *data),
		      void *data);

#endif /* !STRUCT_STORE_H */
