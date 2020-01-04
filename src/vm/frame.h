/*
 * frame.h
 *
 *  Created on: Dec 29, 2019
 *      Author: Beshoy Saad
 */

#ifndef SRC_VM_FRAME_H_
#define SRC_VM_FRAME_H_

void
frame_table_init (void);

void*
frame_alloc (void *user_address, bool zeroed);

void
frame_free (void *kernel_address);

#endif /* SRC_VM_FRAME_H_ */
