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

#define _GNU_SOURCE

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "struct-store.h"

/* XXX - guard a bit against corruption via rogue pointers: map as
   much of the file read-only as possible.  As soon as next crosses a
   page boundary.
 */

/* Object layout 
 *
 * The store is a big array of uint32_t words.  A object uses one or
 * more words, with tag and length stored in the first word.
 * 
 * Of the 32 bits of the first word, 1 is reserved for the garbage
 * collector, 7 are used for the tag, and 24 are for the length.
 *
 * There are two fundamental types of objects: blobs and records.
 *
 * A blob always has the tag SS_BLOB_TAG (0x7f) and the length in the
 * first word is given in bytes.  Objects always start on word
 * boundaries: if a blob length is not a multiple of four, some bytes
 * go unused.
 *
 * Records have a tag that is not SS_BLOB.  Clients can use different
 * tags as they see fit, the store treats them all the same.  The
 * length in the first word gives the number of fields.  Each field is
 * either a reference to another object or a small integer and is
 * represented as one word.  For references, that word contains the
 * offset to the referenced object in bytes, relative to the start of
 * this record.  A zero offset represents a NULL reference.  For small
 * integers, the word contains the integer shifted left by 2 bits and
 * the lower two bits are set.
 */

#define SS_BLOB_TAG      0x7F

#define SS_WORD(o,i)          (((uint32_t *)o)[i])
#define SS_SET_WORD(o,i,v)    (SS_WORD(o,i)=(v))

#define SS_HEADER(o)          (SS_WORD(o,0))
#define SS_TAG(o)             (SS_HEADER(o)>>24)
#define SS_LEN(o)             (SS_HEADER(o)&0xFFFFFF)
#define SS_SET_HEADER(o,t,l)  (SS_SET_WORD(o,0,((t)&0x7F) << 24 | (len)))

#define SS_BLOB_LEN_TO_WORDS(l)  (((l)+3)>>2)

#define SS_IS_FORWARD(o)    (SS_HEADER(o)&0x80000000)
#define SS_GET_FORWARD(o)   ((uint32_t *)(SS_HEADER(o)&~0x80000000))
#define SS_SET_FORWARD(o,f) (SS_SET_WORD(o,0, 0x80000000 | (uint32_t)(f)))

#define SS_IS_INT(o)    ((((uint32_t)o)&3)==3)
#define SS_TO_INT(o)    (((uint32_t)o)>>2)
#define SS_FROM_INT(i)  ((((uint32_t)i)<<2)|3)

/* File format

   A struct-store file starts with a header that identifies the format
   and contains the root and various book keeping information.
*/

#define SS_MAGIC   0x42445453 /* STDB */
#define SS_VERSION 0

struct ss_header {
  uint32_t magic;
  uint32_t version;

  uint32_t root;         // offset to start of header
  uint32_t len;          // in words
  
  uint32_t padding[4];
};

/* The ss_store type
 */

struct ss_store {
  ss_store *next_store;

  char *filename;
  ss_error_callback *on_error;

  int fd;
  size_t file_size;      // in bytes
  struct ss_header *head;

  uint32_t *start;
  uint32_t *next;
  uint32_t *end;
};

/* Utilities
 */

static void *
xmalloc (size_t size)
{
  void *mem = malloc (size);
  if (mem == NULL)
    {
      fprintf (stderr, "Out of memory.\n");
      abort ();
    }
  return mem;
}

static void *
xstrdup (const char *str)
{
  char *dup;

  if (str == NULL)
    return NULL;

  dup = xmalloc (strlen (str) + 1);
  strcpy (dup, str);
  return dup;
}

/* Aborting
 */

void
ss_abort (ss_store *ss, const char *fmt, ...)
{
  va_list ap;
  char *message;

  va_start (ap, fmt);
  if (vasprintf (&message, fmt, ap) < 0)
    message = NULL;
  va_end (ap);

  if (ss->on_error)
    ss->on_error (ss, message);

  fprintf (stderr, "%s\n", message);
  free (message);
  ss_close (ss);
  abort ();
}

/* Opening and closing stores.
 */

static ss_store *all_stores = NULL;

#define MAX_SIZE    (512*1024*1024)
#define GROW_MASK   (2*1024*1024-1)
//#define GROW_MASK    (4*1024-1)        // for testing

