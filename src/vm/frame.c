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
#include "userprog/pagedir.h"
#include "swap.h"
#include "frame.h"
#include "stdio.h"

static struct hash frame_table;
static struct lock frame_table_lock;
struct hash_iterator i;
extern struct lock lock_file_sys;

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
      free (f);
      lock_acquire (&frame_table_lock);
      if (i.elem == NULL)
	{
	  hash_first (&i, &frame_table);
	}
      while (true)
	{
	  if (hash_next (&i) == NULL)
	    {
	      hash_first (&i, &frame_table);
	      hash_next (&i);
	    }
	  struct frame *fr = hash_entry(hash_cur (&i), struct frame, h_elem);
	  if (fr->user_page == NULL)
	    {
	      continue;
	    }
	  void *uaddr = fr->user_page->user_address;
	  uint32_t *user_pd = fr->user_page->pagedir;
	  if (pagedir_is_accessed (user_pd, uaddr))
	    {
	      pagedir_set_accessed (user_pd, uaddr, false);
	    }
	  else
	    {
	      if (frame_evict (fr))
		{
		  lock_release (&frame_table_lock);
		  return fr;
		}
	    }
	}
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

bool
frame_evict (struct frame *fr)
{
  ASSERT(fr->user_page != NULL);
  void *uaddr = fr->user_page->user_address;
  uint32_t *user_pd = fr->user_page->pagedir;
  if (fr->user_page->type == PAGE_TYPE_SWAP)
    {
      fr->user_page->ps.swap_sector = swap_write (fr->kernel_address);
    }
  else
    {
      if (pagedir_is_dirty (user_pd, uaddr))
	{
	  switch (fr->user_page->type)
	    {
	    case PAGE_TYPE_FILE:
	      {
		lock_acquire (&lock_file_sys);
		off_t written_size = file_write_at (
		    fr->user_page->ps.fs.f, fr->kernel_address,
		    PGSIZE,
		    fr->user_page->ps.fs.offset);
		lock_release (&lock_file_sys);
		if (written_size == 0) // Write denied. Need to find a different page to evict.
		  {
		    return false;
		  }
		break;
	      }
	    case PAGE_TYPE_ZERO:
	      fr->user_page->type = PAGE_TYPE_SWAP;
	      fr->user_page->ps.swap_sector = swap_write (fr->kernel_address);
	      break;
	    default:
	      ASSERT(false)
	      ;
	      break;
	    }
	  pagedir_set_dirty (user_pd, uaddr, false);
	}
    }
  pagedir_clear_page (user_pd, uaddr);
  fr->user_page->f = NULL;
  fr->user_page = NULL;
  return true;
}

