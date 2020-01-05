/*
 * frame.h
 *
 *  Created on: Dec 29, 2019
 *      Author: Beshoy Saad
 */

#ifndef SRC_VM_FRAME_H_
#define SRC_VM_FRAME_H_

#include "hash.h"
#include "page.h"

struct frame
{
  struct hash_elem h_elem; /* Hash table element */
  void *kernel_address; /* Kernel virtual address of this frame */
  struct page *user_page;
};

void
frame_table_init (void);

struct frame*
frame_alloc (bool zeroed);

void
frame_free (void *kaddr);

#endif /* SRC_VM_FRAME_H_ */
