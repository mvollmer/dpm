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
         much of the file read-only as possible.  As soon as next
         crosses a page boundary.

   XXX - free unstored objects in all the right places.

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
 *
 * Dictionaries use object addresses as hash values, and the GC moves
 * objects around.  Thus, dictionaries need to be rehashed.  We do
 * this by simply iterating over all entries in the dictionary and
 * inserting them in a new dictionary in the to-store.
 *
 * Tables and certain dictionaries have weak references.  This is
 * implemented by delaying to copy them until all non-weak references
 * have been copied.  Then the tables are copied and references in
 * them that have no forwarding pointer will be replaced with null.
 *
 * A complication is that tables can reference other tables, and those
 * tables might not have a forwarding pointer yet (because copying has
 * been delayed).
 *
 * The GC then happens in three phases: first the root and all
 * referenced objects except tables and dictionaries with weak
 * references are copied; then a couple of rounds are made over the
 * dictionaries until the ripples have settled; then the delayed
 * tables and dictionries are copied.
 */

#define WEAK_DICT_DISPATCH_TAG 0x79
#define WEAK_DICT_SEARCH_TAG   0x7A
#define DICT_DISPATCH_TAG      0x7B
#define DICT_SEARCH_TAG        0x7C
#define TAB_DISPATCH_TAG       0x7D
#define TAB_SEARCH_TAG         0x7E

#define MAX_DELAYED 1024

typedef struct {
  ss_store *from_store;
  ss_store *to_store;
  int phase;
  int again;
  int n_delayed;
  ss_val delayed[MAX_DELAYED];
} ss_gc_data;

static void ss_set (ss_val obj, int i, ss_val ref);
static ss_val ss_dict_gc_copy (ss_gc_data *gc, ss_val dict);
static ss_val ss_tab_gc_copy (ss_gc_data *gc, ss_val tab);

int
ss_id (ss_store *ss, ss_val x)
{
  if (x == NULL || ss_is_int (x))
    return -1;
  return ((uint32_t *)x) - ss->start;
}

static int
ss_gc_delay_p (ss_val obj)
{
  return (ss_is (obj, TAB_DISPATCH_TAG)
	  || ss_is (obj, TAB_SEARCH_TAG)
	  || ss_is (obj, WEAK_DICT_SEARCH_TAG)
	  || ss_is (obj, WEAK_DICT_SEARCH_TAG));
}

static int
ss_gc_alive_p (ss_gc_data *gc, ss_val obj)
{
  int i;

  if (SS_IS_FORWARD (obj))
    return 1;

  if (!ss_gc_delay_p (obj))
    return 0;

  for (i =0; i < gc->n_delayed; i++)
    if (gc->delayed[i] == obj)
      {
	fprintf (stderr, "alive delayed\n");
	return 1;
      }

  return 0;
}

static ss_val 
ss_gc_copy (ss_gc_data *gc, ss_val obj)
{
  uint32_t len;
  uint32_t *copy;

  if (obj == NULL || SS_IS_INT (obj))
    return obj;

  if (SS_IS_FORWARD (obj))
    return (ss_val)SS_GET_FORWARD (obj);

  if (ss_is_stored (gc->to_store, obj))
    return obj;

  if (gc->phase == 0 && ss_gc_delay_p (obj))
   {
     if (gc->n_delayed >= MAX_DELAYED)
       ss_abort (gc->to_store, "too many weak tables\n");
     gc->delayed[gc->n_delayed++] = obj;
     return obj;
   }

  if (ss_is (obj, TAB_DISPATCH_TAG)
      || ss_is (obj, TAB_SEARCH_TAG))
    return ss_tab_gc_copy (gc, obj);
  if (ss_is (obj, DICT_DISPATCH_TAG)
      || ss_is (obj, DICT_SEARCH_TAG)
      || ss_is (obj, WEAK_DICT_DISPATCH_TAG)
      || ss_is (obj, WEAK_DICT_SEARCH_TAG))
    return ss_dict_gc_copy (gc, obj);
  else
    {
      len = SS_LEN (obj);
      if (SS_TAG (obj) == SS_BLOB_TAG)
	{
	  len = SS_BLOB_LEN_TO_WORDS (len);
	  copy = ss_alloc (gc->to_store, len + 1);
	  memcpy (copy, obj, (len+1)*sizeof(uint32_t));
	}
      else
	{
	  int i;
	  copy = ss_alloc (gc->to_store, len + 1);
	  copy[0] = SS_HEADER(obj);
	  for (i = 0; i < len; i++)
	    ss_set ((ss_val)copy, i, ss_ref (obj, i));
	}

      SS_SET_FORWARD (obj, copy);
      return (ss_val)copy;
    }
}