static void
ss_grow (ss_store *ss, size_t size)
{
  size = (size + GROW_MASK) & ~GROW_MASK;
  if (size >= MAX_SIZE)
    ss_abort (ss, "%s has reached maximum size", ss->filename);

  if (size > ss->file_size)
    {
      if (ftruncate (ss->fd, size) < 0)
	ss_abort (ss, "Can't grow %s: %m", ss->filename);
      ss->file_size = size;
      ss->end = (uint32_t *)((char *)ss->head + ss->file_size);
    }
}

static void
ss_sync (ss_store *ss)
{
  uint32_t *start = ss->start + ss->head->len;
  uint32_t *end = ss->next;

  if (end > start)
    {
      start = (uint32_t *)(((int)start) & ~4095);

      fprintf (stderr, 
	       "SYNC %p - %d\n", start, (end - start)*sizeof(uint32_t));

      if (msync (start, (end - start)*sizeof(uint32_t), MS_SYNC) <0)
	ss_abort (ss, "Can't sync %s: %m", ss->filename);
      ss->head->len = ss->next - ss->start;
    }

  if (msync (ss->head, sizeof (struct ss_header), MS_ASYNC) < 0)
    ss_abort (ss, "Can't sync %s header: %m", ss->filename);
}

ss_store *
ss_open (const char *filename, int mode,
	 ss_error_callback *on_error)
{
  int fd;
  struct stat buf;
  int prot;
  int create_header;

  ss_store *ss;

  ss = xmalloc (sizeof (ss_store));
  ss->next_store = NULL;
  ss->filename = xstrdup (filename);
  ss->on_error = on_error;
  ss->head = NULL;
  ss->file_size = 0;
  ss->start = NULL;
  ss->next = NULL;
  ss->end = NULL;

  if (mode == SS_READ)
    ss->fd = open (filename, O_RDONLY);
  else
    ss->fd = open (filename, O_RDWR | O_CREAT, 0666);

  if (ss->fd < 0)
    ss_abort (ss, "Can't open %s: %m", filename);

  if (mode == SS_READ)
    prot = PROT_READ;
  else
    prot = PROT_READ | PROT_WRITE;

  ss->head = mmap (NULL, MAX_SIZE, prot, MAP_SHARED,
		   ss->fd, 0);
  if (ss->head == MAP_FAILED)
    ss_abort (ss, "Can't map %s: %m", ss->filename);

  if (mode == SS_TRUNC)
    {
      ftruncate (ss->fd, 0);
      ss->file_size = 0;
    }
  else
    {
      fstat (ss->fd, &buf);
      ss->file_size = buf.st_size;
    }

  ss->start = (uint32_t *)(ss->head + 1);
  ss->end = (uint32_t *)((char *)ss->head + ss->file_size);

  if ((ss->file_size == 0 && mode == SS_WRITE) || mode == SS_TRUNC)
    {
      ss_grow (ss, sizeof (struct ss_header));
      memset (ss->head, 0, sizeof (struct ss_header));

      ss->head->magic = SS_MAGIC;
      ss->head->version = SS_VERSION;
      ss->head->root = 0;
      ss->head->len = 0;
    }
  else
    {
      if (ss->file_size < sizeof (struct ss_header)
	  || ss->head->magic != SS_MAGIC)
	ss_abort (ss, "Not a struct-store file: %s", ss->filename);

      if (ss->head->version != SS_VERSION)
	ss_abort (ss,
		  "Unsupported struct-store format version in %s.  "
		  "Found %d, expected %d.",
		  ss->filename, ss->head->version, SS_VERSION);
    }

  ss->next = ss->start + ss->head->len;

  ss->next_store = all_stores;
  all_stores = ss;

  return ss;
}

void
ss_close (ss_store *ss)
{
  ss_store **sp;

  for (sp = &all_stores; *sp; sp = &(*sp)->next_store)
    if (*sp == ss)
      {
	*sp = ss->next_store;
	break;
      }

  if (ss->head)
    munmap (ss->head, ss->file_size);

  close (ss->fd);
  free (ss->filename);
  free (ss);
}

ss_store *
ss_find_object_store (ss_object *o)
{
  ss_store *ss;

  for (ss = all_stores; ss; ss = ss->next_store)
    if ((char *)(ss->head + 1) <= (char *)o && (char *)o < (char *)ss->next)
      return ss;
  fprintf (stderr, "Object without store, aborting.\n");
  abort ();
}

/* The root
 */

ss_object *
ss_get_root (ss_store *ss)
{
  if (ss->head->root == 0 || SS_IS_INT (ss->head->root))
    return (ss_object *)ss->head->root;
  else
    return (ss_object *)((char *)(ss->head) + ss->head->root);
}

