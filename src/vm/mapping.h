/*
 * mapping.h
 *
 *  Created on: Jan 9, 2020
 *      Author: Beshoy Saad
 */

#ifndef SRC_VM_MAPPING_H_
#define SRC_VM_MAPPING_H_

#include "hash.h"
#include "lib/user/syscall.h"
#include "filesys/file.h"

struct mapping
{
  struct hash_elem h_elem;
  mapid_t map_id;
  void *upage;
  int num_pages;
  struct process *proc;
};

bool
mapping_table_init (struct hash **mapping_table);

void
mapping_table_destroy (struct process *proc);

struct mapping*
mapping_alloc (struct process *proc, void *upage, struct file *f);

void
mapping_free (struct process *proc, mapid_t map_id);

#endif /* SRC_VM_MAPPING_H_ */
