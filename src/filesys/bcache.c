#include "devices/block.h"
#include "filesys/bcache.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <stdio.h>

/* List holding all Buffer Cache Entries */
static struct list bcache_entry_list;

/* Forward declare cache policies */
static struct bcache_entry *bcache_policy_alloc_LRU (void);
static void bcache_policy_use_LRU (struct bcache_entry *e);

/* Select the cache policy to use */
#define bcache_policy_alloc  bcache_policy_alloc_LRU
#define bcache_policy_use    bcache_policy_use_LRU

/* Initialize buffer cache system */
void
bcache_init ()
{
  /* Initialize the Buffer Cache Entry lock */
  list_init (&bcache_entry_list);
}

/* Returns the buffer entry for the cached block */
struct bcache_entry *
bcache_get_entry (struct block *device, block_sector_t sector)
{
  struct list_elem *e;

  /* Iterate through the entry list and search for a matching entry */
  for (e = list_begin (&bcache_entry_list); e != list_end (&bcache_entry_list);
       e = list_next (e))
    {
      struct bcache_entry *bce = list_entry (e, struct bcache_entry, elem);
      if (device == bce -> device && sector == bce -> sector)
        {
          printf ("Cache hit! [%p,%u] --> %p\n", device, sector, bce);

          // mark the entry as used and return
          bcache_policy_use (bce);
          return bce;
        }
    }

  printf ("Cache miss! [%p,%u] --> NULL\n", device, sector);
  return NULL;
}

/* Allocates a new buffer cache entry by evicting
   a old entry if nessessary. */
struct bcache_entry *
bcache_alloc_entry (void)
{
  /* Forward to cache policy */
  return bcache_policy_alloc ();
}

/* Define ALLOC for the LRU cache replacement policy */
static struct bcache_entry *
bcache_policy_alloc_LRU (void)
{
  const size_t ENTRY_LIMIT = 8;

  if (list_size (&bcache_entry_list) >= ENTRY_LIMIT)
    {
      /* Move the last item to the front again. */
      struct list_elem *e = list_pop_back (&bcache_entry_list);
      list_push_front (&bcache_entry_list, e);

      return list_entry (e, struct bcache_entry, elem);
    }
  else
    {
      /* Just allocate and initialize a new entry */
      struct bcache_entry *bce = malloc (sizeof *bce);
      bce -> buffer = malloc (BLOCK_SECTOR_SIZE);

      /* Insert the entry into the global list of entries */
      list_push_front (&bcache_entry_list, &bce -> elem);

      return bce;
    }
}

/* Define USE for the LRU cache replacement policy */
static void
bcache_policy_use_LRU (struct bcache_entry *bce)
{
  /* Move the entry to the front again. */
  list_remove (&bce -> elem);
  list_push_front (&bcache_entry_list, &bce -> elem);
}