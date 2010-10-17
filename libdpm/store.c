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
#include <unistd.h>

#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "dyn.h"
#include "store.h"

/* XXX - better support for unstored objects, including some form of
         garbage collection.
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
#define SS_TAG(o)             ((int)(SS_HEADER(o)>>24))
#define SS_LEN(o)             ((int)(SS_HEADER(o)&0xFFFFFF))
#define SS_SET_HEADER(o,t,l)  (SS_SET_WORD(o,0,((t)&0x7F) << 24 | (l)))

#define SS_BLOB_LEN_TO_WORDS(l)  (((l)+3)>>2)

#define SS_IS_FORWARD(o)    (SS_HEADER(o)&0x80000000)
#define SS_GET_FORWARD(o)   ((uint32_t)(SS_HEADER(o)&~0x80000000))
#define SS_SET_FORWARD2(o,f) (ss_set_forward_carefully (o, (uint32_t)(f)))
#define SS_SET_FORWARD(o,f) (SS_SET_WORD(o,0, 0x80000000 | (uint32_t)(f)))

#define SS_OFFSET(ss,obj)      (((uint32_t)obj)-((uint32_t)((ss)->head)))
#define SS_FROM_OFFSET(ss,off) ((ss_val)(((uint32_t)((ss)->head))+((uint32_t)off)))

#if 0
static void
ss_set_forward_carefully (void *o, uint32_t f)
{
  if (f & 0x80000000)
    abort ();
  SS_SET_FORWARD2(o,f);
}
#endif

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
  uint32_t counts[16];
};

/* The ss_store type
 */

static void ss_store_unref (dyn_type *, void *);

static int
ss_store_equal (void *a, void *b)
{
  return 0;
}

DYN_DEFINE_TYPE (ss_store, "struct-store");

struct ss_store_struct {
  ss_store next_store;

  char *filename;

  int fd;
  size_t file_size;      // in bytes
  struct ss_header *head;

  uint32_t *start;
  uint32_t *next;
  uint32_t *end;
  int alloced_words;
  uint32_t counts[16];
};

/* Opening stores.
 */

static ss_store all_stores = NULL;

#define MAX_SIZE    (512*1024*1024)
#define GROW_MASK   (2*1024*1024-1)
//#define GROW_MASK    (4*1024-1)        // for testing

static void
ss_grow (ss_store ss, size_t size)
{
  size = (size + GROW_MASK) & ~GROW_MASK;
  if (size >= MAX_SIZE)
    dyn_error ("%s has reached maximum size", ss->filename);

  if (size > ss->file_size)
    {
      if (ftruncate (ss->fd, size) < 0)
	dyn_error ("Can't grow %s: %m", ss->filename);
      ss->file_size = size;
      ss->end = (uint32_t *)((char *)ss->head + ss->file_size);
    }
}

static void
ss_sync (ss_store ss, uint32_t root_off)
{
  uint32_t *start = ss->start + ss->head->len;
  uint32_t *end = ss->next;
  
  if (end > start)
    {
      start = (uint32_t *)(((int)start) & ~4095);

      if (msync (start, (end - start)*sizeof(uint32_t), MS_SYNC) <0)
	dyn_error ("Can't sync %s: %m", ss->filename);
    }

  if (mmap (ss->head, sizeof (struct ss_header), PROT_READ | PROT_WRITE, 
	    MAP_SHARED | MAP_FIXED, ss->fd, 0)
      == MAP_FAILED)
    dyn_error ("Can't write-enable header of %s: %m", ss->filename);
  
  ss->head->len = ss->next - ss->start;
  ss->head->alloced = ss->alloced_words;
  ss->head->root = root_off;
  for (int i = 0; i < 16; i++)
    ss->head->counts[i] = ss->counts[i];

  if (msync (ss->head, sizeof (struct ss_header), MS_ASYNC) < 0)
    dyn_error ("Can't sync %s header: %m", ss->filename);

  if (mmap (ss->head, sizeof (struct ss_header), PROT_READ, 
	    MAP_SHARED | MAP_FIXED, ss->fd, 0)
      == MAP_FAILED)
    dyn_error ("Can't write-protect header of %s: %m", ss->filename);
}

