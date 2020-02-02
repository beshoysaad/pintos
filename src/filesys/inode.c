#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/bcache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 				0x494e4f44
/* Sector counts */
#define NUM_DIRECT_SECTORS			124
#define NUM_INDIRECT_SECTORS			128
/* Max supported file size in bytes */
#define MAX_FILE_SIZE				8321536

struct inode_disk_indirect
{
  block_sector_t sector_nums[NUM_INDIRECT_SECTORS];
};

/* On-disk inode.
 Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  off_t length; /* File size in bytes. */
  block_sector_t sector_nums[NUM_DIRECT_SECTORS]; /* Direct pointers. */
  block_sector_t indirect; /* Singly-indirect pointer. */
  block_sector_t doubly_indirect; /* Doubly-indirect pointer. */
  unsigned magic; /* Magic number. */
};

/* Returns the number of sectors to allocate for an inode SIZE
 bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE);
}

static void
disk_inode_remove (struct inode_disk *disk_inode);

/* In-memory inode. */
struct inode
{
  struct list_elem elem; /* Element in inode list. */
  block_sector_t sector; /* Sector number of disk location. */
  int open_cnt; /* Number of openers. */
  bool removed; /* True if deleted, false otherwise. */
  int deny_write_cnt; /* 0: writes ok, >0: deny writes. */
  struct inode_disk data; /* Inode content. */
};

/* Returns the block device sector that contains byte offset POS
 within INODE.
 Returns -1 if INODE does not contain data for a byte at offset
 POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT(inode != NULL);
  if (pos > inode->data.length)
    {
      return -1;
    }
  size_t sector_idx = pos / BLOCK_SECTOR_SIZE;

  if (sector_idx < NUM_DIRECT_SECTORS)
    {
      return inode->data.sector_nums[sector_idx];
    }
  else if (sector_idx < NUM_DIRECT_SECTORS + NUM_INDIRECT_SECTORS)
    {
      sector_idx -= NUM_DIRECT_SECTORS;
      struct inode_disk_indirect ind_node;
      bcache_read (fs_device, inode->data.indirect, &ind_node,
      BLOCK_SECTOR_SIZE,
		   0);
      return ind_node.sector_nums[sector_idx];
    }
  else
    {
      sector_idx -= (NUM_DIRECT_SECTORS + NUM_INDIRECT_SECTORS);
      block_sector_t ind_sector_idx = sector_idx / NUM_INDIRECT_SECTORS;
      struct inode_disk_indirect ind_node, doubly_ind_node;
      bcache_read (fs_device, inode->data.doubly_indirect, &ind_node,
      BLOCK_SECTOR_SIZE,
		   0);
      bcache_read (fs_device, ind_node.sector_nums[ind_sector_idx],
		   &doubly_ind_node, BLOCK_SECTOR_SIZE, 0);
      return doubly_ind_node.sector_nums[(sector_idx % NUM_INDIRECT_SECTORS)];
    }
}

/* List of open inodes, so that opening a single inode twice
 returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
 writes the new inode to sector SECTOR on the file system
 device.
 Returns true if successful.
 Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;

  ASSERT(length >= 0);

  /* If this assertion fails, the inode structure is not exactly
   one sector in size, and you should fix that. */
  ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  int32_t max_size = (fs_device->size
      - (3 + NUM_INDIRECT_SECTORS + fs_device->size / (8 * BLOCK_SECTOR_SIZE)))
      * BLOCK_SECTOR_SIZE;

  // Greater than max supported size
  if (length > max_size)
    {
      return false;
    }

  disk_inode = calloc (1, sizeof *disk_inode);
  ASSERT(disk_inode != NULL);

  disk_inode->length = length;
  disk_inode->magic = INODE_MAGIC;

  static char zeros[BLOCK_SECTOR_SIZE];

  int32_t rem_length = length;

  // Direct pointers
  unsigned i = 0;
  do
    {
      if (!free_map_allocate (1, &disk_inode->sector_nums[i]))
	{
	  goto out_of_disk_space;
	}
      bcache_write (fs_device, disk_inode->sector_nums[i], zeros,
      BLOCK_SECTOR_SIZE,
		    0);
      i++;
      rem_length -= BLOCK_SECTOR_SIZE;
    }
  while ((rem_length > 0) && (i < NUM_DIRECT_SECTORS));

  // Indirect pointers
  if (rem_length > 0)
    {
      struct inode_disk_indirect *indirect = calloc (1, sizeof *indirect);
      ASSERT(indirect != NULL);
      if (!free_map_allocate (1, &disk_inode->indirect))
	{
	  free (indirect);
	  goto out_of_disk_space;
	}
      unsigned i = 0;
      do
	{
	  if (!free_map_allocate (1, &indirect->sector_nums[i]))
	    {
	      free (indirect);
	      goto out_of_disk_space;
	    }
	  bcache_write (fs_device, indirect->sector_nums[i], zeros,
	  BLOCK_SECTOR_SIZE,
			0);
	  i++;
	  rem_length -= BLOCK_SECTOR_SIZE;
	}
      while ((rem_length > 0) && (i < NUM_INDIRECT_SECTORS));
      bcache_write (fs_device, disk_inode->indirect, indirect,
      BLOCK_SECTOR_SIZE,
		    0);
      free (indirect);
    }

  // Doubly-indirect pointers
  if (rem_length > 0)
    {
      struct inode_disk_indirect *doubly_indirect = calloc (
	  1, sizeof *doubly_indirect);
      ASSERT(doubly_indirect != NULL);
      if (!free_map_allocate (1, &disk_inode->doubly_indirect))
	{
	  free (doubly_indirect);
	  goto out_of_disk_space;
	}
      unsigned j = 0;
      do
	{
	  struct inode_disk_indirect *indirect = calloc (1, sizeof *indirect);
	  ASSERT(indirect != NULL);
	  if (!free_map_allocate (1, &doubly_indirect->sector_nums[j]))
	    {
	      free (indirect);
	      free (doubly_indirect);
	      goto out_of_disk_space;
	    }
	  unsigned i = 0;
	  do
	    {
	      if (!free_map_allocate (1, &indirect->sector_nums[i]))
		{
		  free (indirect);
		  free (doubly_indirect);
		  goto out_of_disk_space;
		}
	      bcache_write (fs_device, indirect->sector_nums[i], zeros,
	      BLOCK_SECTOR_SIZE,
			    0);
	      i++;
	      rem_length -= BLOCK_SECTOR_SIZE;
	    }
	  while ((rem_length > 0) && (i < NUM_INDIRECT_SECTORS));
	  bcache_write (fs_device, doubly_indirect->sector_nums[j], indirect,
	  BLOCK_SECTOR_SIZE,
			0);
	  free (indirect);
	  j++;
	}
      while ((rem_length > 0) && (j < NUM_INDIRECT_SECTORS));
      bcache_write (fs_device, disk_inode->doubly_indirect, doubly_indirect,
      BLOCK_SECTOR_SIZE,
		    0);
      free (doubly_indirect);
    }

  bcache_write (fs_device, sector, disk_inode, BLOCK_SECTOR_SIZE, 0);

  free (disk_inode);
  return true;

