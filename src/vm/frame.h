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
  struct semaphore frame_sema;
};

void
frame_table_init (void);

struct frame*
frame_alloc_and_check_out (bool zeroed);

void
frame_free (void *kaddr, bool free_page);

struct frame*
frame_check_out (void *kaddr);

void
frame_check_in (void *kaddr);

#endif /* SRC_VM_FRAME_H_ */