typedef struct {
  ss_gc_data *gc;
  ss_dict *d;
  int weak;
} ss_gc_dict_data;

static void ss_dict_node_foreach (int dispatch_tag,
				  ss_val node,
				  void (*func) (ss_val key, ss_val val,
						void *data),
				  void *data);

static void
ss_dict_gc_set (ss_val key, ss_val val, void *data)
{
  ss_gc_dict_data *dd = (ss_gc_dict_data *)data;
  if (dd->weak == SS_DICT_STRONG || ss_gc_alive_p (dd->gc, key))
    ss_dict_set (dd->d, ss_gc_copy (dd->gc, key), ss_gc_copy (dd->gc, val));
}

static int
ss_dict_weak_kind (ss_val node)
{
  if (node == NULL
      || ss_is (node, DICT_SEARCH_TAG)
      || ss_is (node, DICT_DISPATCH_TAG))
    return SS_DICT_STRONG;

  if (ss_is (node, WEAK_DICT_SEARCH_TAG)
      || ss_is (node, WEAK_DICT_DISPATCH_TAG))
    return SS_DICT_WEAK_KEYS;

  abort ();
}

static int
ss_dict_dispatch_tag (int weak)
{
  if (weak == SS_DICT_STRONG)
    return DICT_DISPATCH_TAG;
  else if (weak == SS_DICT_WEAK_KEYS)
    return WEAK_DICT_DISPATCH_TAG;
  else
    abort ();
}

static ss_val
ss_dict_gc_copy (ss_gc_data *gc, ss_val node)
{
  ss_val copy;
  ss_gc_dict_data dd;
  dd.gc = gc;
  dd.weak = ss_dict_weak_kind (node);
  dd.d = ss_dict_init (gc->to_store, NULL, dd.weak);
  ss_dict_node_foreach (ss_dict_dispatch_tag (dd.weak),
			node, ss_dict_gc_set, &dd);

  copy = ss_dict_finish (dd.d);
  if (node)
    SS_SET_FORWARD (node, copy);
  return copy;
}

static ss_val
ss_tab_gc_copy (ss_gc_data *gc, ss_val node)
{
  ss_val copy;

  if (ss_is (node, TAB_SEARCH_TAG))
    {
      int len = ss_len (node), i, n;
      ss_val vals[len];

      n = 1;
      vals[0] = ss_ref (node, 0);
      for (i = 1; i < len; i++)
	{
	  ss_val x = ss_ref (node, i);
	  if (ss_gc_alive_p (gc, x))
	    vals[n++] = ss_gc_copy (gc, x);
	}

      if (n > 1)
	copy = ss_newv (gc->to_store, TAB_SEARCH_TAG, n, vals);
      else
	copy = NULL;
    }
  else
    {
      int len = ss_len (node), i, map, pos, n;
      ss_val vals[len];
      
      map = ss_to_int (ss_ref (node, 0)) | 0xC0000000;
      pos = 1;
      n = 1;
      for (i = 0; i < 32; i++)
	{
	  int bit = (1 << i);
	  if (map & bit)
	    {
	      ss_val x = ss_ref (node, pos++);
	      ss_val y = x? ss_tab_gc_copy (gc, x) : NULL;
	      if (y == NULL && i < 30)
		map &= ~bit;
	      else
		vals[n++] = y;
	    }
	}

      if (map == 0xC0000000 && vals[1] == NULL && vals[2] == NULL)
	copy = NULL;
      else
	{
	  vals[0] = ss_from_int (map);
	  copy = ss_newv (gc->to_store, TAB_DISPATCH_TAG, n, vals);
	}
    }

  SS_SET_FORWARD (node, copy);
  return copy;
}

