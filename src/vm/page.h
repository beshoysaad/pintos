/*
 * page.h
 *
 *  Created on: Dec 29, 2019
 *      Author: Beshoy Saad
 */

#ifndef SRC_VM_PAGE_H_
#define SRC_VM_PAGE_H_

#include "hash.h"
#include "threads/synch.h"
#include "filesys/file.h"

enum page_type
{
  PAGE_TYPE_FILE, PAGE_TYPE_SWAP, PAGE_TYPE_ZERO
};

struct file_storage
{
  struct file *f;
  off_t size;
  off_t offset;
};

union page_storage
{
  struct file_storage fs;
};

struct page
{
  struct hash_elem h_elem;
  void *user_address;
  struct frame *f;
  enum page_type type;
  bool writable;
  union page_storage ps;
};

bool
page_table_init (struct hash **page_table);

void
page_table_destroy (void);

struct page*
page_alloc (void *upage, enum page_type type, bool writable);

void
page_free (void *uaddr);

struct page*
page_get (void *upage);

#endif /* SRC_VM_PAGE_H_ */