ss_store 
ss_open (const char *filename, int mode)
{
  struct stat buf;
  int prot;

  ss_store ss;

  ss = dyn_new (ss_store);
  ss->next_store = NULL;
  ss->filename = dyn_strdup (filename);
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
    dyn_error ("Can't open %s: %m", filename);

  if (mode != SS_READ)
    {
      struct flock lock;
      lock.l_type = F_WRLCK;
      lock.l_whence = SEEK_SET;
      lock.l_start = 0;
      lock.l_len = sizeof (struct ss_header);

      if (fcntl (ss->fd, F_SETLK, &lock) == -1)
	dyn_error ("Can't lock %s: %m", filename);
    }

  if (mode == SS_READ)
    prot = PROT_READ;
  else
    prot = PROT_READ | PROT_WRITE;

  ss->head = mmap (NULL, MAX_SIZE, prot, MAP_SHARED,
		   ss->fd, 0);
  if (ss->head == MAP_FAILED)
    dyn_error ("Can't map %s: %m", ss->filename);

#if 0
  if (((unsigned long)ss->head) > (0x80000000 - MAX_SIZE))
    dyn_error ("Map too high: %x", ss->head);
#endif
    
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
      for (int i = 0; i < 16; i++)
	ss->head->counts[i] = 0;
    }
  else
    {
      if (ss->file_size < sizeof (struct ss_header)
	  || ss->head->magic != SS_MAGIC)
	dyn_error ("Not a struct-store file: %s", ss->filename);

      if (ss->head->version != SS_VERSION)
	dyn_error ("Unsupported struct-store format version in %s.  "
		   "Found %d, expected %d.",
		   ss->filename, ss->head->version, SS_VERSION);
    }

  ss->next = (uint32_t *)(((uint32_t)(ss->start + ss->head->len)+4095) & ~4095);
  for (int i = 0; i < 16; i++)
    ss->counts[i] = ss->head->counts[i];

  if (mmap (ss->head, (uint32_t)ss->next - (uint32_t)ss->head, PROT_READ, 
	    MAP_SHARED | MAP_FIXED, ss->fd, 0)
      == MAP_FAILED)
    dyn_error ("Can't write-protect %s: %m", ss->filename);

  ss->next_store = all_stores;
  all_stores = ss;

  return ss;
}

static void
ss_store_unref (dyn_type *type, void *object)
{
  ss_store ss = object;
  ss_store *sp;

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
}

ss_store 
ss_find_object_store (ss_val o)
{
  ss_store ss;

  for (ss = all_stores; ss; ss = ss->next_store)
    if ((char *)(ss->head + 1) <= (char *)o && (char *)o < (char *)ss->next)
      return ss;
  fprintf (stderr, "Object without store, aborting.\n");
  abort ();
}

/* The root
 */

ss_val 
ss_get_root (ss_store ss)
{
  if (ss->head->root == 0 || SS_IS_INT (ss->head->root))
    return (ss_val )ss->head->root;
  else
    return (ss_val )((char *)(ss->head) + ss->head->root);
}

void
ss_set_root (ss_store ss, ss_val root)
{
  uint32_t off;

  if (root == NULL || SS_IS_INT (root))
    off = (uint32_t)root;
  else
    off = (char *)root - (char *)(ss->head);

  ss_sync (ss, off);
}

/* Allocating new objects.
 */

static uint32_t *ss_alloc_unstored (size_t words);

static uint32_t *
ss_alloc (ss_store ss, size_t words)
{
  uint32_t *obj, *new_next;

  if (ss == NULL)
    return ss_alloc_unstored (words);

  new_next = ss->next + words;
  ss->alloced_words += words;
  if (new_next > ss->end)
    ss_grow (ss, (char *)new_next - (char *)ss->head);

  obj = ss->next;
  ss->next = new_next;
  
  return obj;
}

int
ss_is_stored (ss_store ss, ss_val obj)
{
  uint32_t *w = (uint32_t *)obj;
  return ss == NULL || (ss->start <= w && w < ss->next);
}

void
ss_assert_in_store (ss_store ss, ss_val obj)
{
  if (obj == NULL || ss_is_int (obj) || ss_is_stored (ss, obj))
    return;
  abort ();
  dyn_error ("Rogue pointer.");
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
 * tables and dictionaries are copied.
 */

#define WEAK_SETS_DISPATCH_TAG 0x77
#define WEAK_SETS_SEARCH_TAG   0x78
#define WEAK_DICT_DISPATCH_TAG 0x79
#define WEAK_DICT_SEARCH_TAG   0x7A
#define DICT_DISPATCH_TAG      0x7B
#define DICT_SEARCH_TAG        0x7C
#define TAB_DISPATCH_TAG       0x7D
#define TAB_SEARCH_TAG         0x7E

#define MAX_DELAYED 1024

typedef struct {
  ss_store from_store;
  ss_store to_store;
  int phase;
  int again;
  int n_delayed;
  ss_val delayed[MAX_DELAYED];
} ss_gc_data;

static void ss_set (ss_val obj, int i, ss_val ref);
static ss_val ss_dict_gc_copy (ss_gc_data *gc, ss_val dict);
static ss_val ss_tab_gc_copy (ss_gc_data *gc, ss_val tab);

int
ss_id (ss_store ss, ss_val x)
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
	  || ss_is (obj, WEAK_DICT_DISPATCH_TAG)
	  || ss_is (obj, WEAK_DICT_SEARCH_TAG)
	  || ss_is (obj, WEAK_SETS_DISPATCH_TAG)
	  || ss_is (obj, WEAK_SETS_SEARCH_TAG));
}

