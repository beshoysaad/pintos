#include "devices/block.h"
#include "devices/timer.h"
#include "filesys/bcache.h"
#include "threads/malloc.h"
#include "threads/synch.h"
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
  struct lock lock;
  struct lock cntlock;
};

/* Data struct that holds the device and sector to read. */
struct bcache_data
{
  struct block_device  *device;
  block_sector_t       sector;
};

/* Declare buffer cache policy functions */
static struct bcache_entry*
find_entry (struct block_device*, block_sector_t);
static struct bcache_entry*
alloc_entry (void);
static void
thread_read_ahead (void*);
static void
thread_write_behind (void*);

/* Declare common synchronization parts */
static void enter_usage_flow (void);
static void exit_usage_flow (void);
static void enter_modification_flow (void);
static void exit_modification_flow (void);

/* List holding all Buffer Cache Entries */
static struct list bcache_entry_list;

/* Synchronization constructs for the buffer cache. */
static struct lock usage_flow_lock;
static struct condition usage_flow_condition;
static unsigned usage_flow_count;
static struct lock modification_flow_lock;
static struct condition modification_flow_condition;
static unsigned modification_flow_count;

/* Indicator if the Write-Behind thread should stop. */
static bool bcache_stop_write_behind;

/* Initializes buffer cache system */
void
bcache_init (void)
{
  /* Initialize the Buffer Cache Entry list */
  list_init (&bcache_entry_list);

  /* Initialize the buffer cache synchonization. */
  lock_init(&modification_flow_lock);
  cond_init(&modification_flow_condition);
  modification_flow_count = 0;
  lock_init(&usage_flow_lock);
  cond_init(&usage_flow_condition);
  usage_flow_count = 0;

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

  /* Enter modification flow. */
  enter_modification_flow ();

  /* Free all list bcache entries */
  while (!list_empty (&bcache_entry_list))
    {
      struct list_elem *e = list_front (&bcache_entry_list);
      struct bcache_entry *bce = list_entry(e, struct bcache_entry, elem);
      lock_acquire (&bce->lock);
      lock_acquire (&bce->cntlock);

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

  /* Exit modification flow. */
  exit_modification_flow ();
}

/* Reads a block through the buffer cache system */
bool
bcache_read (struct block_device *device, block_sector_t sector, void *buffer,
	     off_t size, off_t offset)
{
  bool cache_hit;

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
      lock_acquire (&bce->cntlock);
      lock_release (&bce->lock);
      block_read (device, sector, bce->buffer);
    }
  else
    {
      cache_hit = true;

      /* Mark the entry as recently used. */
      bce->used = true;

      lock_acquire (&bce->cntlock);
      lock_release (&bce->lock);
    }

  /* Partially copy into caller's buffer. */
  if (buffer != NULL)
    memcpy (buffer, bce->buffer + offset, size);

  lock_release (&bce->cntlock);
  return cache_hit;
}

/* Writes a block through the buffer cache system */
bool
bcache_write (struct block_device *device, block_sector_t sector, const void *buffer,
	      off_t size, off_t offset)
{
  bool cache_hit;

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
      lock_acquire (&bce->cntlock);
      lock_release (&bce->lock);
      block_read (device, sector, bce->buffer);
    }
  else
    {
      cache_hit = true;

      /* Mark the entry as dirty and recently used. */
      bce->dirty = true;
      bce->used = true;

      lock_acquire (&bce->cntlock);
      lock_release (&bce->lock);
    }

  /* Partially copy from caller's buffer. */
  if (buffer != NULL)
    memcpy (bce->buffer + offset, buffer, size);

  lock_release (&bce->cntlock);
  return cache_hit;
}

/* Read-Ahead caller function to read a specfic block
   into the block cache. */
void
bcache_read_ahead (struct block_device *device, block_sector_t sector)
{
  /* Allocate the AUX argument structure. */
  struct bcache_data *data = malloc (sizeof *data);

  /* Initialize the data packet. */
  data -> device = device;
  data -> sector = sector;

  /* Start the read-ahead thread. */
  thread_create ("bcache-read-ahead", PRI_MIN, thread_read_ahead, data);
}

/* Thread function that read a single block into
   the block cache. */
static void
thread_read_ahead (void *aux)
{
  struct bcache_data *data = aux;
  
  /* Read the block into the buffer cache. */
  bcache_read (data -> device, data -> sector, NULL, 0, 0);
  free (aux);
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

      /* Enter the usage flow. */
      enter_usage_flow ();

      for (e = list_begin (&bcache_entry_list);
          e != list_end (&bcache_entry_list); e = list_next (e))
        {
          struct bcache_entry *bce = list_entry(e, struct bcache_entry, elem);
          lock_acquire (&bce->lock);

          /* Write behind if dirty. */
          if (bce->dirty)
            {
              lock_acquire (&bce->cntlock);
              lock_release (&bce->lock);
              block_write (bce->device, bce->sector, bce->buffer);
              lock_release (&bce->cntlock);
            }
          else
            {
              lock_release (&bce->lock);
            }
        }

      /* Exit the usage flow. */
      exit_usage_flow ();
    }
  while (!bcache_stop_write_behind);
}