void
ss_set_root (ss_store *ss, ss_object *root)
{
  if (root == NULL || SS_IS_INT (root))
    ss->head->root = (uint32_t)root;
  else
    ss->head->root = (char *)root - (char *)(ss->head);

  ss_sync (ss);
}

/* Allocating new objects.
 */

static uint32_t *
ss_alloc (ss_store *ss, size_t words)
{
  uint32_t *obj, *new_next;

  new_next = ss->next + words;
  if (new_next > ss->end)
    ss_grow (ss, (char *)new_next - (char *)ss->head);

  obj = ss->next;
  ss->next = new_next;
  
  return obj;
}

/* Collecting garbage
 *
 * We use a simple copying collector.  It uses a a lot of temporary
 * storage, let's see how far we can get with it.
 *
 * The forwarding pointers are stored in the from-store, and we need
 * to disconnect it from the file to prevent destroying it.
 */

static void ss_set (ss_object *obj, int i, ss_object *ref);

static ss_object *
ss_gc_copy (ss_store *to_store, ss_object *obj)
{
  uint32_t len;
  uint32_t *copy;

  if (obj == NULL || SS_IS_INT (obj))
    {
      // fprintf (stderr, "NULL\n");
      return obj;
    }

  if (SS_IS_FORWARD (obj))
    {
      // fprintf (stderr, "FORW %p -> %p\n", obj, SS_GET_FORWARD (obj));
      return (ss_object *)SS_GET_FORWARD (obj);
    }

  len = SS_LEN (obj);
  if (SS_TAG (obj) == SS_BLOB_TAG)
    {
      len = SS_BLOB_LEN_TO_WORDS (len);
      copy = ss_alloc (to_store, len + 1);
      // fprintf (stderr, "BLOB %p %d -> %p\n", obj, len, copy);
      memcpy (copy, obj, (len+1)*sizeof(uint32_t));
    }
  else
    {
      int i;
      copy = ss_alloc (to_store, len + 1);
      // fprintf (stderr, "COPY %p %d -> %p\n", obj, len, copy);
      copy[0] = SS_HEADER(obj);
      for (i = 0; i < len; i++)
	copy[i+1] = (uint32_t)ss_ref (obj, i);
    }
  
  SS_SET_FORWARD (obj, copy);

  return (ss_object *)copy;
}

static ss_object *
ss_gc_scan_and_advance (ss_store *to_store, ss_object *obj)
{
  uint32_t len = SS_LEN (obj), i;

  // fprintf (stderr, "SCAN %p %d\n", obj, len);

  if (SS_TAG (obj) != SS_BLOB_TAG)
    {
      uint32_t *w = (uint32_t *)obj;
      for (i = 0; i < len; i++)
	ss_set (obj, i, ss_gc_copy (to_store, (ss_object *)w[i+1]));
    }
  else
    len = SS_BLOB_LEN_TO_WORDS (len);
  
  return (ss_object *)(((uint32_t *)obj) + len + 1);
}

ss_store *
ss_gc (ss_store *ss)
{
  ss_store *to_store;
  ss_object *new_root, *to_ptr;
  char *newfile;

  /* Disconnect old store from file.
   */
  if (mmap (ss->head, ss->file_size, PROT_READ | PROT_WRITE, 
	    MAP_PRIVATE | MAP_FIXED, ss->fd, 0)
      == MAP_FAILED)
    ss_abort (ss, "Can't disconnect from %s: %m", ss->filename);

  /* Should have SS_CREATE mode.
   */
  asprintf (&newfile, "%s.gc", ss->filename);
  to_store = ss_open (newfile, SS_TRUNC, NULL);
  
  new_root = ss_gc_copy (to_store, ss_get_root (ss));
  for (to_ptr = (ss_object *)to_store->start;
       to_ptr < (ss_object *)to_store->next;
       to_ptr = ss_gc_scan_and_advance (to_store, to_ptr))
    ;
  ss_set_root (to_store, new_root);

  /* Rename file
   */
  if (rename (to_store->filename, ss->filename) < 0)
    ss_abort (ss, "Can't rename %s to %s: %m",
	      to_store->filename, ss->filename);

  free (to_store->filename);
  to_store->filename = ss->filename;
  ss->filename = NULL;
  ss_close (ss);

  return to_store;
}

/* Small integers
 */