static int
ss_gc_alive_p (ss_gc_data *gc, ss_val obj)
{
  int i;

  if (obj == NULL || ss_is_int (obj))
    return 1;

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
    {
      uint32_t off = SS_GET_FORWARD (obj);
      if (off == 0)
	return NULL;
      else
	return SS_FROM_OFFSET (gc->to_store, off);
    }

  if (ss_is_stored (gc->to_store, obj))
    return obj;

  if (gc->phase == 0 && ss_gc_delay_p (obj))
   {
     if (gc->n_delayed >= MAX_DELAYED)
       dyn_error ("too many weak tables");
     gc->delayed[gc->n_delayed++] = obj;
     return obj;
   }

  if (ss_is (obj, TAB_DISPATCH_TAG)
      || ss_is (obj, TAB_SEARCH_TAG))
    return ss_tab_gc_copy (gc, obj);
  if (ss_is (obj, DICT_DISPATCH_TAG)
      || ss_is (obj, DICT_SEARCH_TAG)
      || ss_is (obj, WEAK_DICT_DISPATCH_TAG)
      || ss_is (obj, WEAK_DICT_SEARCH_TAG)
      || ss_is (obj, WEAK_SETS_DISPATCH_TAG)
      || ss_is (obj, WEAK_SETS_SEARCH_TAG))
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
	  uint32_t i;
	  copy = ss_alloc (gc->to_store, len + 1);
	  copy[0] = SS_HEADER(obj);
	  for (i = 0; i < len; i++)
	    {
	      ss_val val = ss_ref (obj, i);
	      if (i == 0 && SS_TAG(obj) >= 64 && SS_TAG(obj) < 80)
		val = ss_from_int (gc->to_store->counts[SS_TAG(obj)-64]++);
	      ss_set ((ss_val)copy, i, val);
	    }
	}

      SS_SET_FORWARD (obj, SS_OFFSET (gc->to_store, copy));
      return (ss_val)copy;
    }
}

static void ss_dict_node_foreach (void (*func) (ss_val key, ss_val val),
				  int dispatch_tag,
				  ss_val node);

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

  if (ss_is (node, WEAK_SETS_SEARCH_TAG)
      || ss_is (node, WEAK_SETS_DISPATCH_TAG))
    return SS_DICT_WEAK_SETS;

  abort ();
}

static int
ss_dict_dispatch_tag (int weak)
{
  if (weak == SS_DICT_STRONG)
    return DICT_DISPATCH_TAG;
  else if (weak == SS_DICT_WEAK_KEYS)
    return WEAK_DICT_DISPATCH_TAG;
  else if (weak == SS_DICT_WEAK_SETS)
    return WEAK_SETS_DISPATCH_TAG;
  else
    abort ();
}

static ss_val
ss_dict_gc_copy (ss_gc_data *gc, ss_val node)
{
  ss_val copy;
  int weak = ss_dict_weak_kind (node);
  ss_dict *d = ss_dict_init (gc->to_store, NULL, weak);

  dyn_foreach_x ((ss_val key, ss_val val),
		 ss_dict_node_foreach, ss_dict_dispatch_tag (weak), node)
    {
      if (weak == SS_DICT_STRONG)
	{
	  ss_dict_set (d, ss_gc_copy (gc, key), ss_gc_copy (gc, val));
	}
      else if (weak == SS_DICT_WEAK_KEYS)
	{
	  if (ss_gc_alive_p (gc, key))
	    ss_dict_set (d, ss_gc_copy (gc, key), ss_gc_copy (gc, val));
	}
      else if (weak == SS_DICT_WEAK_SETS)
	{
	  if (val)
	    {
	      int len = ss_len (val), n = 0;
	      ss_val new_elts[len];
	      for (int i = 0; i < len; i++)
		{
		  ss_val elt = ss_ref (val, i);
		  if (elt && ss_gc_alive_p (gc, elt))
		    new_elts[n++] = ss_gc_copy (gc, elt);
		}
	      if (n > 0)
		ss_dict_set (d, ss_gc_copy (gc, key),
			     ss_newv (gc->to_store,
				      ss_tag (val), n,
				      new_elts));
	    }
	}
      else
	abort ();
    }

  copy = ss_dict_finish (d);
  if (node)
    {
      if (copy)
	SS_SET_FORWARD (node, SS_OFFSET (gc->to_store, copy));
      else
	SS_SET_FORWARD (node, 0);
    }
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
      int len = ss_len (node), i, pos, n;
      uint32_t map;
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

  if (copy)
    SS_SET_FORWARD (node, SS_OFFSET (gc->to_store, copy));
  else
    SS_SET_FORWARD (node, 0);

  return copy;
}

