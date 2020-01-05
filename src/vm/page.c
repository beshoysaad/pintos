/*
 * page.c
 *
 *  Created on: Dec 29, 2019
 *      Author: Beshoy Saad
 */

#include "hash.h"
#include "debug.h"
#include "frame.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "page.h"

static void
page_deallocate (struct hash_elem *e, void *aux UNUSED)
{
  struct page *p = hash_entry(e, struct page, h_elem);
  ASSERT(p != NULL);
  frame_free (p->kernel_address);
  free (p);
}

static unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct page *p = hash_entry(p_, struct page, h_elem);
  uint32_t key = (uint32_t) (p->user_address);
  return hash_bytes (&key, sizeof key);
}

static bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
	   void *aux UNUSED)
{
  const struct page *a = hash_entry(a_, struct page, h_elem);
  const struct page *b = hash_entry(b_, struct page, h_elem);

  return a->user_address < b->user_address;
}

bool
page_table_init (struct hash **pages)
{
  *pages = (struct hash*) malloc (sizeof(struct hash));
  if (*pages == NULL)
    {
      return false;
    }
  return hash_init (*pages, page_hash, page_less, NULL);
}

void
page_table_destroy (void)
{
  struct process *proc = thread_current ()->p;
  lock_acquire (&proc->page_table_lock);
  hash_destroy (proc->pages, page_deallocate);
  lock_release (&proc->page_table_lock);
}

struct page*
page_alloc (void *upage, enum page_type type, bool writable)
{
  struct process *proc = thread_current ()->p;
  struct page *pg = (struct page*) malloc (sizeof(struct page));
  ASSERT(pg != NULL);
  struct frame *f = frame_alloc (upage, type == PAGE_TYPE_ZERO);
  ASSERT(f != NULL);
  ASSERT(f->kernel_address != NULL);
  pg->kernel_address = f->kernel_address;
  pg->user_address = upage;
  pg->type = type;
  pg->writable = writable;
  lock_acquire (&proc->page_table_lock);
  ASSERT(hash_insert(proc->pages, &pg->h_elem) == NULL);
  lock_release (&proc->page_table_lock);
  return pg;
}

void
page_free (void *upage)
{
  struct process *proc = thread_current ()->p;
  struct page p;
  struct hash_elem *e;
  p.user_address = upage;
  lock_acquire (&proc->page_table_lock);
  e = hash_delete (proc->pages, &p.h_elem);
  lock_release (&proc->page_table_lock);
  if (e != NULL)
    {
      struct page *g = hash_entry(e, struct page, h_elem);
      frame_free (g->kernel_address);
      free (g);
    }
}

struct page*
page_get (void *upage)
{
  struct process *proc = thread_current ()->p;
  struct page p;
  struct hash_elem *e;
  p.user_address = upage;
  e = hash_find (proc->pages, &p.h_elem);
  if (e != NULL)
    {
      return hash_entry(e, struct page, h_elem);
    }
  else
    {
      return NULL;
    }
}
