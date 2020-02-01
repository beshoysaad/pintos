#ifndef FILESYS_BCACHE_H
#define FILESYS_BCACHE_H

#include "devices/block.h"
#include "filesys/off_t.h"
#include <stdbool.h>

/* Initialize buffer cache system */
void
bcache_init (void);

/* Uninitialize buffer cache system */
void
bcache_done (void);

/* Read a block through the buffer cache system */
bool
bcache_read (struct block_device*, block_sector_t, void*, off_t, off_t);

/* Write a block through the buffer cache system */
bool
bcache_write (struct block_device*, block_sector_t, const void*, off_t, off_t);

#endif /* filesys/bcache.h */