static ss_val 
ss_gc_scan_and_advance (ss_gc_data *gc, ss_val obj)
{
  uint32_t len = SS_LEN (obj), i;

  if (SS_TAG (obj) != SS_BLOB_TAG)
    {
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
ss_gc_copy_phase (ss_gc_data *gc, ss_val root, int phase)
{
  gc->phase = phase;
  root = ss_gc_copy (gc, root);
  ss_gc_scan (gc);
  return root;
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
	  {
	    dyn_foreach_x ((ss_val key, ss_val val),
			   ss_dict_node_foreach, WEAK_DICT_DISPATCH_TAG, d)
	      {
		/* If the key is alive, we make sure that the value is
		   alive, too.  Since the value might be used in
		   another dict, we need to scan again.
		*/
		if (ss_gc_alive_p (gc, key)
		    && !ss_gc_alive_p (gc, val))
		  {
		    ss_gc_copy (gc, val);
		    gc->again = 1;
		  }
	      }
	  }
	else if (ss_is (d, WEAK_SETS_DISPATCH_TAG)
		 || ss_is (d, WEAK_SETS_SEARCH_TAG))
	  {
	    dyn_foreach_x ((ss_val key, ss_val val),
			   ss_dict_node_foreach, WEAK_SETS_DISPATCH_TAG, d)
	      {
		/* If any of the values are alive, we make sure that
		   the key is alive, too.  Since the key might be used
		   in another dict, we need to scan again.
		*/
		if (val && !ss_gc_alive_p (gc, key))
		  {
		    int len = ss_len (val);
		    for (int i = 0; i < len; i++)
		      if (ss_gc_alive_p (gc, ss_ref (val, i)))
			{
			  ss_gc_copy (gc, key);
			  gc->again = 1;
			  break;
			}
		  }
	      }
	  }
      }
  } while (gc->again);
}

ss_store 
ss_gc (ss_store ss)
{
  ss_gc_data gc;
  ss_val root;
  char *newfile;

  /* Disconnect old store from file.
   */
  if (mmap (ss->head, ss->file_size, PROT_READ | PROT_WRITE, 
	    MAP_PRIVATE | MAP_FIXED, ss->fd, 0)
      == MAP_FAILED)
    dyn_error ("Can't disconnect from %s: %m", ss->filename);

  asprintf (&newfile, "%s.gc", ss->filename);
  gc.from_store = ss;
  gc.to_store = ss_open (newfile, SS_TRUNC);
  gc.n_delayed = 0;

  root = ss_gc_copy_phase (&gc, ss_get_root (ss), 0);
  ss_gc_ripple_dicts (&gc);
  root = ss_gc_copy_phase (&gc, root, 2);

  gc.to_store->alloced_words = 0;
  ss_set_root (gc.to_store, root);

  /* Rename file
   */
  if (rename (gc.to_store->filename, ss->filename) < 0)
    dyn_error ("Can't rename %s to %s: %m",
	      gc.to_store->filename, ss->filename);

  free (gc.to_store->filename);
  gc.to_store->filename = ss->filename;
  ss->filename = NULL;

  return gc.to_store;
}

ss_store 
ss_maybe_gc (ss_store ss)
{
  if (ss->head->alloced > 5*1024*1024)
    {
      fprintf (stderr, "(Garbage collecting...");
      fflush (stderr);
      ss = ss_gc (ss);
      fprintf (stderr, ")\n");
    }

  return ss;
}

