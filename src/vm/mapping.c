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

extern struct lock lock_file_sys;

static void
mapping_deallocate (struct hash_elem *e, void *aux UNUSED)
{
  struct mapping *m = hash_entry(e, struct mapping, h_elem);
  ASSERT(m != NULL);
  for (int i = 0; i < m->num_pages; i++)
    {
      page_evict (m->proc, m->upage + (i * PGSIZE));
      page_free (m->proc, m->upage + (i * PGSIZE));
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
mapping_table_destroy (struct process *proc)
{
  lock_acquire (&proc->mapping_table_lock);
  hash_destroy (proc->mapping_table, mapping_deallocate);
  free (proc->mapping_table);
}

struct mapping*
mapping_alloc (struct process *proc, void *upage, struct file *f)
{
  if (f == NULL)
    {
      return NULL;
    }
  ASSERT(upage != NULL);
  ASSERT(proc != NULL);
  ASSERT(is_user_vaddr (upage));
  struct mapping *mp = (struct mapping*) malloc (sizeof(struct mapping));
  ASSERT(mp != NULL);
  lock_acquire (&lock_file_sys);
  off_t file_size = file_length (f);
  lock_release (&lock_file_sys);
  if (file_size == 0)
    {
      free (mp);
      return NULL;
    }
  if (!load_segment (f, 0, upage, file_size, PGSIZE - (file_size % PGSIZE),
		     true, false))
    {
      free (mp);
      return NULL;
    }
  mp->map_id = proc->mapping_counter++;
  mp->upage = upage;
  mp->num_pages = file_size / PGSIZE + ((file_size % PGSIZE == 0) ? 0 : 1);
  mp->proc = proc;
  lock_acquire (&proc->mapping_table_lock);
  ASSERT(hash_insert(proc->mapping_table, &mp->h_elem) == NULL);
  lock_release (&proc->mapping_table_lock);
  return mp;
}

void
mapping_free (struct process *proc, mapid_t map_id)
{
  struct mapping m;
  struct hash_elem *e;
  m.map_id = map_id;
  lock_acquire (&proc->mapping_table_lock);
  e = hash_delete (proc->mapping_table, &m.h_elem);
  if (e != NULL)
    {
      struct mapping *mp = hash_entry(e, struct mapping, h_elem);
      for (int i = 0; i < mp->num_pages; i++)
	{
	  page_evict (mp->proc, mp->upage + (i * PGSIZE));
	  page_free (mp->proc, mp->upage + (i * PGSIZE));
	}
      free (mp);
    }
  lock_release (&proc->mapping_table_lock);
}
