#include "devices/block.h"
#include "devices/timer.h"
#include "filesys/bcache.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <stdio.h>
#include <string.h>
#include "filesys.h"

#define ENTRY_LIMIT 64

/* Define the buffer cache entry struct.
 Holds all the metadata for a cache block */
struct bcache_entry
{
  struct list_elem elem;
  struct block_device *device;
  block_sector_t sector;
  uint8_t *buffer;
  bool dirty;
  bool used;
};

/* Declare buffer cache policy functions */
static struct bcache_entry*
find_entry (struct block_device*, block_sector_t);
static struct bcache_entry*
alloc_entry (void);
static void
use_entry (struct bcache_entry*);
static void
thread_write_behind (void*);

/* List holding all Buffer Cache Entries */
static struct list bcache_entry_list;

/* Lock used for the buffer cache. */
static struct lock bcache_lock;

/* Indicator if the Write-Behind thread should stop. */
static bool bcache_stop_write_behind;

/* Initializes buffer cache system */
void
bcache_init (void)
{
  /* Initialize the Buffer Cache Entry list */
  list_init (&bcache_entry_list);

  /* Initialize the buffer cache lock */
  lock_init (&bcache_lock);

  /* Start the Write-Behind thread. */
  bcache_stop_write_behind = false;
  thread_create ("bcache-write-behind", PRI_MIN, thread_write_behind, NULL);
}

/* Uninitializes buffer cache system */
void
bcache_done (void)
{
  /* Stop the Write-Behind thread */
  bcache_stop_write_behind = true;

  /* Free all list bcache entries */
  lock_acquire (&bcache_lock);
  while (!list_empty (&bcache_entry_list))
    {
      struct list_elem *e = list_front (&bcache_entry_list);
      struct bcache_entry *bce = list_entry(e, struct bcache_entry, elem);

      /* Make sure the content is written to
       disk if it is dirty. */
      if (bce->dirty)
	{
	  block_write (bce->device, bce->sector, bce->buffer);
	}

      list_remove (e);
      free (bce->buffer);
      free (bce);
    }
  lock_release (&bcache_lock);
}

/* Reads a block through the buffer cache system */
bool
bcache_read (struct block_device *device, block_sector_t sector, void *buffer,
	     off_t size, off_t offset)
{
  bool cache_hit;
  lock_acquire (&bcache_lock);

  /* Find the corresponding buffer cache entry, if
   it exists. */
  struct bcache_entry *bce = find_entry (device, sector);
  if (bce == NULL)
    {
      cache_hit = false;

      /* Allocate a new entry. */
      bce = alloc_entry ();
      bce->device = device;
      bce->sector = sector;
      bce->dirty = false;

      /* Read sector into bcache buffer. */
      block_read (device, sector, bce->buffer);
    }
  else
    {
      cache_hit = true;

      /* Mark the entry as recently used. */
      use_entry (bce);
    }

  /* Partially copy into caller's buffer. */
  memcpy (buffer, bce->buffer + offset, size);

  lock_release (&bcache_lock);
  return cache_hit;
}

/* Writes a block through the buffer cache system */
bool
bcache_write (struct block_device *device, block_sector_t sector, const void *buffer,
	      off_t size, off_t offset)
{
  bool cache_hit;
  lock_acquire (&bcache_lock);

  /* Find the corresponding buffer cache entry, if
   it exists. */
  struct bcache_entry *bce = find_entry (device, sector);
  if (bce == NULL)
    {
      cache_hit = false;

      /* Allocate a new entry. */
      bce = alloc_entry ();
      bce->device = device;
      bce->sector = sector;
      bce->dirty = true;

      /* Read sector into bcache buffer. */
      block_read (device, sector, bce->buffer);
    }
  else
    {
      cache_hit = true;

      /* Mark the entry as dirty and recently used. */
      bce->dirty = true;
      use_entry (bce);
    }

  /* Partially copy from caller's buffer. */
  memcpy (bce->buffer + offset, buffer, size);

  lock_release (&bcache_lock);
  return cache_hit;
}

/* Thread function that periodically loops through
 the buffer cache entries and write dirty ones
 back to disk. */
static void
thread_write_behind (void *aux UNUSED)
{
  struct list_elem *e;

  do
    {
      timer_sleep (1000); /* 1000 ticks. */

      lock_acquire (&bcache_lock);
      for (e = list_begin (&bcache_entry_list);
	  e != list_end (&bcache_entry_list); e = list_next (e))
	{
	  struct bcache_entry *bce = list_entry(e, struct bcache_entry, elem);

	  /* Write behind if dirty. */
	  if (bce->dirty)
	    {
	      block_write (bce->device, bce->sector, bce->buffer);
	    }
	}
      lock_release (&bcache_lock);
    }
  while (!bcache_stop_write_behind);
}

/* BUFFER CACHE POLICY  */

/* Define the CLOCK list entry pointer */
static struct list_elem *bcache_policy_ptr;

/* Iterates through the buffer cache entries and
 searches for a matching item. */
static struct bcache_entry*
find_entry (struct block_device *device, block_sector_t sector)
{
  struct list_elem *e, *start;

  /* Shortcut if the list is empty. */
  if (list_empty (&bcache_entry_list))
    goto miss;

  /* Iterate through the entry list and search for a matching entry */
  start = e = bcache_policy_ptr;
  do
    {
      struct bcache_entry *bce = list_entry(e, struct bcache_entry, elem);
      if (device == bce->device && sector == bce->sector)
	{
	  return bce;
	}

      /* Advance the iterator by 1. */
      e = list_next_circular (&bcache_entry_list, e);
    }
  while (e != start);

miss: return NULL;
}

/* Allocates a new buffer cache entry by evicting
 a old entry if necessary. */
static struct bcache_entry*
alloc_entry (void)
{
  if (list_size (&bcache_entry_list) >= ENTRY_LIMIT)
    {
      /* Iterate through the entry list and search for the first
       element with used bit == false */
      do
	{
	  struct bcache_entry *bce = list_entry(bcache_policy_ptr,
						struct bcache_entry, elem);

	  /* Advance the list pointer by 1 */
	  bcache_policy_ptr = list_next_circular (&bcache_entry_list,
						  bcache_policy_ptr);

	  if (bce->used == false)
	    {
	      /* Write behind if dirty. */
	      if (bce->dirty)
		{
		  block_write (bce->device, bce->sector, bce->buffer);
		}

	      return bce;
	    }
	  else
	    {
	      bce->used = false;
	    }
	}
      while (true);
    }
  else
    {
      /* Just allocate and initialize a new entry */
      struct bcache_entry *bce = malloc (sizeof *bce);
      bce->buffer = malloc (BLOCK_SECTOR_SIZE);
      bce->used = false;

      /* Insert the entry into the global list of entries */
      if (!list_empty (&bcache_entry_list))
	{
	  list_insert (bcache_policy_ptr, &bce->elem);
	}
      else
	{
	  /* Handle the case where the list is empty
	   and `bcache_policy_ptr' is uninitialized. */
	  list_push_back (&bcache_entry_list, &bce->elem);
	  bcache_policy_ptr = &bce->elem;
	}

      return bce;
    }
}

/* Define USE for the CLOCK cache replacement policy */
static void
use_entry (struct bcache_entry *bce)
{
  /* Set the used bit of that entry. */
  bce->used = true;
}