int
ss_tag_count (ss_store ss, int tag)
{
  if (tag >= 64 && tag < 80)
    return ss->counts[tag-64];
  else
    return 0;
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
    dyn_error ("Object of wrong type.");
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
ss_newv (ss_store ss, int tag, int len, ss_val *vals)
{
  uint32_t *w = ss_alloc (ss, len + 1);
  int i;

  if (tag >= 64 && tag < 80 && len > 0)
    vals[0] = ss_from_int (ss->counts[tag-64]++);

  SS_SET_HEADER (w, tag, len);
  for (i = 0; i < len; i++)
    {
      ss_assert_in_store (ss, vals[i]);
      ss_set ((ss_val )w, i, vals[i]);
    }
    
  return (ss_val )w;
}

ss_val 
ss_new (ss_store ss, int tag, int len, ...)
{
  uint32_t *w = ss_alloc (ss, len + 1);
  va_list ap;
  int i;

  va_start (ap, len);
  SS_SET_HEADER (w, tag, len);
  for (i = 0; i < len; i++)
    {
      ss_val val = va_arg (ap, ss_val);
      if (i == 0 && tag >= 64 && tag < 80)
	val = ss_from_int (ss->counts[tag-64]++);
      ss_assert_in_store (ss, val);
      ss_set ((ss_val )w, i, val);
    }
  va_end (ap);

  return (ss_val )w;
}

ss_val 
ss_make (ss_store ss, int tag, int len, ss_val init)
{
  int i;
  uint32_t *w = ss_alloc (ss, len + 1);

  ss_assert_in_store (ss, init);
  SS_SET_HEADER (w, tag, len);
  for (i = 0; i < len; i++)
    {
      ss_val val = init;
      if (i == 0 && tag >= 64 && tag < 80)
	val = ss_from_int (ss->counts[tag-64]++);
      ss_set ((ss_val )w, i, val);
    }

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
ss_blob_new (ss_store ss, int len, void *blob)
{
  uint32_t *w = ss_alloc (ss, SS_BLOB_LEN_TO_WORDS(len) + 1);

  SS_SET_HEADER(w, SS_BLOB_TAG, len);
  memcpy (w+1, blob, len);

  return (ss_val )w;
}

ss_val 
ss_copy (ss_store ss, ss_val obj)
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

/* Mutations and unstored objects.

   Normally, objects are inmutable.  However, you can create
   'unstored' objects which are not backed by any file and don't
   observe transactions etc.  No stored object can refer to an
   unstored object, of course.

   Unstored objects have the same layout as stored objects so that
   they can be used interchangeably with the stored objects.  They
   also support modifying their content and efficiently changing their
   size in small steps.

   The idea is that you create your data structure incrementally until
   all objects are in place and have the right size, and then ou store
   the whole thing in one go.  This is advantagous to doing it all in
   the store itself since much less garbage is created.

   (Creating a lot of garbage in a store is a problem since it can not
   be collected on-demand, only when the store is otherwise unused.)

   A unstored object is created when using NULL as the store with any
   of the object creation functions.

   The following mutation functions are capable of operating on both
   stored and unstored objects.  They generally create a new object
   that is the modified version of the input object.  If the input
   object is unstored, the functions will destroy it.
 */

static size_t
round_up_len (size_t n)
{
  if (n > 128)
    return (n + 1023) & ~1023;
  else if (n > 16)
    return 128;
  else
    return 16;
}

static ss_val
ss_realloc_unstored (ss_val obj, size_t words)
{
  /* Hopefully dyn_realloc will do the right thing quickly if the size
     doesn't actually change.  XXX - check that.
   */
  ss_val new_obj = dyn_realloc (obj, sizeof(uint32_t)*round_up_len (words));
  if (obj && new_obj != obj && SS_TAG (new_obj) != SS_BLOB_TAG)
    {
      /* Need to relocate all references.
       */
      uint32_t off = (char *)new_obj - (char *)obj;
      int len = SS_LEN (new_obj);
      uint32_t *elts = ((uint32_t *)new_obj) + 1;
      for (int i = 0; i < len; i++)
	{
	  if (elts[i] && !ss_is_int ((ss_val)elts[i]))
	    elts[i] -= off;
	}
    }
  return new_obj;
}

static uint32_t *
ss_alloc_unstored (size_t words)
{
  return (uint32_t *)ss_realloc_unstored (NULL, words);
}

static void
ss_free_unstored (ss_val obj)
{
  free (obj);
}

static void
ss_deep_free_unstored (ss_store ss, ss_val obj)
{
  if (obj == NULL || ss_is_int (obj) || ss_is_stored (ss, obj))
    return;
  
  if (!ss_is_blob (obj))
    {
      int len = ss_len(obj), i;
      for (i = 0; i < len; i++)
	ss_deep_free_unstored (ss, ss_ref (obj, i));
    }

  ss_free_unstored (obj);
}

static int
ss_is_unstored (ss_val obj)
{
  // XXX - makes this faster (but we usually have at most two stores,
  //       so this is not that horrible)

  for (ss_store s = all_stores; s; s = s->next_store)
    if (ss_is_stored (s, obj))
      return 0;
  return 1;
}

static ss_val 
ss_store_object (ss_store ss, ss_val obj)
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

  ss_free_unstored (obj);
  return copy;
}

ss_val 
ss_insert_many (ss_store ss, ss_val obj, int index, int n, ...)
{
  int obj_is_unstored = ss_is_unstored (obj);

  if (ss == NULL && obj_is_unstored)
    {
      int len = ss_len (obj);
      obj = ss_realloc_unstored (obj, len + n);
      SS_SET_HEADER (obj, SS_TAG (obj), len + n);
      uint32_t *elts = ((uint32_t *)obj)+1;
      memmove (elts + index + n, elts + index, sizeof(uint32_t)*(len-index));
      
      va_list ap;
      va_start (ap, n);
      for (int i = 0; i < n; i++)
	ss_set (obj, index+i, va_arg (ap, ss_val));
      va_end (ap);

      return obj;
    }
  else
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

      int tag = SS_TAG (obj);

      if (obj_is_unstored)
	ss_free_unstored (obj);

      return ss_newv (ss, tag, len + n, vals);
    }
}

ss_val 
ss_insert (ss_store ss, ss_val obj, int index, ss_val val)
{
  return ss_insert_many (ss, obj, index, 1, val);
}

ss_val
ss_append (ss_store ss, ss_val obj, ss_val val)
{
  return ss_insert_many (ss, obj, ss_len (obj), 1, val);
}

ss_val
ss_remove_many (ss_store ss, ss_val obj, int index, int n)
{
  int obj_is_unstored = ss_is_unstored (obj);

  if (ss == NULL && obj_is_unstored)
    {
      int len = ss_len (obj);
      SS_SET_HEADER (obj, SS_TAG (obj), len - n);
      uint32_t *elts = ((uint32_t *)obj)+1;
      memmove (elts + index, elts + index + n, sizeof(uint32_t)*(len-index-n));
      return obj;
    }
  else
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

      int tag = SS_TAG (obj);

      if (obj_is_unstored)
	ss_free_unstored (obj);

      return ss_newv (ss, tag, len - n, vals);
    }
}

static ss_val 
ss_unstore_object (ss_store ss, ss_val obj)
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
	h = (h<<8) + ss_hash (ss_ref (o, i));
      return h & 0x3FFFFFFF;
    }
}