/* BUFFER CACHE POLICY  */

/* Define the CLOCK list entry pointer */
static struct list_elem *bcache_policy_ptr;

/* Iterates through the buffer cache entries and
 searches for a matching item. If a matching item
 is found, its lock is automatically aquired. */
static struct bcache_entry*
find_entry (struct block_device *device, block_sector_t sector)
{
  struct list_elem *e, *start;
  struct bcache_entry *bce = NULL;

  /* Enter the usage flow. */
  enter_usage_flow ();

  /* Shortcut if the list is empty. */
  if (list_empty (&bcache_entry_list))
    goto exit;

  /* Iterate through the entry list and search for a matching entry */
  start = e = bcache_policy_ptr;
  do
    {
      bce = list_entry(e, struct bcache_entry, elem);
      lock_acquire (&bce->lock);
      if (device == bce->device && sector == bce->sector)
        goto exit;

      lock_release (&bce->lock);

      /* Advance the iterator by 1. We search in reverse list
         order to reward more recent cache entries. */
      e = list_prev_circular (&bcache_entry_list, e);
    }
  while (e != start);

  /* Reset the return value. */
  bce = NULL;

exit:
  /* Exit the usage flow. */
  exit_usage_flow ();

  return bce;
}

/* Allocates a new buffer cache entry by evicting
 a old entry if necessary. The lock of the entry
 is automatically aquired. */
static struct bcache_entry*
alloc_entry (void)
{
  if (list_size (&bcache_entry_list) >= ENTRY_LIMIT)
    {
      /* Enter the usage flow. */
      enter_usage_flow ();

      /* Iterate through the entry list and search for the first
       element with used bit == false */
      do
        {
          struct bcache_entry *bce = list_entry(bcache_policy_ptr,
                  struct bcache_entry, elem);

          /* Advance the list pointer by 1 */
          bcache_policy_ptr = list_next_circular (&bcache_entry_list,
                    bcache_policy_ptr);

          lock_acquire (&bce->lock);
          if (bce->used == true)
            bce->used = false;
          else
            {
              /* Write behind if dirty. */
              if (bce->dirty)
                {
                  lock_acquire (&bce->cntlock);
                  block_write (bce->device, bce->sector, bce->buffer);
                  lock_release (&bce->cntlock);
                }

              /* Exit the usage flow. */
              exit_usage_flow ();

              return bce;
            }
          lock_release (&bce->lock);
        }
      while (true);
    }
  else
    {
      /* Just allocate and initialize a new entry */
      struct bcache_entry *bce = malloc (sizeof *bce);
      bce->buffer = malloc (BLOCK_SECTOR_SIZE);
      bce->used = false;
      lock_init (&bce->lock);
      lock_init (&bce->cntlock);
      lock_acquire (&bce->lock);

      /* Enter modification flow. */
      enter_modification_flow ();

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

      /* Exit modification flow. */
      exit_modification_flow ();

      return bce;
    }
}

/* BUFFER CACHE SYNCHRONISATION */

/* Enters a usage flow. This should be called before
   iterating over the bcache entry list. */
static void
enter_usage_flow (void)
{
  lock_acquire(&modification_flow_lock);
  while (modification_flow_count > 0)
    cond_wait(&modification_flow_condition, &modification_flow_lock);
  lock_release(&modification_flow_lock);
  lock_acquire(&usage_flow_lock);
  ++usage_flow_count;
  lock_release(&usage_flow_lock);
}

/* Exits a usage flow. This should be called after
   iterating over the bcache entry list. */
static void
exit_usage_flow (void)
{
  lock_acquire(&usage_flow_lock);
  --usage_flow_count;
  cond_broadcast(&usage_flow_condition, &usage_flow_lock);
  lock_release(&usage_flow_lock);
}

/* Enters a modification flow. This should be called
   before modifying the bcache entry list (i.e. adding
   or removing a item). */
static void
enter_modification_flow (void)
{
  lock_acquire(&modification_flow_lock);
  ++modification_flow_count;
  lock_acquire(&usage_flow_lock);
  while (usage_flow_count > 0)
    cond_wait(&usage_flow_condition, &usage_flow_lock);
  lock_release(&usage_flow_lock);
}

/* Exits a modification flow. This should be called
   after modifying the bcache entry list (i.e. adding
   or removing a item). */
static void
exit_modification_flow (void)
{
  --modification_flow_count;
  cond_broadcast(&modification_flow_condition, &modification_flow_lock);
  lock_release(&modification_flow_lock);
}
