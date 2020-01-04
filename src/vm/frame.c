/*
 * frame.c
 *
 *  Created on: Dec 29, 2019
 *      Author: Beshoy Saad
 */

#include "hash.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "frame.h"

struct frame
{
  struct hash_elem h_elem; /* Hash table element */
  void *kernel_address; /* Kernel virtual address of this frame */
  void *user_address; /* User virtual address of the page occupying this frame */
  uint8_t unaccessed_count; /* Used in implementing the clock eviction algorithm */
  struct thread *t; /* Pointer to the thread owning the page in this frame */
};

static struct hash frames;
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
  hash_init (&frames, frame_hash, frame_less, NULL);
}

void*
frame_alloc (void *user_address, bool zeroed)
{
  ASSERT(pg_ofs (user_address) == 0);
  enum palloc_flags flags = PAL_USER | zeroed ? PAL_ZERO : 0;
  void *kaddr = palloc_get_page (flags);
  if (kaddr == NULL)
    {
      // TODO implement swapping
      return NULL;
    }
  else
    {
      struct frame *f = (struct frame*) malloc (sizeof(struct frame));
      ASSERT(f != NULL);
      f->kernel_address = kaddr;
      f->user_address = user_address;
      f->unaccessed_count = 0;
      f->t = thread_current ();
      lock_acquire (&frame_table_lock);
      ASSERT(hash_insert (&frames, &f->h_elem) == NULL);
      lock_release (&frame_table_lock);
      return kaddr;
    }
}

void
frame_free (void *kernel_address)
{
  struct frame f;
  struct hash_elem *e;
  f.kernel_address = kernel_address;
  lock_acquire (&frame_table_lock);
  e = hash_delete (&frames, &f.h_elem);
  lock_release (&frame_table_lock);
  if (e != NULL)
    {
      struct frame *g = hash_entry(e, struct frame, h_elem);
      palloc_free_page (g->kernel_address);
      free (g);
    }
}

