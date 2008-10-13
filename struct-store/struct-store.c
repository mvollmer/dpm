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
  uint32_t alloced;      // in words, since last gc

  uint32_t padding[3];
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
  int alloced_words;
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
xremalloc (void *old, size_t size)
{
  void *mem = realloc (old, size);
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
      ss->head->alloced = ss->alloced_words;
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
  ss->alloced_words = 0;

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
      ss->head->alloced = 0;
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
ss_find_object_store (ss_val o)
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

ss_val 
ss_get_root (ss_store *ss)
{
  if (ss->head->root == 0 || SS_IS_INT (ss->head->root))
    return (ss_val )ss->head->root;
  else
    return (ss_val )((char *)(ss->head) + ss->head->root);
}

void
ss_set_root (ss_store *ss, ss_val root)
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

  if (ss == NULL)
    return xmalloc (words * sizeof(uint32_t));

  new_next = ss->next + words;
  ss->alloced_words += words;
  if (new_next > ss->end)
    ss_grow (ss, (char *)new_next - (char *)ss->head);

  obj = ss->next;
  ss->next = new_next;
  
  return obj;
}

int
ss_is_stored (ss_store *ss, ss_val obj)
{
  uint32_t *w = (uint32_t *)obj;
  return ss == NULL || (ss->start <= w && w < ss->next);
}

void
ss_assert_in_store (ss_store *ss, ss_val obj)
{
  if (obj == NULL || ss_is_int (obj) || ss_is_stored (ss, obj))
    return;
  ss_abort (ss, "Rogue pointer.");
}
    
/* Collecting garbage
 *
 * We use a simple copying collector.  It uses a a lot of temporary
 * storage, let's see how far we can get with it.
 *
 * The forwarding pointers are stored in the from-store, and we need
 * to disconnect it from the file to prevent destroying it.
 */

#define DICT_DISPATCH_TAG 0x7C
#define DICT_SEARCH_TAG   0x7B

static void ss_set (ss_val obj, int i, ss_val ref);
static ss_val ss_dict_gc_copy (ss_store *to_store, ss_val dict);

static ss_val 
ss_gc_copy (ss_store *to_store, ss_val obj)
{
  uint32_t len;
  uint32_t *copy;

  if (obj == NULL || SS_IS_INT (obj))
    return obj;

  if (SS_IS_FORWARD (obj))
    return (ss_val)SS_GET_FORWARD (obj);

  if (ss_is_stored (to_store, obj))
    return obj;

  if (ss_is (obj, DICT_DISPATCH_TAG)
      || ss_is (obj, DICT_SEARCH_TAG))
    copy = (uint32_t *)ss_dict_gc_copy (to_store, obj);
  else
    {
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
	    ss_set ((ss_val)copy, i, ss_ref (obj, i));
	}
    }
  
  SS_SET_FORWARD (obj, copy);

  return (ss_val)copy;
}

static ss_val 
ss_gc_scan_and_advance (ss_store *to_store, ss_val obj)
{
  uint32_t len = SS_LEN (obj), i;

  // fprintf (stderr, "SCAN %p %d\n", obj, len);

  if (SS_TAG (obj) != SS_BLOB_TAG)
    {
      uint32_t *w = (uint32_t *)obj;
      for (i = 0; i < len; i++)
	ss_set (obj, i, ss_gc_copy (to_store, ss_ref (obj, i)));
    }
  else
    len = SS_BLOB_LEN_TO_WORDS (len);
  
  return (ss_val )(((uint32_t *)obj) + len + 1);
}

