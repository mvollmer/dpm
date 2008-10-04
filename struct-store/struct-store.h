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

   It contains a number of records.  Records can contain references to
   other records, and one of the records is the 'root'.

   You can not change records, only create new ones.  Setting a new
   root is guaranteed to leave the struct-store in a consistent state
   on disk: either the new root has been set and all referenced
   records are present, or the old root is still set.
   
   A garbage collection is performed from time to time to remove
   unreferenced records.

   A record is a sequence of references to other records, called its
   "fields", followed by a region of uninterpreted memory, called it's
   "blob".  Some of the references can be 'weak': if there are only
   weak references to a record, that record is removed by the garbage
   collector and all references to it are set to null.

   None of the struct-store functions return failure indications: they
   either succeed or abort.  You can specify a error callback if you
   want to control how to abort.
 */

struct ss_store;
typedef struct ss_store ss_store;

typedef void ss_error_callback (ss_store *ss, const char *message);

ss_store *ss_open (const char *filename, int writing,
		   ss_error_callback *on_error);
void ss_close (ss_store *ss);

void ss_reset (ss_store *ss);
void ss_gc (ss_store *ss);

struct ss_record;
typedef struct ss_record ss_record;

ss_record *ss_get_root (struct_store *ss);
void ss_set_root (struct_store *ss, ss_record *root);

int ss_record_field_count (ss_record *r);
ss_record *ss_record_ref (ss_record *r, int index);

void *ss_record_blob (ss_record *r);
int ss_record_blob_len (ss_record *r);

ss_record *ss_record_new (int field_count, ss_record *fields, int *weak_p,
			  int blob_len, void *blob);
/* short cuts */
ss_record *ss_new (ss_record *first_field, ...);
ss_record *ss_blob_new (int blob_len, void *blob);

ss_store *ss_find_record_store (ss_record *r);

#endif /* !STRUCT_STORE_H */