int
ss_is_int (ss_object *obj)
{
  return SS_IS_INT (obj);
}

ss_object *
ss_from_int (int i)
{
  return (ss_object *)SS_FROM_INT (i);
}

int
ss_to_int (ss_object *obj)
{
  return SS_TO_INT (obj);
}

/* Objects
*/

int
ss_tag (ss_object *obj)
{
  return SS_TAG(obj);
}

int
ss_len (ss_object *obj)
{
  return SS_LEN(obj);
}

int
ss_is (ss_object *obj, int tag)
{
  return obj && SS_TAG(obj) == tag;
}

void
ss_assert (ss_object *obj, int tag, int min_len)
{
  if (obj == NULL
      || SS_TAG(obj) != tag
      || SS_LEN(obj) < min_len)
    ss_abort (ss_find_object_store (obj), "Object of wrong type.");
}

ss_object *
ss_ref (ss_object *obj, int i)
{
  uint32_t val = SS_WORD(obj,i+1);
  if (val == 0 || SS_IS_INT (val))
    return (ss_object *)val;
  else
    return (ss_object *)((uint32_t *)obj + (val>>2));
}

int
ss_ref_int (ss_object *obj, int i)
{
  return SS_TO_INT (ss_ref (obj, i));
}

static void
ss_set (ss_object *obj, int i, ss_object *val)
{
  if (val == NULL || SS_IS_INT (val))
    SS_SET_WORD (obj, i+1, (uint32_t)val);
  else
    SS_SET_WORD (obj, i+1, ((uint32_t *)val - (uint32_t *)obj) << 2);
}

ss_object *
ss_newv (ss_store *ss, int tag, int len, ss_object **vals)
{
  uint32_t *w = ss_alloc (ss, len + 1);
  int i;

  SS_SET_HEADER (w, tag, len);
  for (i = 0; i < len; i++)
    ss_set ((ss_object *)w, i, vals[i]);

  return (ss_object *)w;
}

ss_object *
ss_new (ss_store *ss, int tag, int len, ...)
{
  uint32_t *w = ss_alloc (ss, len + 1);
  va_list ap;
  int i;

  va_start (ap, len);
  SS_SET_HEADER (w, tag, len);
  for (i = 0; i < len; i++)
    ss_set ((ss_object *)w, i, va_arg (ap, ss_object *));
  va_end (ap);

  return (ss_object *)w;
}

int
ss_is_blob (ss_object *o)
{
  return SS_TAG(o) == SS_BLOB_TAG;
}

void *
ss_blob_start (ss_object *b)
{
  return ((uint32_t *)b) + 1;
}

ss_object *
ss_blob_new (ss_store *ss, int len, void *blob)
{
  uint32_t *w = ss_alloc (ss, SS_BLOB_LEN_TO_WORDS(len) + 1);

  SS_SET_HEADER(w, SS_BLOB_TAG, len);
  memcpy (w+1, blob, len);

  return (ss_object *)w;
}

/* String tables

   Bit partitioned hash tries.  I don't know what that means exactly,
   but it sounds good.

   Anyway, we store our hash table as a tree where each level of the
   tree is indexed by 5 bits of the hash value.  Each node is either a
   internal node that dispatches to 32 other nodes (according to the 5
   bits of the hash value that belong to its level), or a leaf node
   which contains a number of blobs with the same hash value.
   Internal nodes are distinguished from leaf nodes by their tag.
 */

/* Debugging
 */

void
ss_dump_store (ss_store *ss, const char *header)
{
  printf ("Store %p, %s.\n", ss, header);
  printf (" filename:  %s\n", ss->filename);
  printf (" head:      %p\n", ss->head);
  printf (" size:      %d\n", ss->file_size);
  printf (" start:     %p\n", ss->start);
  printf (" next:      %p\n", ss->next);
  printf (" end:       %p\n", ss->end);
  
  if (ss->head)
    {
      printf (" head root: %d\n", ss->head->root);
      printf (" head len:  %d\n", ss->head->len);
    }
}

void
ss_scan_store (ss_store *ss)
{
  uint32_t *w = ss->start;

  printf ("Object scan:\n");
  while (w < ss->next)
    {
      int t = SS_TAG(w), n = SS_LEN(w);
      printf (" %08x: %d : %d\n", w[0], t, n);
      if (t == SS_BLOB_TAG)
	w += SS_BLOB_LEN_TO_WORDS (n) + 1;
      else
	w += 1 + n;
    }
}