out_of_disk_space:

  disk_inode_remove (disk_inode);
  free (disk_inode);
  return false;
}

/* Reads an inode from SECTOR
 and returns a `struct inode' that contains it.
 Returns a null pointer if memory allocation fails. */
struct inode*
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes); e =
      list_next (e))
    {
      inode = list_entry(e, struct inode, elem);
      if (inode->sector == sector)
	{
	  inode_reopen (inode);
	  return inode;
	}
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  bcache_read (fs_device, inode->sector, &inode->data,
  BLOCK_SECTOR_SIZE,
	       0);
  return inode;
}

/* Reopens and returns INODE. */
struct inode*
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
 If this was the last reference to INODE, frees its memory.
 If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
	{
	  disk_inode_remove (&inode->data);
	  free_map_release (inode->sector, 1);
	}
      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
 has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT(inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
 Returns the number of bytes actually read, which may be less
 than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  block_sector_t sector_idx;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
	break;

      /* Read the block from buffer cache. */
      bcache_read (fs_device, sector_idx, buffer + bytes_read, chunk_size,
		   sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  /* Read ahead the next block as well. */
  if (bytes_read > 0)
    {
      sector_idx++;
      bcache_read_ahead (fs_device, sector_idx);
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
 Returns the number of bytes actually written, which may be
 less than SIZE if end of file is reached or an error occurs.
 (Normally a write at end of file would extend the inode, but
 growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
		off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    {
      return 0;
    }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
	break;

      /* Write the block to buffer cache. */
      bcache_write (fs_device, sector_idx, buffer + bytes_written, chunk_size,
		    sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
 May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
 Must be called once by each inode opener who has called
 inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT(inode->deny_write_cnt > 0);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

static void
disk_inode_remove (struct inode_disk *disk_inode)
{
  ASSERT(disk_inode != NULL);
  int32_t num_sectors = bytes_to_sectors (disk_inode->length);
  // Direct sectors
  if (num_sectors > 0)
    {
      unsigned i = 0;
      do
	{
	  free_map_release (disk_inode->sector_nums[i], 1);
	  i++;
	  num_sectors--;
	}
      while ((num_sectors > 0) && (i < NUM_DIRECT_SECTORS));
    }
  // Indirect sectors
  if (num_sectors > 0)
    {
      struct inode_disk_indirect *indirect = calloc (1, sizeof *indirect);
      bcache_read (fs_device, disk_inode->indirect, indirect, BLOCK_SECTOR_SIZE,
		   0);
      unsigned i = 0;
      do
	{
	  free_map_release (indirect->sector_nums[i], 1);
	  i++;
	  num_sectors--;
	}
      while ((num_sectors > 0) && (i < NUM_INDIRECT_SECTORS));
      free_map_release (disk_inode->indirect, 1);
      free (indirect);
    }
  // Doubly-indirect sectors
  if (num_sectors > 0)
    {
      struct inode_disk_indirect *doubly_indirect = calloc (
	  1, sizeof *doubly_indirect);
      bcache_read (fs_device, disk_inode->doubly_indirect, doubly_indirect,
		   BLOCK_SECTOR_SIZE, 0);
      unsigned j = 0;
      do
	{
	  struct inode_disk_indirect *indirect = calloc (1, sizeof *indirect);
	  bcache_read (fs_device, doubly_indirect->sector_nums[j], indirect,
		       BLOCK_SECTOR_SIZE, 0);
	  unsigned i = 0;
	  do
	    {
	      free_map_release (indirect->sector_nums[i], 1);
	      i++;
	      num_sectors--;
	    }
	  while ((num_sectors > 0) && (i < NUM_INDIRECT_SECTORS));
	  free_map_release (doubly_indirect->sector_nums[j], 1);
	  free (indirect);
	  j++;
	}
      while ((num_sectors > 0) && (j < NUM_INDIRECT_SECTORS));
      free_map_release (disk_inode->doubly_indirect, 1);
      free (doubly_indirect);
    }
}
