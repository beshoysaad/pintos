#ifndef FILESYS_BCACHE_H
#define FILESYS_BCACHE_H

#include "devices/block.h"
#include <list.h>

/* Declare a buffer cache entry.
   Holds all the metadata for a cache block */
struct bcache_entry
  {
    struct list_elem  elem;
    struct block      *device;
    block_sector_t    sector;
    void              *buffer;
    long long         aux;
  };

/* Initialize buffer cache system */
void bcache_init (void);

/* Returns the buffer address for the cached block */
struct bcache_entry *bcache_get_entry (struct block *block, block_sector_t sector);

/* Allocates a new buffer cache entry by evicting
   a old entry if nessessary. */
struct bcache_entry *bcache_alloc_entry (void);

#endif /* filesys/bcache.h */