ss_store *
ss_gc (ss_store *ss)
{
  ss_store *to_store;
  ss_val new_root, to_ptr;
  char *newfile;

  /* Disconnect old store from file.
   */
  if (mmap (ss->head, ss->file_size, PROT_READ | PROT_WRITE, 
	    MAP_PRIVATE | MAP_FIXED, ss->fd, 0)
      == MAP_FAILED)
    ss_abort (ss, "Can't disconnect from %s: %m", ss->filename);

  asprintf (&newfile, "%s.gc", ss->filename);
  to_store = ss_open (newfile, SS_TRUNC, NULL);
  
  new_root = ss_gc_copy (to_store, ss_get_root (ss));
  for (to_ptr = (ss_val)to_store->start;
       to_ptr < (ss_val)to_store->next;
       to_ptr = ss_gc_scan_and_advance (to_store, to_ptr))
    ;
  to_store->alloced_words = 0;
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

ss_store *
ss_maybe_gc (ss_store *ss)
{
  if (ss->head->alloced > ss->head->len / 10)
    {
      fprintf (stderr, "Allocated %d words, garbage collecting\n", 
		ss->head->alloced);
      return ss_gc (ss);
    }
  else
    return ss;
}

/* Small integers
 */

int
ss_is_int (ss_val obj)
{
  return SS_IS_INT (obj);
}

ss_val 
ss_from_int (int i)
{
  return (ss_val )SS_FROM_INT (i);
}

int
ss_to_int (ss_val obj)
{
  return SS_TO_INT (obj);
}

/* Objects
*/

int
ss_tag (ss_val obj)
{
  return SS_TAG(obj);
}

int
ss_len (ss_val obj)
{
  return SS_LEN(obj);
}

int
ss_is (ss_val obj, int tag)
{
  return obj && SS_TAG(obj) == tag;
}

void
ss_assert (ss_val obj, int tag, int min_len)
{
  if (obj == NULL
      || SS_TAG(obj) != tag
      || SS_LEN(obj) < min_len)
    ss_abort (ss_find_object_store (obj), "Object of wrong type.");
}

ss_val 
ss_ref (ss_val obj, int i)
{
  uint32_t val = SS_WORD(obj,i+1);
  if (val == 0 || SS_IS_INT (val))
    return (ss_val )val;
  else
    return (ss_val )((uint32_t *)obj + (val>>2));
}

int
ss_ref_int (ss_val obj, int i)
{
  return SS_TO_INT (ss_ref (obj, i));
}

void
ss_set (ss_val obj, int i, ss_val val)
{
  if (val == NULL || SS_IS_INT (val))
    SS_SET_WORD (obj, i+1, (uint32_t)val);
  else
    SS_SET_WORD (obj, i+1, ((uint32_t *)val - (uint32_t *)obj) << 2);
}

ss_val 
ss_newv (ss_store *ss, int tag, int len, ss_val *vals)
{
  uint32_t *w = ss_alloc (ss, len + 1);
  int i;

  SS_SET_HEADER (w, tag, len);
  for (i = 0; i < len; i++)
    {
      ss_assert_in_store (ss, vals[i]);
      ss_set ((ss_val )w, i, vals[i]);
    }

  return (ss_val )w;
}

ss_val 
ss_new (ss_store *ss, int tag, int len, ...)
{
  uint32_t *w = ss_alloc (ss, len + 1);
  va_list ap;
  int i;

  va_start (ap, len);
  SS_SET_HEADER (w, tag, len);
  for (i = 0; i < len; i++)
    {
      ss_val val = va_arg (ap, ss_val );
      ss_assert_in_store (ss, val);
      ss_set ((ss_val )w, i, val);
    }
  va_end (ap);

  return (ss_val )w;
}

ss_val 
ss_make (ss_store *ss, int tag, int len, ss_val init)
{
  int i;
  uint32_t *w = ss_alloc (ss, len + 1);

  ss_assert_in_store (ss, init);
  SS_SET_HEADER (w, tag, len);
  for (i = 0; i < len; i++)
    ss_set ((ss_val )w, i, init);
  return (ss_val )w;
}

int
ss_is_blob (ss_val o)
{
  return SS_TAG(o) == SS_BLOB_TAG;
}

void *
ss_blob_start (ss_val b)
{
  return ((uint32_t *)b) + 1;
}

ss_val 
ss_blob_new (ss_store *ss, int len, void *blob)
{
  uint32_t *w = ss_alloc (ss, SS_BLOB_LEN_TO_WORDS(len) + 1);

  SS_SET_HEADER(w, SS_BLOB_TAG, len);
  memcpy (w+1, blob, len);

  return (ss_val )w;
}

ss_val 
ss_copy (ss_store *ss, ss_val obj)
{
  if (obj == NULL || ss_is_int (obj))
    return obj;
  else if (ss_is_blob (obj))
    return ss_blob_new (ss, ss_len (obj), ss_blob_start (obj));
  else
    {
      int len = ss_len(obj), i;
      ss_val vals[len];
      for (i = 0; i < len; i++)
	vals[i] = ss_ref (obj, i);
      return ss_newv (ss, ss_tag (obj), ss_len (obj), vals);
    }
}

ss_val 
ss_insert_many (ss_store *ss, ss_val obj, int index, int n, ...)
{
  int len = ss_len (obj), i;
  ss_val vals[len+n];
  va_list ap;

  i = 0;
  while (i < index)
    {
      vals[i] = ss_ref (obj, i);
      i++;
    }
  va_start (ap, n);
  while (i < index + n)
    {
      vals[i] = va_arg (ap, ss_val);
      i++;
    }
  va_end (ap);
  while (i < len + n)
    {
      vals[i] = ss_ref (obj, i-n);
      i++;
    }

  return ss_newv (ss, ss_tag (obj), len + n, vals);
}

ss_val 
ss_insert (ss_store *ss, ss_val obj, int index, ss_val val)
{
  return ss_insert_many (ss, obj, index, 1, val);
}

static ss_val 
ss_store_object (ss_store *ss, ss_val obj)
{
  ss_val copy;

  if (obj == NULL || ss_is_int (obj) || ss_is_stored (ss, obj))
    return obj;
  
  if (ss_is_blob (obj))
    copy = ss_blob_new (ss, ss_len (obj), ss_blob_start (obj));
  else
    {
      int len = ss_len(obj), i;
      ss_val vals[len];
      for (i = 0; i < len; i++)
	vals[i] = ss_store_object (ss, ss_ref (obj, i));
      copy = ss_newv (ss, ss_tag (obj), ss_len (obj), vals);
    }

  free (obj);
  return copy;
}

static ss_val 
ss_unstore_object (ss_store *ss, ss_val obj)
{
  if (ss_is_stored (ss, obj))
    return ss_copy (NULL, obj);
  else
    return obj;
}

/* Hashing and equality
 */

static uint32_t
ss_hash_blob (int len, void *blob)
{
  uint32_t h = 0;
  char *mem = (char *)blob;

  while (len-- > 0)
    h = *mem++ + h*37;
  return h & 0x3FFFFFFF;
}

static uint32_t
ss_hash (ss_val o)
{
  if (o == NULL)
    return 0;

  if (ss_is_int (o))
    return ss_to_int (o);

  if (ss_is_blob (o))
    return ss_hash_blob (ss_len (o), ss_blob_start (o));
  else
    {
      uint32_t h = 0;
      int len = ss_len (o), i;
      for (i = 0; i < len; i++)
	h = h<<8 + ss_hash (ss_ref (o, i));
      return h & 0x3FFFFFFF;
    }
}

static uint32_t
ss_id_hash (ss_store *ss, ss_val o)
{
  return ((uint32_t)o - (uint32_t)(ss->start)) & 0x3FFFFFFF;
}

static int
ss_equal (ss_val a, ss_val b)
{
  if (a == NULL)
    return b == NULL;

  if (ss_is_int (a))
    return ss_is_int (b) && ss_to_int (a) == ss_to_int (b);

  if (ss_tag (a) != ss_tag (b) || ss_len (a) != ss_len (b))
    return 0;

  if (ss_is_blob (a))
    return memcmp (ss_blob_start (a), ss_blob_start (b), ss_len (a)) == 0;

  {
    int len = ss_len (a), i;
    for (i = 0; i < len; i++)
      if (!ss_equal (ss_ref (a, i), ss_ref (b, i)))
	return 0;
    return 1;
  }
}

static int
ss_equal_blob (ss_val b, int len, void *blob)
{
  if (b && !ss_is_int (b) && ss_is_blob (b)
      && ss_len (b) == len)
    return memcmp (ss_blob_start (b), blob, len) == 0;
  return 0;
}

/* Small, sparse vectors.
 */

static inline int
popcnt (uint32_t x)
{
  return __builtin_popcount (x);
}

static ss_val 
ss_mapvec_new (int tag)
{
  return ss_new (NULL, tag, 3, ss_from_int (0), NULL, NULL);
}

static ss_val 
ss_mapvec_get (ss_val vec, int index)
{
  uint32_t map = ss_to_int (ss_ref (vec, 0)) | 0xC0000000;
  int bit = 1 << index;
  int pos = (index == 0)? 1 : popcnt (map << (32 - index)) + 1;
  
  if (map & bit)
    return ss_ref (vec, pos);
  else
    return NULL;
}

static ss_val 
ss_mapvec_set (ss_store *ss, ss_val vec, int index, ss_val val)
{
  ss_val new_vec;
  uint32_t map = ss_to_int (ss_ref (vec, 0)) | 0xC0000000;
  int bit = 1 << index;
  int pos = (index == 0)? 1 : popcnt (map << (32 - index)) + 1;

  if (map & bit)
    {
      new_vec = ss_unstore_object (ss, vec);
      ss_set (new_vec, pos, val);
    }
  else
    {
      new_vec = ss_insert (NULL, vec, pos, val);
      if (!ss_is_stored (ss, vec))
	free (vec);
      map |= bit;
      ss_set (new_vec, 0, ss_from_int (map));
    }
  return new_vec;
}

/* Hash tables

   Bit partitioned hash tries.  I don't know what that means exactly,
   but it sounds good.

   Anyway, we store our hash table as a tree where each level of the
   tree is indexed by N bits of the hash value.  Each node is either a
   "dispatch node" that dispatches to 2^N other nodes (according to
   the N bits of the hash value that belong to its level), or a
   "search node" which contains a number of blobs with the same hash
   value.

   Dispatch nodes are distinguished from search nodes by their tag.
   In order to not waste memory, dispatch nodes are stored in
   compressed form: a bitmap identifies which entries are used, and
   the unused ones are not stored.

   (See Phil Bagwell's paper "Ideal Hash Trees" for more about this.)
 */

#define BITS_PER_LEVEL 5
#define LEVEL_MASK     ((1<<BITS_PER_LEVEL)-1)

typedef struct ss_hash_class {
  int dispatch_tag;
  int search_tag;
  ss_val (*action) (ss_store *ss, ss_val node, int hash, void *data);
} ss_hash_class;

static ss_val 
ss_hash_node_lookup (ss_hash_class *class,
		     ss_store *ss,
		     ss_val node, int shift,
		     uint32_t hash, void *data)
{
  if (node == NULL)
    return class->action (ss, node, hash, data);
  else if (ss_is (node, class->search_tag))
    {
      if (ss_to_int (ss_ref (node, 0)) == hash)
	return class->action (ss, node, hash, data);
      else
	{
	  /* Create a new dispatch node and move this search node one
	     level down.
	  */
	  ss_val entry, new_entry, new_node;
	  int obj_index = (hash >> shift) & LEVEL_MASK;
	  int node_index = ss_to_int (ss_ref (node, 0)) >> shift & LEVEL_MASK;

	  /* XXX - optimize this to create the new_node in one go.
	   */
	  new_node = ss_mapvec_new (class->dispatch_tag);
	  new_node = ss_mapvec_set (ss, new_node, node_index, node);
	  entry = ss_mapvec_get (new_node, obj_index);
	  new_entry = ss_hash_node_lookup (class, ss,
					   entry,
					   shift + BITS_PER_LEVEL,
					   hash, data);
	  new_node = ss_mapvec_set (ss, new_node, obj_index, new_entry);
	  return new_node;
	}
    }
  else
    {
      /* Recurse through this dispatch node
       */
      ss_val entry, new_entry;
      int index = (hash >> shift) & LEVEL_MASK;

      entry = ss_mapvec_get (node, index);
      new_entry = ss_hash_node_lookup (class, ss,
				       entry,
				       shift + BITS_PER_LEVEL,
				       hash, data);
      if (new_entry != entry)
	node = ss_mapvec_set (ss, node, index, new_entry);
      return node;
    }
}

/* Object tables
 */

#define DISPATCH_TAG 0x7D
#define SEARCH_TAG   0x7E

ss_val
ss_objtab_intern_action (ss_store *ss, ss_val node, int hash, void *data)
{
  ss_val *objp = (ss_val *)data;
  ss_val obj = *objp;

  if (node == NULL)
    {
      obj = ss_store_object (ss, obj);
      *objp = obj;
      return ss_new (NULL, SEARCH_TAG, 2, ss_from_int (hash), obj);
    }
  else
    {
      /* Add to this search node if not found.
      */
      int len = ss_len (node), i;
      for (i = 1; i < len; i++)
	if (ss_equal (ss_ref (node, i), obj))
	  {
	    *objp = ss_ref (node, i);
	    return node;
	  }
      obj = ss_store_object (ss, obj);
      *objp = obj;
      return ss_insert (NULL, node, len, obj);
    }
}

ss_hash_class ss_objtab_intern_class = {
  DISPATCH_TAG,
  SEARCH_TAG,
  ss_objtab_intern_action
};
    
typedef struct {
  int len;
  void *blob;
  ss_val obj;
} ss_objtab_intern_blob_data;


ss_val
ss_objtab_intern_blob_action (ss_store *ss, ss_val node, int hash, void *data)
{
  ss_objtab_intern_blob_data *d = (ss_objtab_intern_blob_data *)data;

  if (node == NULL)
    {
      d->obj = ss_blob_new (ss, d->len, d->blob);
      return ss_new (NULL, SEARCH_TAG, 2, ss_from_int (hash), d->obj);
    }
  else
    {
      /* Add to this search node if not found.
      */
      int len = ss_len (node), i;
      for (i = 1; i < len; i++)
	if (ss_equal_blob (ss_ref (node, i), d->len, d->blob))
	  {
	    d->obj = ss_ref (node, i);
	    return node;
	  }
      d->obj = ss_blob_new (ss, d->len, d->blob);
      return ss_insert (NULL, node, len, d->obj);
    }
}

ss_hash_class ss_objtab_intern_blob_class = {
  DISPATCH_TAG,
  SEARCH_TAG,
  ss_objtab_intern_blob_action
};

ss_val
ss_objtab_intern_soft_action (ss_store *ss, ss_val node, int hash, void *data)
{
  ss_objtab_intern_blob_data *d = (ss_objtab_intern_blob_data *)data;

  if (node == NULL)
    return NULL;
  else
    {
      int len = ss_len (node), i;
      for (i = 1; i < len; i++)
	if (ss_equal_blob (ss_ref (node, i), d->len, d->blob))
	  {
	    d->obj = ss_ref (node, i);
	    return node;
	  }
      return node;
    }
}

ss_hash_class ss_objtab_intern_soft_class = {
  DISPATCH_TAG,
  SEARCH_TAG,
  ss_objtab_intern_soft_action
};

struct ss_objtab {
  ss_store *store;
  ss_val root;
};

ss_objtab *
ss_objtab_init (ss_store *ss, ss_val root)
{
  ss_objtab *ot = xmalloc (sizeof (ss_objtab));
  ot->store = ss;
  ot->root = root;
  return ot;
}

ss_val 
ss_objtab_finish (ss_objtab *ot)
{
  ss_val root;

  root = ss_store_object (ot->store, ot->root);
  free (ot);

  return root;
}

ss_val 
ss_objtab_intern (ss_objtab *ot, ss_val obj)
{
  uint32_t h = ss_hash (obj);
  ot->root = ss_hash_node_lookup (&ss_objtab_intern_class,
				  ot->store, ot->root, 0, h, &obj);
  return obj;
}

ss_val 
ss_objtab_intern_blob (ss_objtab *ot, int len, void *blob)
{
  ss_objtab_intern_blob_data d = { len, blob, NULL };
  uint32_t h = ss_hash_blob (len, blob);
  ot->root = ss_hash_node_lookup (&ss_objtab_intern_blob_class,
				  ot->store, ot->root, 0, h, &d);
  return d.obj;
}

ss_val 
ss_objtab_intern_soft (ss_objtab *ot, int len, void *blob)
{
  ss_objtab_intern_blob_data d = { len, blob, NULL };
  uint32_t h = ss_hash_blob (len, blob);
  ot->root = ss_hash_node_lookup (&ss_objtab_intern_soft_class,
				  ot->store, ot->root, 0, h, &d);
  return d.obj;
}

void
ss_objtab_node_foreach (ss_val node,
			void (*func) (ss_val , void *data), void *data)
{
  if (node == NULL)
    ;
  else if (ss_is (node, SEARCH_TAG))
    {
      int len = ss_len(node), i;
      for (i = 1; i < len; i++)
	func (ss_ref (node, i), data);
    }
  else
    {
      int len = ss_len(node), i;
      for (i = 1; i < len; i++)
	ss_objtab_node_foreach (ss_ref (node, i), func, data);
    }
}

void
ss_objtab_foreach (ss_objtab *ot,
		   void (*func) (ss_val , void *data), void *data)
{
  ss_objtab_node_foreach (ot->root, func, data);
}

typedef struct {
  int n_leaves;
  int n_leaf_nodes;
  int n_dispatches;
  int n_dispatch_nodes;
  int n_dispatch_slots;
  int max_level;
  int max_collisions;
} ss_objtab_stats;

static void
ss_objtab_dump_node (ss_val n, int level, ss_objtab_stats *stats)
{
  int i;
  static char spaces[21] = "                    ";

  if (n == NULL)
    {
      // printf ("%.*sNULL\n", level, spaces);
    }
  else
    {
      if (level > stats->max_level)
	stats->max_level = level;
      
      if (ss_tag (n) == SEARCH_TAG)
	{
	  // printf ("%.*s%d leaves of %x\n", level, spaces, n->len, n->hash);
	  stats->n_leaf_nodes++;
	  stats->n_leaves += ss_len (n) - 1;
	  if (ss_len(n)-1 > stats->max_collisions)
	    stats->max_collisions = ss_len(n)-1;
	}
      else
	{
	  //  printf ("%.*s%d nodes\n", level, spaces, n->len);
	  stats->n_dispatch_nodes++;
	  stats->n_dispatch_slots += ss_len (n) - 1;
	  for (i = 1; i < ss_len (n); i++)
	    if (ss_ref (n, i))
	      {
		stats->n_dispatches++;
		ss_objtab_dump_node (ss_ref (n, i), level+1, stats);
	      }
	}
    }
}

void
ss_objtab_dump (ss_objtab *ot)
{
  ss_objtab_stats stats = { 0, 0, 0, 0, 0, 0 };
  ss_objtab_dump_node (ot->root, 0, &stats);

  printf ("Stats:\n");
  printf (" %d leaves in %d search nodes, %d collisions max\n",
	  stats.n_leaves, stats.n_leaf_nodes, stats.max_collisions-1);
  printf (" %d dispatches in %d dispatch nodes on %d levels\n",
	  stats.n_dispatches, stats.n_dispatch_nodes, stats.max_level);
  printf (" %g%% dispatch slots used\n",
	  stats.n_dispatches * 100.0 / (stats.n_dispatch_slots));
  printf (" %g dispatch slots used per node\n",
	  1.0*stats.n_dispatches / stats.n_dispatch_nodes);
}

/* Dictionaries
 */

typedef struct {
  ss_val key;
  ss_val val;
} ss_dict_action_data;

ss_val
ss_dict_get_action (ss_store *ss, ss_val node, int hash, void *data)
{
  ss_dict_action_data *d = (ss_dict_action_data *)data;

  if (node == NULL)
    {
      d->val = NULL;
      return node;
    }
  else
    {
      int len = ss_len (node), i;
      for (i = 1; i < len; i += 2)
	if (ss_ref (node, i) == d->key)
	  {
	    d->val = ss_ref (node, i+1);
	    return node;
	  }

      d->val = NULL;
      return node;
    }
}

ss_hash_class ss_dict_get_class = {
  DICT_DISPATCH_TAG,
  DICT_SEARCH_TAG,
  ss_dict_get_action
};

ss_val
ss_dict_set_action (ss_store *ss, ss_val node, int hash, void *data)
{
  ss_dict_action_data *d = (ss_dict_action_data *)data;

  if (node == NULL)
    {
      if (d->val == NULL)
	return NULL;
      else
	return ss_new (NULL, DICT_SEARCH_TAG, 3, ss_from_int (hash),
		       d->key, d->val);
    }
  else
    {
      int len = ss_len (node), i;
      for (i = 1; i < len; i += 2)
	if (ss_ref (node, i) == d->key)
	  {
	    /* XXX - shrink node when d.val == NULL
	     */
	    node = ss_unstore_object (ss, node);
	    ss_set (node, i+1, d->val);
	    return node;
	  }
      
      return ss_insert_many (NULL, node, len, 2, d->key, d->val);
    }
}

ss_hash_class ss_dict_set_class = {
  DICT_DISPATCH_TAG,
  DICT_SEARCH_TAG,
  ss_dict_set_action
};

struct ss_dict {
  ss_store *store;
  ss_val root;
};

ss_dict *
ss_dict_init (ss_store *ss, ss_val root)
{
  ss_dict *d = xmalloc (sizeof (ss_dict));
  d->store = ss;
  d->root = root;
  return d;
}

ss_val 
ss_dict_finish (ss_dict *d)
{
  ss_val root;

  root = ss_store_object (d->store, d->root);
  free (d);

  return root;
}

ss_val
ss_dict_get (ss_dict *d, ss_val key)
{
  ss_dict_action_data ad = { key, NULL };
  uint32_t h = ss_id_hash (d->store, key);
  ss_hash_node_lookup (&ss_dict_get_class, d->store, 
		       d->root, 0, h, &ad);
  return ad.val;
}

void
ss_dict_set (ss_dict *d, ss_val key, ss_val val)
{
  ss_dict_action_data ad = { key, val };
  uint32_t h = ss_id_hash (d->store, key);
  d->root = ss_hash_node_lookup (&ss_dict_set_class, d->store, 
				 d->root, 0, h, &ad);
}

static void
ss_dict_node_foreach (ss_val node,
		      void (*func) (ss_val key, ss_val val, void *data),
		      void *data)
{
  if (node == NULL)
    ;
  else if (ss_is (node, DICT_SEARCH_TAG))
    {
      int len = ss_len(node), i;
      for (i = 1; i < len; i += 2)
	func (ss_ref (node, i), ss_ref (node, i+1), data);
    }
  else
    {
      int len = ss_len(node), i;
      for (i = 1; i < len; i++)
	ss_dict_node_foreach (ss_ref (node, i), func, data);
    }
}

void
ss_dict_foreach (ss_dict *d,
		 void (*func) (ss_val key, ss_val val, void *data), void *data)
{
  ss_dict_node_foreach (d->root, func, data);
}

static void
ss_dict_gc_set (ss_val key, ss_val val, void *data)
{
  ss_dict *d = (ss_dict *)data;
  ss_dict_set (d, ss_gc_copy (d->store, key), ss_gc_copy (d->store, val));
}

ss_val
ss_dict_gc_copy (ss_store *ss, ss_val node)
{
  ss_dict *d = ss_dict_init (ss, NULL);
  ss_dict_node_foreach (node, ss_dict_gc_set, d);
  return ss_dict_finish (d);
}

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
      printf (" head allc: %d\n", ss->head->alloced);
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