static uint32_t
ss_id_hash (ss_store ss, ss_val o)
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

int
ss_equal_blob (ss_val b, int len, const void *blob)
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
ss_mapvec_set (ss_store ss, ss_val vec, int index, ss_val val)
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
		     ss_val (*action) (ss_store ss, ss_val node,
				       int hash, void *data),
		     ss_store ss,
		     ss_val node, int shift,
		     int hash, void *data)
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
ss_tab_intern_action (ss_store ss, ss_val node, int hash, void *data)
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
ss_tab_intern_blob_action (ss_store ss, ss_val node, int hash, void *data)
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
ss_tab_intern_soft_action (ss_store ss, ss_val node, int hash, void *data)
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
  ss_store store;
  ss_val root;
};

ss_tab *
ss_tab_init (ss_store ss, ss_val root)
{
  ss_tab *ot = dyn_malloc (sizeof (ss_tab));
  ot->store = ss;
  ot->root = root;
  return ot;
}

ss_val 
ss_tab_store (ss_tab *ot)
{
  ot->root = ss_store_object (ot->store, ot->root);
  return ot->root;
}

void
ss_tab_abort (ss_tab *ot)
{
  ss_deep_free_unstored (ot->store, ot->root);
  free (ot);
}

ss_val
ss_tab_finish (ss_tab *ot)
{
  ss_val r = ss_tab_store (ot);
  free (ot);
  return r;
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

static void
ss_tab_node_foreach (void (*func) (ss_val val), ss_val node)
{
  if (node == NULL)
    ;
  else if (ss_is (node, TAB_SEARCH_TAG))
    {
      int len = ss_len(node), i;
      for (i = 1; i < len; i++)
	func (ss_ref (node, i));
    }
  else
    {
      int len = ss_len(node), i;
      for (i = 1; i < len; i++)
	ss_tab_node_foreach (func, ss_ref (node, i));
    }
}

void
ss_tab_foreach (void (*func) (ss_val val), ss_tab *ot)
{
  ss_tab_node_foreach (func, ot->root);
}

static void
ss_tab_entries_micro_step (ss_tab_entries *iter)
{
  // Perform one micro step that walks us along the tree of dispatch
  // and search nodes.

  // If we are at the end of the current node, pop it and advance in
  // the lower level.
  //
  if (iter->index[iter->level] >= ss_len (iter->node[iter->level]))
    {
      iter->level -= 1;
      if (iter->level >= 0)
	iter->index[iter->level] += 1;
      return;
    }

  // If we are in a dispatch node, push to the next level (if there is
  // one), or advance.
  //
  if (ss_is (iter->node[iter->level], TAB_DISPATCH_TAG))
    {
      ss_val n = ss_ref (iter->node[iter->level], iter->index[iter->level]);
      if (n)
	{
	  iter->level += 1;
	  iter->node[iter->level] = n;
	  iter->index[iter->level] = 1;
	}
      else
	iter->index[iter->level] += 1;
      return;
    }

  // If we are in a search node, advance.
  //
  if (ss_is (iter->node[iter->level], TAB_SEARCH_TAG))
    {
      iter->index[iter->level] += 1;
      return;
    }

  abort ();
}

static bool
ss_tab_entries_hit (ss_tab_entries *iter)
{
  // We have a hit when we are inside a search node.
  ss_val n = iter->node[iter->level];
  return (iter->index[iter->level] < ss_len (n)
	  && ss_is (n, TAB_SEARCH_TAG));
}

void
ss_tab_entries_init (ss_tab_entries *iter, ss_tab *t)
{
  iter->tab = dyn_ref (t);
  if (t->root)
    {
      iter->level = 0;
      iter->node[0] = t->root;
      iter->index[0] = 1;
    }
  else
    iter->level = -1;

  while (!(ss_tab_entries_done (iter)
	   || ss_tab_entries_hit (iter)))
    ss_tab_entries_micro_step (iter);
}

void
ss_tab_entries_fini (ss_tab_entries *iter)
{
  dyn_unref (iter->tab);
}

void
ss_tab_entries_step (ss_tab_entries *iter)
{
  // Do micro steps until we have something or run out.
  //
  do {
    ss_tab_entries_micro_step (iter);
  } while (!(ss_tab_entries_done (iter)
	     || ss_tab_entries_hit (iter)));
}

bool
ss_tab_entries_done (ss_tab_entries *iter)
{
  return iter->level < 0;
}

ss_val
ss_tab_entries_elt (ss_tab_entries *iter)
{
  return ss_ref (iter->node[iter->level], iter->index[iter->level]);
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
  // static char spaces[21] = "                    ";

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
  ss_tab_stats stats = { 0, 0, 0, 0, 0, 0, 0 };
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
  ss_store store;
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
ss_dict_get_action (ss_store ss, ss_val node, int hash, void *data)
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
ss_dict_set_action (ss_store ss, ss_val node, int hash, void *data)
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
ss_set_add (ss_store ss, ss_val set, ss_val val)
{
  // XXX - quadratic complexity warning
  int len = ss_len (set), i;
  for (i = 0; i < len; i++)
    if (ss_ref (set, i) == val)
      return set;
  return ss_append (ss, set, val);
}

static ss_val
ss_set_rem (ss_store ss, ss_val set, ss_val val)
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
ss_dict_add_action (ss_store ss, ss_val node, int hash, void *data)
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
ss_dict_del_action (ss_store ss, ss_val node, int hash, void *data)
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
  return node;
}

ss_dict *
ss_dict_init (ss_store ss, ss_val root, int weak)
{
  ss_dict *d = dyn_malloc (sizeof (ss_dict));
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
  else if (weak == SS_DICT_WEAK_SETS)
    {
      d->search_tag = WEAK_SETS_SEARCH_TAG;
      d->dispatch_tag = WEAK_SETS_DISPATCH_TAG;      
    }
  else
    abort ();

  d->root = root;
  return d;
}

ss_val 
ss_dict_store (ss_dict *d)
{
  d->root = ss_store_object (d->store, d->root);
  return d->root;
}

void
ss_dict_abort (ss_dict *d)
{
  ss_deep_free_unstored (d->store, d->root);
  free (d);
}

ss_val
ss_dict_finish  (ss_dict *d)
{
  ss_val r = ss_dict_store (d);
  free (d);
  return r;
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
ss_dict_node_foreach (void (*func) (ss_val key, ss_val val),
		      int dispatch_tag, 
		      ss_val node)
{
  if (node == NULL)
    ;
  else if (!ss_is (node, dispatch_tag))
    {
      int len = ss_len(node), i;
      for (i = 1; i < len; i += 2)
	func (ss_ref (node, i), ss_ref (node, i+1));
    }
  else
    {
      int len = ss_len(node), i;
      for (i = 1; i < len; i++)
	ss_dict_node_foreach (func, dispatch_tag, ss_ref (node, i));
    }
}

void
ss_dict_foreach (void (*func) (ss_val key, ss_val val),
		  ss_dict *d)
{
  ss_dict_node_foreach (func, d->dispatch_tag, d->root);
}

/* Dict entries iterator.
 */

static void
ss_dict_entries_micro_step (ss_dict_entries *iter)
{
  // Perform one micro step that walks us along the tree of dispatch
  // and search nodes.

  // If we are at the end of the current node, pop it and advance in
  // the lower level.
  //
  if (iter->index[iter->level] >= ss_len (iter->node[iter->level]))
    {
      iter->level -= 1;
      if (iter->level >= 0)
	iter->index[iter->level] += 1;
      return;
    }

  // If we are in a dispatch node, push to the next level (if there is
  // one), or advance.
  //
  if (ss_is (iter->node[iter->level], iter->dict->dispatch_tag))
    {
      ss_val n = ss_ref (iter->node[iter->level], iter->index[iter->level]);
      if (n)
	{
	  iter->level += 1;
	  iter->node[iter->level] = n;
	  iter->index[iter->level] = 1;
	}
      else
	iter->index[iter->level] += 1;
      return;
    }

  // If we are in a search node, advance.
  //
  if (ss_is (iter->node[iter->level], iter->dict->search_tag))
    {
      iter->index[iter->level] += 2;
      return;
    }

  abort ();
}

static bool
ss_dict_entries_hit (ss_dict_entries *iter)
{
  // We have a hit when we are inside a search node.
  ss_val n = iter->node[iter->level];
  return (iter->index[iter->level] < ss_len (n)
	  && ss_is (n, iter->dict->search_tag));
}

void
ss_dict_entries_init (ss_dict_entries *iter, ss_dict *d)
{
  iter->dict = dyn_ref (d);
  if (d->root)
    {
      iter->level = 0;
      iter->node[0] = d->root;
      iter->index[0] = 1;
    }
  else
    iter->level = -1;

  while (!(ss_dict_entries_done (iter)
	   || ss_dict_entries_hit (iter)))
    ss_dict_entries_micro_step (iter);

  if (!ss_dict_entries_done (iter))
    {
      iter->key = ss_ref (iter->node[iter->level],
			  iter->index[iter->level]);
      iter->val = ss_ref (iter->node[iter->level],
			  iter->index[iter->level] + 1);
    }
}

void
ss_dict_entries_fini (ss_dict_entries *iter)
{
  dyn_unref (iter->dict);
}

void
ss_dict_entries_step (ss_dict_entries *iter)
{
  // Do micro steps until we have something or run out.
  //
  do {
    ss_dict_entries_micro_step (iter);
  } while (!(ss_dict_entries_done (iter)
	     || ss_dict_entries_hit (iter)));

  if (!ss_dict_entries_done (iter))
    {
      iter->key = ss_ref (iter->node[iter->level],
			  iter->index[iter->level]);
      iter->val = ss_ref (iter->node[iter->level],
			  iter->index[iter->level] + 1);
    }
}

bool
ss_dict_entries_done (ss_dict_entries *iter)
{
  return iter->level < 0;
}

void
ss_dict_entry_members_init (ss_dict_entry_members *iter, ss_dict *d)
{
  ss_dict_entries_init (&iter->entries, d);

  while (!ss_dict_entries_done (&iter->entries)
	 && ss_len (iter->entries.val) == 0)
    ss_dict_entries_step (&iter->entries);

  if (!ss_dict_entries_done (&iter->entries))
    {
      iter->index = 0;
      iter->key = iter->entries.key;
      iter->val = ss_ref (iter->entries.val, 0);
    }
}

void
ss_dict_entry_members_fini (ss_dict_entry_members *iter)
{
  ss_dict_entries_fini (&iter->entries);
}

void
ss_dict_entry_members_step (ss_dict_entry_members *iter)
{
  iter->index++;
  if (iter->index >= ss_len (iter->entries.val))
    {
      do {
	ss_dict_entries_step (&iter->entries);
      } while (!ss_dict_entries_done (&iter->entries)
	       && ss_len (iter->entries.val) == 0);

      if (ss_dict_entries_done (&iter->entries))
	return;

      iter->index = 0;
    }
  
  iter->key = iter->entries.key;
  iter->val = ss_ref (iter->entries.val, 0);
}

bool
ss_dict_entry_members_done (ss_dict_entry_members *iter)
{
  return ss_dict_entries_done (&iter->entries);
}

static ss_val
ss_dict_node_update (ss_store ss, int dispatch_tag, ss_val node,
		     ss_val (*func) (ss_val key, ss_val val, void *data),
		     void *data)
{
  if (node == NULL)
    return node;
  else if (!ss_is (node, dispatch_tag))
    {
      int len = ss_len(node), i;
      for (i = 1; i < len; i += 2)
	{
	  ss_val old_val = ss_ref (node, i+1), new_val;
	  new_val = func (ss_ref (node, i), old_val, data);
	  if (new_val != old_val)
	    {
	      if (new_val)
		{
		  node = ss_unstore_object (ss, node);
		  ss_set (node, i+1, new_val);
		}
	      else
		{
		  node = ss_remove_many (NULL, node, i, 2);
		  i -= 2;
		  len -= 2;
		}
	    }
	}
      return node;
    }
  else
    {
      int len = ss_len(node), i;
      for (i = 1; i < len; i++)
	{
	  ss_val old_val = ss_ref (node, i), new_val;
	  new_val = ss_dict_node_update (ss, dispatch_tag, old_val, func, data);
	  if (new_val != old_val)
	    {
	      node = ss_unstore_object (ss, node);
	      ss_set (node, i, new_val);
	    }
	}
      return node;
    }
}

void
ss_dict_update (ss_dict *d,
		ss_val (*func) (ss_val key, ss_val val, void *data), void *data)
{
  d->root = ss_dict_node_update (d->store, d->dispatch_tag, d->root,
				 func, data);
}

void
ss_dict_foreach_member (void (*func) (ss_val key, ss_val val),
			ss_dict *d)
{
  dyn_foreach_x ((ss_val key, ss_val val),
		 ss_dict_foreach, d)
    {
      if (val)
	{
	  int len = ss_len (val);
	  for (int i = 0; i < len; i++)
	    {
	      ss_val member = ss_ref (val, i);
	      if (member)
		func (key, member);
	    }
	}
    }
}

struct update_members_data {
  ss_store ss;
  ss_val (*func) (ss_val key, ss_val val, void *data);
  void *data;
};

static ss_val
update_members (ss_val key, ss_val val, void *data)
{
  struct update_members_data *umd = data;

  if (val)
    {
      int len = ss_len (val);
      for (int i = 0; i < len; i++)
	{
	  ss_val member = ss_ref (val, i);
	  if (member)
	    {
	      ss_val new_member = umd->func (key, member, umd->data);
	      if (new_member != member)
		{
		  if (new_member)
		    {
		      val = ss_unstore_object (umd->ss, val);
		      ss_set (val, i, new_member);
		    }
		  else
		    {
		      val = ss_remove_many (NULL, val, i, 1);
		      i -= 1;
		      len -= 1;
		    }
		}
	    }
	}
    }
  return val;
}

void
ss_dict_update_members (ss_dict *d, 
			ss_val (*func) (ss_val key, ss_val val, void *data),
			void *data)
{
  struct update_members_data umd;
  umd.ss = d->store;
  umd.func = func;
  umd.data = data;
  ss_dict_update (d, update_members, &umd);
}

ss_val
ss_ref_safely (ss_val obj, int i)
{
  if (obj && !ss_is_blob (obj) && i < ss_len (obj))
    return ss_ref (obj, i);
  return NULL;
}

int
ss_streq (ss_val obj, const char *str)
{
  int len = strlen (str);
  return (obj
	  && ss_is_blob (obj)
	  && ss_len (obj) == len
	  && memcmp (ss_blob_start (obj), str, len) == 0);
}

/* Debugging
 */

void
ss_dump_store (ss_store ss, const char *header)
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

  for (int i = 0; i < 16; i++)
    if (ss->counts[i])
      printf (" counts[%d]: %d\n", i, ss->counts[i]); 
}

void
ss_scan_store (ss_store ss)
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
