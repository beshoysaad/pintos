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

enum page_type
{
  PAGE_TYPE_FILE, PAGE_TYPE_SWAP, PAGE_TYPE_ZERO
};

bool
page_table_init (struct hash **pages);

void
page_table_destroy (struct hash *pages, struct lock *l);

void*
page_add (struct hash *pages, struct lock *l, void *upage, enum page_type type);

void
page_remove (struct hash *pages, struct lock *l, void *upage);

#endif /* SRC_VM_PAGE_H_ */
