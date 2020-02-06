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
#define INODE_MAGIC 		0x494e4f44
/* Sector counts */
#define NUM_DIRECT_SECTORS	124
#define NUM_INDIRECT_SECTORS	128
#define MAX_NUM_SECTORS		(NUM_DIRECT_SECTORS + NUM_INDIRECT_SECTORS + NUM_INDIRECT_SECTORS * NUM_INDIRECT_SECTORS)
#define SECTOR_INVALID		0xFFFFFFFF

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

/* Returns the number of sectors to allocate for an inode SIZE
 bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE);
}

static void
disk_inode_remove (struct inode_disk *disk_inode)
{
  ASSERT(disk_inode != NULL);
  // Direct sectors
  for (size_t i = 0; i < NUM_DIRECT_SECTORS; i++)
    {
      if (disk_inode->sector_nums[i] != SECTOR_INVALID)
	{
	  free_map_release (disk_inode->sector_nums[i], 1);
	}
    }
  // Indirect sectors
  if (disk_inode->indirect != SECTOR_INVALID)
    {
      struct inode_disk_indirect *indirect = calloc (1, sizeof *indirect);
      bcache_read (fs_device, disk_inode->indirect, indirect, BLOCK_SECTOR_SIZE,
		   0);
      for (size_t i = 0; i < NUM_INDIRECT_SECTORS; i++)
	{
	  if (indirect->sector_nums[i] != SECTOR_INVALID)
	    {
	      free_map_release (indirect->sector_nums[i], 1);
	    }
	}
      free_map_release (disk_inode->indirect, 1);
      free (indirect);
    }
  // Doubly-indirect sectors
  if (disk_inode->doubly_indirect != SECTOR_INVALID)
    {
      struct inode_disk_indirect *doubly_indirect = calloc (
	  1, sizeof *doubly_indirect);
      bcache_read (fs_device, disk_inode->doubly_indirect, doubly_indirect,
      BLOCK_SECTOR_SIZE,
		   0);
      for (size_t j = 0; j < NUM_INDIRECT_SECTORS; j++)
	{
	  if (doubly_indirect->sector_nums[j] != SECTOR_INVALID)
	    {
	      struct inode_disk_indirect *indirect = calloc (1,
							     sizeof *indirect);
	      bcache_read (fs_device, doubly_indirect->sector_nums[j], indirect,
	      BLOCK_SECTOR_SIZE,
			   0);
	      for (size_t i = 0; i < NUM_INDIRECT_SECTORS; i++)
		{
		  if (indirect->sector_nums[i] != SECTOR_INVALID)
		    {
		      free_map_release (indirect->sector_nums[i], 1);
		    }
		}
	      free_map_release (doubly_indirect->sector_nums[j], 1);
	      free (indirect);
	    }
	}
      free_map_release (disk_inode->doubly_indirect, 1);
      free (doubly_indirect);
    }
}

static inline struct inode_disk_indirect*
create_indirect_inode (block_sector_t *sector)
{
  struct inode_disk_indirect *inode = calloc (1, sizeof *inode);
  ASSERT(inode != NULL);
  memset (inode, 0xFF, sizeof *inode);
  if (*sector == SECTOR_INVALID)
    {
      if (free_map_allocate (1, sector))
	{
	  return inode;
	}
      else
	{
	  free (inode);
	  return NULL;
	}
    }
  else
    {
      bcache_read (fs_device, *sector, inode, BLOCK_SECTOR_SIZE, 0);
      return inode;
    }
}

static bool
grow_inode (block_sector_t sector, struct inode_disk *disk_inode, size_t size)
{
  unsigned num_used_sectors = bytes_to_sectors (disk_inode->length);
  int32_t num_needed_sectors = bytes_to_sectors (size);

  static char zeros[BLOCK_SECTOR_SIZE];

  if ((num_needed_sectors > 0) && (num_used_sectors < NUM_DIRECT_SECTORS))
    {
      do
	{
	  if (!free_map_allocate (1,
				  &disk_inode->sector_nums[num_used_sectors]))
	    {
	      goto out_of_disk_space;
	    }
	  bcache_write (fs_device, disk_inode->sector_nums[num_used_sectors],
			zeros,
			BLOCK_SECTOR_SIZE,
			0);
	  num_used_sectors++;
	  num_needed_sectors--;
	}
      while ((num_needed_sectors > 0) && (num_used_sectors < NUM_DIRECT_SECTORS));
    }
  if ((num_needed_sectors > 0)
      && (num_used_sectors < NUM_DIRECT_SECTORS + NUM_INDIRECT_SECTORS))
    {
      num_used_sectors -= NUM_DIRECT_SECTORS;
      struct inode_disk_indirect *indirect = create_indirect_inode (
	  &disk_inode->indirect);
      if (indirect == NULL)
	{
	  goto out_of_disk_space;
	}
      do
	{
	  if (!free_map_allocate (1, &indirect->sector_nums[num_used_sectors]))
	    {
	      free (indirect);
	      goto out_of_disk_space;
	    }
	  bcache_write (fs_device, indirect->sector_nums[num_used_sectors],
			zeros,
			BLOCK_SECTOR_SIZE,
			0);
	  num_used_sectors++;
	  num_needed_sectors--;
	}
      while ((num_needed_sectors > 0)
	  && (num_used_sectors < NUM_INDIRECT_SECTORS));
      bcache_write (fs_device, disk_inode->indirect, indirect,
      BLOCK_SECTOR_SIZE,
		    0);
      free (indirect);
    }
  if ((num_needed_sectors > 0) && (num_used_sectors < MAX_NUM_SECTORS))
    {
      num_used_sectors -= (NUM_DIRECT_SECTORS + NUM_INDIRECT_SECTORS);
      struct inode_disk_indirect *doubly_indirect = create_indirect_inode (
	  &disk_inode->doubly_indirect);
      if (doubly_indirect == NULL)
	{
	  goto out_of_disk_space;
	}
      unsigned j = num_used_sectors / NUM_INDIRECT_SECTORS;
      do
	{
	  struct inode_disk_indirect *indirect = create_indirect_inode (
	      &doubly_indirect->sector_nums[j]);
	  if (indirect == NULL)
	    {
	      free (doubly_indirect);
	      goto out_of_disk_space;
	    }
	  unsigned i = num_used_sectors % NUM_INDIRECT_SECTORS;
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
	      num_needed_sectors--;
	      num_used_sectors++;
	    }
	  while ((num_needed_sectors > 0) && (i < NUM_INDIRECT_SECTORS));
	  bcache_write (fs_device, doubly_indirect->sector_nums[j], indirect,
	  BLOCK_SECTOR_SIZE,
			0);
	  free (indirect);
	  j++;
	}
      while ((num_needed_sectors > 0) && (j < NUM_INDIRECT_SECTORS));
      bcache_write (fs_device, disk_inode->doubly_indirect, doubly_indirect,
      BLOCK_SECTOR_SIZE,
		    0);
      free (doubly_indirect);
    }

  bcache_write (fs_device, sector, disk_inode, BLOCK_SECTOR_SIZE, 0);

  disk_inode->length += size;

  return true;
out_of_disk_space: return false;
}

/* Returns the block device sector that contains byte offset POS
 within INODE.
 Returns SECTOR_INVALID if INODE does not contain data for a byte at offset
 POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT(inode != NULL);

  block_sector_t ret_val = SECTOR_INVALID;

  size_t sector_idx = pos / BLOCK_SECTOR_SIZE;

  if (sector_idx < NUM_DIRECT_SECTORS)
    {
      ret_val = inode->data.sector_nums[sector_idx];
    }
  else if ((sector_idx < (NUM_DIRECT_SECTORS + NUM_INDIRECT_SECTORS))
      && (inode->data.indirect != SECTOR_INVALID))
    {
      sector_idx -= NUM_DIRECT_SECTORS;
      struct inode_disk_indirect *ind_node = calloc (1, sizeof *ind_node);
      bcache_read (fs_device, inode->data.indirect, ind_node,
      BLOCK_SECTOR_SIZE,
		   0);
      ret_val = ind_node->sector_nums[sector_idx];
      free (ind_node);
    }
  else if ((sector_idx < MAX_NUM_SECTORS)
      && (inode->data.doubly_indirect != SECTOR_INVALID))
    {
      sector_idx -= (NUM_DIRECT_SECTORS + NUM_INDIRECT_SECTORS);
      block_sector_t ind_sector_idx = sector_idx / NUM_INDIRECT_SECTORS;
      struct inode_disk_indirect *ind_node = calloc (1, sizeof *ind_node);
      struct inode_disk_indirect *doubly_ind_node = calloc (
	  1, sizeof *doubly_ind_node);
      bcache_read (fs_device, inode->data.doubly_indirect, doubly_ind_node,
      BLOCK_SECTOR_SIZE,
		   0);
      if (doubly_ind_node->sector_nums[ind_sector_idx] == SECTOR_INVALID)
	{
	  ret_val = SECTOR_INVALID;
	}
      else
	{
	  bcache_read (fs_device, doubly_ind_node->sector_nums[ind_sector_idx],
		       ind_node, BLOCK_SECTOR_SIZE, 0);
	  ret_val = ind_node->sector_nums[sector_idx % NUM_INDIRECT_SECTORS];
	}
      free (ind_node);
      free (doubly_ind_node);
    }
  else
    {
      ret_val = SECTOR_INVALID;
    }
  return ret_val;
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

  /* If this assertion fails, the inode structure is not exactly
   one sector in size, and you should fix that. */
  ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  // Max file size is the size of the disk - meta data - free map
  off_t max_size = (fs_device->size
      - (3 + NUM_INDIRECT_SECTORS + fs_device->size / (8 * BLOCK_SECTOR_SIZE)))
      * BLOCK_SECTOR_SIZE;

  // Check if greater than max supported size
  if (length > max_size)
    {
      return false;
    }

  disk_inode = calloc (1, sizeof *disk_inode);
  ASSERT(disk_inode != NULL);

  disk_inode->length = 0;
  disk_inode->magic = INODE_MAGIC;
  disk_inode->indirect = SECTOR_INVALID;
  disk_inode->doubly_indirect = SECTOR_INVALID;
  memset (disk_inode->sector_nums, 0xFF, sizeof(disk_inode->sector_nums));

  if (!grow_inode (sector, disk_inode, length))
    {
      disk_inode_remove (disk_inode);
      free (disk_inode);
      return false;
    }
  else
    {
      bcache_write (fs_device, sector, disk_inode, BLOCK_SECTOR_SIZE, 0);
      free (disk_inode);
      return true;
    }
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

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      if (offset >= inode->data.length)
	{
	  break;
	}
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      ASSERT(sector_idx != SECTOR_INVALID);

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

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
 Returns the number of bytes actually written, which may be
 less than SIZE if an error occurs.
 */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
		off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if ((inode->deny_write_cnt) || (size <= 0))
    {
      return 0;
    }

  if (offset + size > inode->data.length)
    {
      if (!grow_inode (inode->sector, &inode->data,
		       offset + size - inode->data.length))
	{
	  return 0;
	}
    }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      ASSERT(sector_idx != SECTOR_INVALID);

      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < sector_left ? size : sector_left;
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

  bcache_write (fs_device, inode->sector, &inode->data, BLOCK_SECTOR_SIZE, 0);

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

