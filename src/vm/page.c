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
#include "string.h"

static void
page_deallocate (struct hash_elem *e, void *aux UNUSED)
{
  struct page *p = hash_entry(e, struct page, h_elem);
  ASSERT(p != NULL);
  if (p->f != NULL)
    {
      frame_free (p->f->kernel_address);
    }
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
page_table_init (struct hash **page_table)
{
  ASSERT(page_table != NULL);
  *page_table = (struct hash*) malloc (sizeof(struct hash));
  if (*page_table == NULL)
    {
      return false;
    }
  return hash_init (*page_table, page_hash, page_less, NULL);
}

void
page_table_destroy (void)
{
  struct process *proc = thread_current ()->p;
  lock_acquire (&proc->page_table_lock);
  hash_destroy (proc->page_table, page_deallocate);
  lock_release (&proc->page_table_lock);
  free (proc->page_table);
}

struct page*
page_alloc (void *upage, enum page_type type, bool writable)
{
  ASSERT(upage != NULL);
  struct process *proc = thread_current ()->p;
  struct page *pg = (struct page*) malloc (sizeof(struct page));
  ASSERT(pg != NULL);
  pg->user_address = upage;
  pg->type = type;
  pg->writable = writable;
  pg->f = NULL;
  memset (&pg->ps, 0, sizeof(union page_storage));
  lock_acquire (&proc->page_table_lock);
  ASSERT(hash_insert(proc->page_table, &pg->h_elem) == NULL);
  lock_release (&proc->page_table_lock);
  return pg;
}

void
page_free (void *upage)
{
  if (upage == NULL)
    {
      return;
    }
  struct process *proc = thread_current ()->p;
  struct page p;
  struct hash_elem *e;
  p.user_address = upage;
  lock_acquire (&proc->page_table_lock);
  e = hash_delete (proc->page_table, &p.h_elem);
  lock_release (&proc->page_table_lock);
  if (e != NULL)
    {
      struct page *g = hash_entry(e, struct page, h_elem);
      if (g->f != NULL)
	{
	  frame_free (g->f->kernel_address);
	}
      free (g);
    }
}

struct page*
page_get (void *upage)
{
  if (upage == NULL)
    {
      return NULL;
    }
  struct process *proc = thread_current ()->p;
  struct page p;
  struct hash_elem *e;
  p.user_address = upage;
  lock_acquire(&proc->page_table_lock);
  e = hash_find (proc->page_table, &p.h_elem);
  lock_release(&proc->page_table_lock);
  if (e != NULL)
    {
      return hash_entry(e, struct page, h_elem);
    }
  else
    {
      return NULL;
    }
}
