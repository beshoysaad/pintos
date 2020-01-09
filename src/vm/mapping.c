/*
 * mapping.c
 *
 *  Created on: Jan 9, 2020
 *      Author: Beshoy Saad
 */

#include "mapping.h"
#include "threads/vaddr.h"
#include "page.h"
#include "frame.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/process.h"

static void
mapping_deallocate (struct hash_elem *e, void *aux UNUSED)
{
  struct mapping *m = hash_entry(e, struct mapping, h_elem);
  ASSERT(m != NULL);
  for (int i = 0; i < m->num_pages; i++)
    {
      struct page *p = page_get (m->upage + (i * PGSIZE));
      ASSERT(p != NULL);
      if (p->f != NULL)
	{
	  ASSERT(frame_evict (p->f) == true);
	}
      page_free (p->user_address);
    }

  free (m);
}

static unsigned
mapping_hash (const struct hash_elem *m_, void *aux UNUSED)
{
  const struct mapping *m = hash_entry(m_, struct mapping, h_elem);
  uint32_t key = (uint32_t) (m->map_id);
  return hash_bytes (&key, sizeof key);
}

static bool
mapping_less (const struct hash_elem *a_, const struct hash_elem *b_,
	      void *aux UNUSED)
{
  const struct mapping *a = hash_entry(a_, struct mapping, h_elem);
  const struct mapping *b = hash_entry(b_, struct mapping, h_elem);
  return a->map_id < b->map_id;
}

bool
mapping_table_init (struct hash **mapping_table)
{
  ASSERT(mapping_table != NULL);
  *mapping_table = (struct hash*) malloc (sizeof(struct hash));
  if (*mapping_table == NULL)
    {
      return false;
    }
  return hash_init (*mapping_table, mapping_hash, mapping_less, NULL);
}

void
mapping_table_destroy (void)
{
  struct process *proc = thread_current ()->p;
  lock_acquire (&proc->mapping_table_lock);
  hash_destroy (proc->mapping_table, mapping_deallocate);
  lock_release (&proc->mapping_table_lock);
  free (proc->mapping_table);
}

struct mapping*
mapping_alloc (void *upage, struct file *f)
{
  ASSERT(f != NULL);
  struct process *proc = thread_current ()->p;
  struct mapping *mp = (struct mapping*) malloc (sizeof(struct mapping));
  ASSERT(mp != NULL);
  off_t file_size = file_length (f);
  if (file_size == 0)
    {
      free (mp);
      return NULL;
    }
  if (!load_segment (f, 0, upage, file_size, PGSIZE - file_size % PGSIZE, true))
    {
      free (mp);
      return NULL;
    }
  mp->map_id = proc->mapping_counter++;
  mp->upage = upage;
  mp->num_pages = file_size / PGSIZE + ((file_size % PGSIZE == 0) ? 0 : 1);
  lock_acquire (&proc->mapping_table_lock);
  ASSERT(hash_insert(proc->mapping_table, &mp->h_elem) == NULL);
  lock_release (&proc->mapping_table_lock);
  return mp;
}

void
mapping_free (mapid_t map_id)
{
  struct process *proc = thread_current ()->p;
  struct mapping m;
  struct hash_elem *e;
  m.map_id = map_id;
  lock_acquire (&proc->mapping_table_lock);
  e = hash_delete (proc->mapping_table, &m.h_elem);
  lock_release (&proc->mapping_table_lock);
  if (e != NULL)
    {
      struct mapping *mp = hash_entry(e, struct mapping, h_elem);
      for (int i = 0; i < mp->num_pages; i++)
	{
	  struct page *p = page_get (mp->upage + (i * PGSIZE));
	  ASSERT(p != NULL);
	  if (p->f != NULL)
	    {
	      ASSERT(frame_evict (p->f) == true);
	    }
	  page_free (p->user_address);
	}

      free (mp);
    }
}
