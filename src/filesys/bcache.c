#include "devices/block.h"
#include "filesys/bcache.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <stdio.h>
#include <string.h>

/* Define the buffer cache entry struct.
   Holds all the metadata for a cache block */
struct bcache_entry
  {
    struct list_elem  elem;
    struct block      *device;
    block_sector_t    sector;
    uint8_t*          *buffer;
    unsigned          dirty;
    unsigned          used;
  };

/* Declare buffer cache policy functions */
static struct bcache_entry *find_entry (struct block *, block_sector_t);
static struct bcache_entry *alloc_entry (void);
static void use_entry (struct bcache_entry *);

/* List holding all Buffer Cache Entries */
static struct list bcache_entry_list;

/* Lock used for the buffer cache. */
static struct lock bcache_lock;

/* Initializes buffer cache system */
void
bcache_init ()
{
  /* Initialize the Buffer Cache Entry list */
  list_init (&bcache_entry_list);

  /* Initialize the buffer cache lock */
  lock_init (&bcache_lock);
}

/* Uninitializes buffer cache system */
void
bcache_done ()
{
  /* Free all list bcache entries */
  while (!list_empty (&bcache_entry_list))
    {
      struct list_elem *e = list_front (&bcache_entry_list);
      struct bcache_entry *bce =  list_entry (e, struct bcache_entry, elem);

      list_remove (e);
      free (bce -> buffer);
      free (bce);
    }
}

/* Reads a block through the buffer cache system */
bool
bcache_read (struct block *device, block_sector_t sector,
  void *buffer, off_t size, off_t offset)
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
      bce -> device = device;
      bce -> sector = sector;
      bce -> dirty = 0;

      /* Read sector into bcache buffer. */
      block_read (device, sector, bce -> buffer);
    }
  else
    {
      cache_hit = true;

      /* Mark the entry as recently used. */
      use_entry (bce);
    }

  /* Partially copy into caller's buffer. */
  const void *src = bce -> buffer + offset;
  void *dst = buffer;
  printf ("read: copy %d bytes from %p to %p\n", size, src, dst);
  memcpy (dst, src, size);

  lock_release (&bcache_lock);
  return cache_hit;
}

/* Writes a block through the buffer cache system */
bool
bcache_write (struct block *device, block_sector_t sector,
  const void *buffer, off_t size, off_t offset)
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
      bce -> device = device;
      bce -> sector = sector;
      bce -> dirty = 1;

      /* Read sector into bcache buffer. */
      block_read (device, sector, bce -> buffer);
    }
  else
    {
      cache_hit = true;

      /* Mark the entry as dirty and recently used. */
      bce -> dirty = 1;
      use_entry (bce);
    }

  /* Partially copy from caller's buffer. */
  const void *src = buffer;
  void *dst = bce -> buffer + offset;
  printf ("write: copy %d bytes from %p to %p\n", size, src, dst);
  memcpy (dst, src, size);

  lock_release (&bcache_lock);
  lock_acquire (&bcache_lock);

  /* Write bcache buffer back to block sector. */
  block_write (device, sector, bce -> buffer);
  bce -> dirty = 0;

  lock_release (&bcache_lock);
  return cache_hit;
}


/* BUFFER CACHE POLICY  */

/* Define the CLOCK list entry pointer */
static struct list_elem *bcache_policy_ptr;

/* Iterates through the buffer cache entries and
   searches for a matching item. */
static struct bcache_entry *
find_entry (struct block *device, block_sector_t sector)
{
  struct list_elem *e, *start;

  /* Shortcut if the list is empty. */
  if (list_empty (&bcache_entry_list))
    goto miss;

  /* Iterate through the entry list and search for a matching entry */
  start = e = bcache_policy_ptr;
  do
    {
      struct bcache_entry *bce = list_entry (e, struct bcache_entry, elem);
      if (device == bce -> device && sector == bce -> sector)
        {
          printf ("Cache hit! [%p,%u] --> %p\n", device, sector, bce);
          return bce;
        }

      /* Advance the iterator by 1. */
      e = list_next_circular (&bcache_entry_list, e);
    }
  while (e != start);

miss:
  printf ("Cache miss! [%p,%u] --> NULL\n", device, sector);
  return NULL;
}

/* Allocates a new buffer cache entry by evicting
   a old entry if nessessary. */
static struct bcache_entry *
alloc_entry (void)
{
  const size_t ENTRY_LIMIT = 64;

  if (list_size (&bcache_entry_list) >= ENTRY_LIMIT)
    {
      /* Iterate through the entry list and search for the first
         element with used bit on */
      struct list_elem *start = bcache_policy_ptr;
      do
        {
          struct bcache_entry *bce = list_entry (
              bcache_policy_ptr, struct bcache_entry, elem);

          /* Advance the list pointer by 1 */
          bcache_policy_ptr = list_next_circular (
              &bcache_entry_list, bcache_policy_ptr);

          if (bce -> used == 0)
            return bce;
          else
            bce -> used = 0;
        }
      while (bcache_policy_ptr != start);

      /* all pages have recently been used. */
      return NULL; /* TODO: Do not return NULL */
    }
  else
    {
      /* Just allocate and initialize a new entry */
      struct bcache_entry *bce = malloc (sizeof *bce);
      bce -> buffer = malloc (BLOCK_SECTOR_SIZE);
      bce -> used = 0;

      /* Insert the entry into the global list of entries */
      if (!list_empty (&bcache_entry_list))
        list_insert (bcache_policy_ptr, &bce -> elem);
      else
        {
          /* Handle the case where the list is empty
             and `bcache_policy_ptr' is uninitialized. */
          list_push_back (&bcache_entry_list, &bce -> elem);
          bcache_policy_ptr = &bce -> elem;
        }

      return bce;
    }
}

/* Define USE for the CLOCK cache replacement policy */
static void
use_entry (struct bcache_entry *bce)
{
  /* Set the used bit of that entry. */
  bce -> used = 1;
}
