/*
 * frame.c
 *
 *  Created on: Dec 29, 2019
 *      Author: Beshoy Saad
 */

#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include <stdio.h>
#include "frame.h"

static struct hash frame_table;
static struct lock frame_table_lock;

/* Returns a hash value for frame f */
static unsigned
frame_hash (const struct hash_elem *f_, void *aux UNUSED)
{
  const struct frame *f = hash_entry(f_, struct frame, h_elem);
  uint32_t key = (uint32_t) (f->kernel_address);
  return hash_bytes (&key, sizeof key);
}

/* Returns true if frame a precedes frame b */
static bool
frame_less (const struct hash_elem *a_, const struct hash_elem *b_,
	    void *aux UNUSED)
{
  const struct frame *a = hash_entry(a_, struct frame, h_elem);
  const struct frame *b = hash_entry(b_, struct frame, h_elem);

  return a->kernel_address < b->kernel_address;
}

void
frame_table_init (void)
{
  lock_init (&frame_table_lock);
  hash_init (&frame_table, frame_hash, frame_less, NULL);
}

struct frame*
frame_alloc (bool zeroed)
{
  enum palloc_flags flags = PAL_USER | (zeroed ? PAL_ZERO : 0);
  struct frame *f = (struct frame*) malloc (sizeof(struct frame));
  ASSERT(f != NULL);
  f->user_page = NULL;
  f->kernel_address = palloc_get_page (flags);
  if (f->kernel_address == NULL)
    {
      printf("Out of pages!\r\n");
      free(f);
      // TODO evict something
      return NULL;
    }
  else
    {
      lock_acquire (&frame_table_lock);
      ASSERT(hash_insert (&frame_table, &f->h_elem) == NULL);
      lock_release (&frame_table_lock);
      return f;
    }
}

void
frame_free (void *kaddr)
{
  if (kaddr == NULL)
    {
      return;
    }
  struct frame f;
  f.kernel_address = kaddr;
  struct hash_elem *e;
  lock_acquire (&frame_table_lock);
  e = hash_delete (&frame_table, &f.h_elem);
  lock_release (&frame_table_lock);
  if (e != NULL)
    {
      struct frame *g = hash_entry(e, struct frame, h_elem);
      palloc_free_page (g->kernel_address);
      free (g);
    }
}

