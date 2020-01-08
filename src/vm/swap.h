/*
 * swap.h
 *
 *  Created on: Dec 29, 2019
 *      Author: Beshoy Saad
 */

#ifndef SRC_VM_SWAP_H_
#define SRC_VM_SWAP_H_

#include "devices/block.h"

void
swap_table_init (void);

block_sector_t
swap_write (void *kaddr);

void
swap_read (block_sector_t sector, void *kaddr);

void
swap_free (block_sector_t sector);

#endif /* SRC_VM_SWAP_H_ */
