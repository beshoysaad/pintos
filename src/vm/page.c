/*
 * page.c
 *
 *  Created on: Dec 29, 2019
 *      Author: Beshoy Saad
 */

#include "debug.h"
#include "frame.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "string.h"
#include "threads/vaddr.h"
#include "swap.h"
#include "page.h"

extern struct lock lock_file_sys;

static void
page_deallocate (struct hash_elem *e, void *aux UNUSED)
{
  struct page *p = hash_entry(e, struct page, h_elem);
  ASSERT(p != NULL);
  sema_down (&p->page_sema);
  if (p->f != NULL)
    {
      frame_free (p->f->kernel_address, false);
    }
  else if (p->type == PAGE_TYPE_SWAP)
    {
      swap_free (p->ps.swap_sector);
    }
  sema_up (&p->page_sema);
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
  sema_down (&proc->page_table_sema);
  hash_destroy (proc->page_table, page_deallocate);
  sema_up (&proc->page_table_sema);
  free (proc->page_table);
}

struct page*
page_alloc_and_check_out (void *upage, uint32_t *pagedir, enum page_type type,
bool writable)
{
  if (upage == NULL)
    {
      return NULL;
    }
  ASSERT(is_user_vaddr (upage));
  struct process *proc = thread_current ()->p;
  struct page *pg = (struct page*) malloc (sizeof(struct page));
  ASSERT(pg != NULL);
  pg->user_address = upage;
  pg->type = type;
  pg->writable = writable;
  pg->f = NULL;
  pg->pagedir = pagedir;
  memset (&pg->ps, 0, sizeof(union page_storage));
  sema_init (&pg->page_sema, 1);
  sema_down (&proc->page_table_sema);
  if (hash_insert (proc->page_table, &pg->h_elem) != NULL) // Page already exists
    {
      sema_up (&proc->page_table_sema);
      free (pg);
      return NULL;
    }
  sema_down (&pg->page_sema);
  sema_up (&proc->page_table_sema);
  return pg;
}

void
page_free (void *upage)
{
  if (upage == NULL)
    {
      return;
    }
  ASSERT(is_user_vaddr (upage));
  struct process *proc = thread_current ()->p;
  struct page p;
  struct hash_elem *e;
  p.user_address = upage;
  sema_down (&proc->page_table_sema);
  e = hash_find (proc->page_table, &p.h_elem);
  if (e != NULL)
    {
      struct page *g = hash_entry(e, struct page, h_elem);
      sema_down (&g->page_sema);
      hash_delete (proc->page_table, &g->h_elem);
      if (g->f != NULL)
	{
	  frame_free (g->f->kernel_address, true);
	}
      else if (g->type == PAGE_TYPE_SWAP)
	{
	  swap_free (g->ps.swap_sector);
	}
      sema_up (&g->page_sema);
      free (g);
    }
  sema_up (&proc->page_table_sema);
}

struct page*
page_check_out (void *upage)
{
  if (upage == NULL)
    {
      return NULL;
    }
  ASSERT(is_user_vaddr (upage));
  struct process *proc = thread_current ()->p;
  struct page p;
  struct hash_elem *e;
  p.user_address = upage;
  sema_down (&proc->page_table_sema);
  e = hash_find (proc->page_table, &p.h_elem);
  if (e != NULL)
    {
      struct page *pg = hash_entry(e, struct page, h_elem);
      sema_down (&pg->page_sema);
      sema_up (&proc->page_table_sema);
      return pg;
    }
  else
    {
      sema_up (&proc->page_table_sema);
      return NULL;
    }
}

void
page_check_in (void *upage)
{
  if (upage == NULL)
    {
      return;
    }
  ASSERT(is_user_vaddr (upage));
  struct process *proc = thread_current ()->p;
  struct page p;
  struct hash_elem *e;
  p.user_address = upage;
  sema_down (&proc->page_table_sema);
  e = hash_find (proc->page_table, &p.h_elem);
  if (e != NULL)
    {
      struct page *pg = hash_entry(e, struct page, h_elem);
      sema_up (&pg->page_sema);
    }
  sema_up (&proc->page_table_sema);
}

bool
page_evict (void *uaddr)
{
  if (uaddr == NULL)
    {
      return false;
    }
  ASSERT(is_user_vaddr (uaddr));
  struct page *pg = page_check_out (uaddr);
  if (pg == NULL)
    {
      return false;
    }
  if (pg->f == NULL)
    {
      page_check_in (uaddr);
      return false;
    }
  uint32_t *user_pd = pg->pagedir;
  pagedir_clear_page (user_pd, uaddr);
  if (pg->type == PAGE_TYPE_SWAP)
    {
      pg->ps.swap_sector = swap_write (pg->f->kernel_address);
    }
  else
    {
      if (pagedir_is_dirty (user_pd, uaddr))
	{
	  switch (pg->type)
	    {
	    case PAGE_TYPE_FILE:
	      {
		if (pg->ps.fs.read_only)
		  {
		    pg->type = PAGE_TYPE_SWAP;
		    pg->ps.swap_sector = swap_write (pg->f->kernel_address);
		  }
		else
		  {
		    lock_acquire (&lock_file_sys);
		    ASSERT(
			file_write_at (pg->ps.fs.f, pg->f->kernel_address,
				       pg->ps.fs.size, pg->ps.fs.offset)
			    == pg->ps.fs.size);
		    lock_release (&lock_file_sys);
		  }
		break;
	      }
	    case PAGE_TYPE_ZERO:
	      pg->type = PAGE_TYPE_SWAP;
	      pg->ps.swap_sector = swap_write (pg->f->kernel_address);
	      break;
	    default:
	      ASSERT(false)
	      ;
	      break;
	    }
	  pagedir_set_dirty (user_pd, uaddr, false);
	}
    }
  pg->f = NULL;
  page_check_in (pg->user_address);
  return true;
}

bool
page_is_writable (void *upage)
{
  ASSERT(upage != NULL);
  ASSERT(is_user_vaddr (upage));
  struct process *proc = thread_current ()->p;
  struct page p;
  struct hash_elem *e;
  p.user_address = upage;
  sema_down (&proc->page_table_sema);
  e = hash_find (proc->page_table, &p.h_elem);
  ASSERT(e != NULL);
  struct page *pg = hash_entry(e, struct page, h_elem);
  bool result = pg->writable;
  sema_up (&proc->page_table_sema);
  return result;
}
