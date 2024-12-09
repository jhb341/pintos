#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <bitmap.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "vm/page.h"

void init_swap_table_and_disk();
void swap_in(struct spte *page, void *addr);
int swap_out(void *addr);

#endif