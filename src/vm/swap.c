/*
 * swap.c
 *
 *  Created on: Dec 29, 2019
 *      Author: Beshoy Saad
 */

#include "debug.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "bitmap.h"
#include "swap.h"

struct block *swap_block = NULL;
struct bitmap *swap_bm = NULL;
struct lock swap_table_lock;

void
swap_table_init (void)
{
  swap_block = block_get_role (BLOCK_SWAP);
  ASSERT(swap_block != NULL);
  swap_bm = bitmap_create (8192);
  ASSERT(swap_bm != NULL);
  lock_init (&swap_table_lock);
}

block_sector_t
swap_write (void *kaddr)
{
  ASSERT(kaddr != NULL);
  block_sector_t s = BITMAP_ERROR;
  lock_acquire (&swap_table_lock);
  s = bitmap_scan_and_flip (swap_bm, 0, 8, false);
  lock_release (&swap_table_lock);
  if (s != BITMAP_ERROR)
    {
      for (int i = 0; i < 8; i++)
	{
	  block_write (swap_block, s + i, kaddr + (i * BLOCK_SECTOR_SIZE));
	}
    }
  return s;
}

void
swap_read (block_sector_t sector, void *kaddr)
{
  ASSERT(sector != BITMAP_ERROR);
  ASSERT(kaddr != NULL);
  for (int i = 0; i < 8; i++)
    {
      block_read (swap_block, sector + i, kaddr + (i * BLOCK_SECTOR_SIZE));
    }
  swap_free (sector);
}

void
swap_free (block_sector_t sector)
{
  ASSERT(sector != BITMAP_ERROR);
  lock_acquire (&swap_table_lock);
  bitmap_set_multiple (swap_bm, sector, 8, false);
  lock_release (&swap_table_lock);
}
