/*
 * frame.h
 *
 *  Created on: Dec 29, 2019
 *      Author: Beshoy Saad
 */

#ifndef SRC_VM_FRAME_H_
#define SRC_VM_FRAME_H_

#include "hash.h"

struct frame
{
  struct hash_elem h_elem; /* Hash table element */
  void *kernel_address; /* Kernel virtual address of this frame */
  void *user_address; /* User virtual address of the page occupying this frame */
  uint8_t unaccessed_count; /* Used in implementing the clock eviction algorithm */
  struct thread *t; /* Pointer to the thread owning the page in this frame */
};

void
frame_table_init (void);

struct frame*
frame_alloc (void *user_address, bool zeroed);

void
frame_free (void *kernel_address);

#endif /* SRC_VM_FRAME_H_ */
