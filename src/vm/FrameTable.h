#ifndef VM_FRAMETABLE_H
#define VM_FRAMETABLE_H

#include <list.h>
#include "threads/thread.h"


struct frameTableEntry {
    void* kernel_page; 
    void* physical_page; 

    struct thread* t; 

    struct list_elem frameTableList; 
}; 

void frame_table_init();
static struct frameTableEntry* create_frame_table_entry(void *kpage, void *upage); 
void* falloc_get_page(enum palloc_flags flags, void *upage); 
void remove_frame_entry(struct frameTableEntry *fte); 
struct frameTableEntry* get_fte(void *kpage); 
void falloc_free_page(void *kpage); 



#endif