static ss_val 
ss_gc_scan_and_advance (ss_gc_data *gc, ss_val obj)
{
  uint32_t len = SS_LEN (obj), i;

  if (SS_TAG (obj) != SS_BLOB_TAG)
    {
      uint32_t *w = (uint32_t *)obj;
      for (i = 0; i < len; i++)
	ss_set (obj, i, ss_gc_copy (gc, ss_ref (obj, i)));
    }
  else
    len = SS_BLOB_LEN_TO_WORDS (len);
  
  return (ss_val )(((uint32_t *)obj) + len + 1);
}

static void
ss_gc_scan (ss_gc_data *gc)
{
  ss_val to_ptr;
  for (to_ptr = (ss_val)gc->to_store->start;
       to_ptr < (ss_val)gc->to_store->next;
       to_ptr = ss_gc_scan_and_advance (gc, to_ptr))
    ;
}

static ss_val
ss_gc_copy_root (ss_gc_data *gc)
{
  ss_val new_root;

  gc->phase = 0;
  new_root = ss_gc_copy (gc, ss_get_root (gc->from_store));
  ss_gc_scan (gc);
  return new_root;
}

static void
ss_gc_dict_ripple_entry (ss_val key, ss_val val, void *data)
{
  ss_gc_data *gc = (ss_gc_data *)data;

  /* If the key is alive, we make sure that the value is alive, too.
     Since the value might be used as a key in another dict, we need
     to scan again.
   */
  if (ss_gc_alive_p (gc, key)
      && !ss_gc_alive_p (gc, val))
    {
      ss_gc_copy (gc, val);
      gc->again = 1;
    }
}

static void
ss_gc_ripple_dicts (ss_gc_data *gc)
{
  int i;

  gc->phase = 1;
  do {
    gc->again = 0;
    for (i = 0; i < gc->n_delayed; i++)
      {
	ss_val d = gc->delayed[i];
	if (ss_is (d, WEAK_DICT_DISPATCH_TAG)
	    || ss_is (d, WEAK_DICT_SEARCH_TAG))
	  ss_dict_node_foreach (WEAK_DICT_DISPATCH_TAG,
				d, ss_gc_dict_ripple_entry, gc);
      }
  } while (gc->again);
}


static void
ss_gc_copy_delayed (ss_gc_data *gc)
{
  gc->phase = 2;
  ss_gc_scan (gc);
}

