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
static struct semaphore frame_table_sema;
struct hash_iterator i;

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
  sema_init (&frame_table_sema, 1);
  hash_init (&frame_table, frame_hash, frame_less, NULL);
}

struct frame*
frame_alloc_and_check_out (bool zeroed)
{
  enum palloc_flags flags = PAL_USER | (zeroed ? PAL_ZERO : 0);
  struct frame *f = (struct frame*) malloc (sizeof(struct frame));
  ASSERT(f != NULL);
  sema_init (&f->frame_sema, 1);
  f->user_page = NULL;
  f->kernel_address = palloc_get_page (flags);
  if (f->kernel_address == NULL)
    {
      free (f);
      sema_down (&frame_table_sema);
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
	  sema_down (&fr->frame_sema);
	  ASSERT(fr->user_page != NULL);
	  void *uaddr = fr->user_page->user_address;
	  uint32_t *user_pd = fr->user_page->pagedir;
	  if (pagedir_is_accessed (user_pd, uaddr))
	    {
	      pagedir_set_accessed (user_pd, uaddr, false);
	      sema_up (&fr->frame_sema);
	    }
	  else
	    {
	      if (page_evict (uaddr))
		{
		  sema_up (&frame_table_sema);
		  fr->user_page = NULL;
		  return fr;
		}
	      else
		{
		  sema_up (&fr->frame_sema);
		}
	    }
	}
    }
  else
    {
      sema_down (&frame_table_sema);
      ASSERT(hash_insert (&frame_table, &f->h_elem) == NULL);
      sema_down (&f->frame_sema);
      sema_up (&frame_table_sema);
      return f;
    }
}

struct frame*
frame_check_out (void *kaddr)
{
  ASSERT(is_kernel_vaddr (kaddr));
  struct frame f;
  f.kernel_address = kaddr;
  struct hash_elem *e;
  sema_down (&frame_table_sema);
  e = hash_find (&frame_table, &f.h_elem);
  if (e != NULL)
    {
      struct frame *fr = hash_entry(e, struct frame, h_elem);
      sema_down (&fr->frame_sema);
      sema_up (&frame_table_sema);
      return fr;
    }
  sema_up (&frame_table_sema);
  return NULL;
}

void
frame_check_in (void *kaddr)
{
  ASSERT(is_kernel_vaddr (kaddr));
  struct frame f;
  f.kernel_address = kaddr;
  struct hash_elem *e;
  sema_down (&frame_table_sema);
  e = hash_find (&frame_table, &f.h_elem);
  if (e != NULL)
    {
      struct frame *fr = hash_entry(e, struct frame, h_elem);
      sema_up (&fr->frame_sema);
    }
  sema_up (&frame_table_sema);
}

void
frame_free (void *kaddr, bool free_page)
{
  ASSERT(is_kernel_vaddr (kaddr));
  struct frame f;
  f.kernel_address = kaddr;
  struct hash_elem *e;
  sema_down (&frame_table_sema);
  e = hash_find (&frame_table, &f.h_elem);
  if (e != NULL)
    {
      struct frame *fr = hash_entry(e, struct frame, h_elem);
      sema_down (&fr->frame_sema);
      hash_delete (&frame_table, &fr->h_elem);
      if (free_page)
	{
	  palloc_free_page (fr->kernel_address);
	}
      sema_up (&fr->frame_sema);
      free (fr);
    }
  sema_up (&frame_table_sema);
}