ss_store *
ss_gc (ss_store *ss)
{
  ss_gc_data gc;
  ss_val new_root;
  char *newfile;
  int old_n_delayed;

  /* Disconnect old store from file.
   */
  if (mmap (ss->head, ss->file_size, PROT_READ | PROT_WRITE, 
	    MAP_PRIVATE | MAP_FIXED, ss->fd, 0)
      == MAP_FAILED)
    ss_abort (ss, "Can't disconnect from %s: %m", ss->filename);

  asprintf (&newfile, "%s.gc", ss->filename);
  gc.from_store = ss;
  gc.to_store = ss_open (newfile, SS_TRUNC, NULL);
  gc.n_delayed = 0;

  new_root = ss_gc_copy_root (&gc);
  ss_gc_ripple_dicts (&gc);
  ss_gc_copy_delayed (&gc);

  gc.to_store->alloced_words = 0;
  ss_set_root (gc.to_store, new_root);

  /* Rename file
   */
  if (rename (gc.to_store->filename, ss->filename) < 0)
    ss_abort (ss, "Can't rename %s to %s: %m",
	      gc.to_store->filename, ss->filename);

  free (gc.to_store->filename);
  gc.to_store->filename = ss->filename;
  ss->filename = NULL;
  ss_close (ss);

  return gc.to_store;
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

ss_val
ss_remove_many (ss_store *ss, ss_val obj, int index, int n)
{
  int len = ss_len (obj), i;
  ss_val vals[len-n];

  i = 0;
  while (i < index)
    {
      vals[i] = ss_ref (obj, i);
      i++;
    }
  while (i < len - n)
    {
      vals[i] = ss_ref (obj, i + n);
      i++;
    }

  return ss_newv (ss, ss_tag (obj), len - n, vals);
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

  if (val)
    {
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
    }
  else
    {
      if (map & bit)
	{
	  if (index < 30)
	    {
	      new_vec = ss_remove_many (NULL, vec, pos, 1);
	      if (!ss_is_stored (ss, vec))
		free (vec);
	      map &= ~bit;
	      ss_set (new_vec, 0, ss_from_int (map));
	    }
	  else
	    {
	      new_vec = ss_unstore_object (ss, vec);
	      ss_set (new_vec, pos, val);
	    }
	}
      else
	new_vec = vec;
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

static ss_val 
ss_hash_node_lookup (int dispatch_tag,
		     ss_val (*action) (ss_store *ss, ss_val node,
				       int hash, void *data),
		     ss_store *ss,
		     ss_val node, int shift,
		     uint32_t hash, void *data)
{
  if (node == NULL)
    return action (ss, node, hash, data);
  else if (!ss_is (node, dispatch_tag))
    {
      if (ss_to_int (ss_ref (node, 0)) == hash)
	return action (ss, node, hash, data);
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
	  new_node = ss_mapvec_new (dispatch_tag);
	  new_node = ss_mapvec_set (ss, new_node, node_index, node);
	  entry = ss_mapvec_get (new_node, obj_index);
	  new_entry = ss_hash_node_lookup (dispatch_tag, action,
					   ss,
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
      new_entry = ss_hash_node_lookup (dispatch_tag, action,
				       ss,
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

ss_val
ss_tab_intern_action (ss_store *ss, ss_val node, int hash, void *data)
{
  ss_val *objp = (ss_val *)data;
  ss_val obj = *objp;

  if (node == NULL)
    {
      obj = ss_store_object (ss, obj);
      *objp = obj;
      return ss_new (NULL, TAB_SEARCH_TAG, 2, ss_from_int (hash), obj);
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

typedef struct {
  int len;
  void *blob;
  ss_val obj;
} ss_tab_intern_blob_data;

ss_val
ss_tab_intern_blob_action (ss_store *ss, ss_val node, int hash, void *data)
{
  ss_tab_intern_blob_data *d = (ss_tab_intern_blob_data *)data;

  if (node == NULL)
    {
      d->obj = ss_blob_new (ss, d->len, d->blob);
      return ss_new (NULL, TAB_SEARCH_TAG, 2, ss_from_int (hash), d->obj);
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

ss_val
ss_tab_intern_soft_action (ss_store *ss, ss_val node, int hash, void *data)
{
  ss_tab_intern_blob_data *d = (ss_tab_intern_blob_data *)data;

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

struct ss_tab {
  ss_store *store;
  ss_val root;
};

ss_tab *
ss_tab_init (ss_store *ss, ss_val root)
{
  ss_tab *ot = xmalloc (sizeof (ss_tab));
  ot->store = ss;
  ot->root = root;
  return ot;
}

ss_val 
ss_tab_finish (ss_tab *ot)
{
  ss_val root;

  root = ss_store_object (ot->store, ot->root);
  free (ot);

  return root;
}

ss_val 
ss_tab_intern (ss_tab *ot, ss_val obj)
{
  uint32_t h = ss_hash (obj);
  ot->root = ss_hash_node_lookup (TAB_DISPATCH_TAG,
				  ss_tab_intern_action,
				  ot->store, ot->root, 0, h, &obj);
  return obj;
}

ss_val 
ss_tab_intern_blob (ss_tab *ot, int len, void *blob)
{
  ss_tab_intern_blob_data d = { len, blob, NULL };
  uint32_t h = ss_hash_blob (len, blob);
  ot->root = ss_hash_node_lookup (TAB_DISPATCH_TAG,
				  ss_tab_intern_blob_action,
				  ot->store, ot->root, 0, h, &d);
  return d.obj;
}

ss_val 
ss_tab_intern_soft (ss_tab *ot, int len, void *blob)
{
  ss_tab_intern_blob_data d = { len, blob, NULL };
  uint32_t h = ss_hash_blob (len, blob);
  ot->root = ss_hash_node_lookup (TAB_DISPATCH_TAG,
				  ss_tab_intern_soft_action,
				  ot->store, ot->root, 0, h, &d);
  return d.obj;
}

void
ss_tab_node_foreach (ss_val node,
			void (*func) (ss_val , void *data), void *data)
{
  if (node == NULL)
    ;
  else if (ss_is (node, TAB_SEARCH_TAG))
    {
      int len = ss_len(node), i;
      for (i = 1; i < len; i++)
	func (ss_ref (node, i), data);
    }
  else
    {
      int len = ss_len(node), i;
      for (i = 1; i < len; i++)
	ss_tab_node_foreach (ss_ref (node, i), func, data);
    }
}

void
ss_tab_foreach (ss_tab *ot,
		   void (*func) (ss_val , void *data), void *data)
{
  ss_tab_node_foreach (ot->root, func, data);
}

typedef struct {
  int n_leaves;
  int n_leaf_nodes;
  int n_dispatches;
  int n_dispatch_nodes;
  int n_dispatch_slots;
  int max_level;
  int max_collisions;
} ss_tab_stats;

static void
ss_tab_dump_node (ss_val n, int level, ss_tab_stats *stats)
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
      
      if (ss_tag (n) == TAB_SEARCH_TAG)
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
		ss_tab_dump_node (ss_ref (n, i), level+1, stats);
	      }
	}
    }
}

void
ss_tab_dump (ss_tab *ot)
{
  ss_tab_stats stats = { 0, 0, 0, 0, 0, 0 };
  ss_tab_dump_node (ot->root, 0, &stats);

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

struct ss_dict {
  ss_store *store;
  int dispatch_tag;
  int search_tag;
  ss_val root;
};

typedef struct {
  ss_dict *d;
  ss_val key;
  ss_val val;
} ss_dict_action_data;

ss_val
ss_dict_get_action (ss_store *ss, ss_val node, int hash, void *data)
{
  ss_dict_action_data *d = (ss_dict_action_data *)data;

  if (node == NULL)
    d->val = NULL;
  else
    {
      int len = ss_len (node), i;
      d->val = NULL;
      for (i = 1; i < len; i += 2)
	if (ss_ref (node, i) == d->key)
	  {
	    ss_val x = ss_ref (node, i+1);
	    d->val = ss_store_object (ss, x);
	    if (d->val != x)
	      ss_set (node, i+1, d->val);
	    break;
	  }
    }

  return node;
}

ss_val
ss_dict_set_action (ss_store *ss, ss_val node, int hash, void *data)
{
  ss_dict_action_data *d = (ss_dict_action_data *)data;

  if (node == NULL)
    {
      if (d->val == NULL)
	return NULL;
      else
	return ss_new (NULL, d->d->search_tag, 3, ss_from_int (hash),
		       d->key, d->val);
    }
  else
    {
      int len = ss_len (node), i;
      for (i = 1; i < len; i += 2)
	if (ss_ref (node, i) == d->key)
	  {
	    if (d->val == NULL)
	      {
		if (len == 3)
		  return NULL;
		node = ss_remove_many (NULL, node, i, 2);
		return node;
	      }
	    else
	      {
		node = ss_unstore_object (ss, node);
		ss_set (node, i+1, d->val);
		return node;
	      }
	  }
      
      return ss_insert_many (NULL, node, len, 2, d->key, d->val);
    }
}

static ss_val
ss_set_add (ss_store *ss, ss_val set, ss_val val)
{
  int len = ss_len (set), i;
  for (i = 0; i < len; i++)
    if (ss_ref (set, i) == val)
      return set;
  return ss_insert (ss, set, 0, val);
}

static ss_val
ss_set_rem (ss_store *ss, ss_val set, ss_val val)
{
  int len = ss_len (set), i;
  for (i = 0; i < len; i++)
    if (ss_ref (set, i) == val)
      {
	if (len == 1)
	  return NULL;
	else
	  return ss_remove_many (ss, set, i, 1);
      }
  return set;
}

ss_val
ss_dict_add_action (ss_store *ss, ss_val node, int hash, void *data)
{
  ss_dict_action_data *d = (ss_dict_action_data *)data;

  if (node == NULL)
    {
      ss_val set = ss_new (NULL, 0, 1, d->val);
      return ss_new (NULL, d->d->search_tag, 3, ss_from_int (hash),
		     d->key, set);
    }
  else
    {
      int len = ss_len (node), i;
      for (i = 1; i < len; i += 2)
	if (ss_ref (node, i) == d->key)
	  {
	    ss_val set = ss_ref (node, i+1);
	    ss_val new_set = ss_set_add (NULL, set, d->val);
	    if (new_set != set)
	      {
		node = ss_unstore_object (ss, node);
		ss_set (node, i+1, new_set);
	      }
	    return node;
	  }
      
      return ss_insert_many (NULL, node, len, 2,
			     d->key, ss_new (NULL, 0, 1, d->val));
    }
}

ss_val
ss_dict_del_action (ss_store *ss, ss_val node, int hash, void *data)
{
  ss_dict_action_data *d = (ss_dict_action_data *)data;

  if (node)
    {
      int len = ss_len (node), i;
      for (i = 1; i < len; i += 2)
	if (ss_ref (node, i) == d->key)
	  {
	    ss_val set = ss_ref (node, i+1);
	    ss_val new_set = ss_set_rem (NULL, set, d->val);
	    if (new_set != set)
	      {
		if (new_set)
		  {
		    node = ss_unstore_object (ss, node);
		    ss_set (node, i+1, new_set);
		  }
		else
		  {
		    if (len == 3)
		      return NULL;
		    node = ss_remove_many (NULL, node, i, 2);
		  }
	      }
	    return node;
	  }
      
      return node;
    }
}

ss_dict *
ss_dict_init (ss_store *ss, ss_val root, int weak)
{
  ss_dict *d = xmalloc (sizeof (ss_dict));
  d->store = ss;
  if (weak == SS_DICT_STRONG)
    {
      d->search_tag = DICT_SEARCH_TAG;
      d->dispatch_tag = DICT_DISPATCH_TAG;
    }
  else if (weak == SS_DICT_WEAK_KEYS)
    {
      d->search_tag = WEAK_DICT_SEARCH_TAG;
      d->dispatch_tag = WEAK_DICT_DISPATCH_TAG;      
    }
  else
    abort ();

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
  ss_dict_action_data ad = { d, key, NULL };
  uint32_t h = ss_id_hash (d->store, key);
  ss_hash_node_lookup (d->dispatch_tag, ss_dict_get_action,
		       d->store, d->root, 0, h, &ad);
  return ad.val;
}

void
ss_dict_set (ss_dict *d, ss_val key, ss_val val)
{
  ss_dict_action_data ad = { d, key, val };
  uint32_t h = ss_id_hash (d->store, key);
  d->root = ss_hash_node_lookup (d->dispatch_tag, ss_dict_set_action,
				 d->store, d->root, 0, h, &ad);
}

void
ss_dict_add (ss_dict *d, ss_val key, ss_val val)
{
  ss_dict_action_data ad = { d, key, val };
  uint32_t h = ss_id_hash (d->store, key);
  d->root = ss_hash_node_lookup (d->dispatch_tag, ss_dict_add_action,
				 d->store, d->root, 0, h, &ad);
}

void
ss_dict_del (ss_dict *d, ss_val key, ss_val val)
{
  ss_dict_action_data ad = { d, key, val };
  uint32_t h = ss_id_hash (d->store, key);
  d->root = ss_hash_node_lookup (d->dispatch_tag, ss_dict_del_action,
				 d->store, d->root, 0, h, &ad);
}

static void
ss_dict_node_foreach (int dispatch_tag, 
		      ss_val node,
		      void (*func) (ss_val key, ss_val val, void *data),
		      void *data)
{
  if (node == NULL)
    ;
  else if (!ss_is (node, dispatch_tag))
    {
      int len = ss_len(node), i;
      for (i = 1; i < len; i += 2)
	func (ss_ref (node, i), ss_ref (node, i+1), data);
    }
  else
    {
      int len = ss_len(node), i;
      for (i = 1; i < len; i++)
	ss_dict_node_foreach (dispatch_tag, ss_ref (node, i), func, data);
    }
}

void
ss_dict_foreach (ss_dict *d,
		 void (*func) (ss_val key, ss_val val, void *data), void *data)
{
  ss_dict_node_foreach (d->dispatch_tag, d->root, func, data);
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
