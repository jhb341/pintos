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



#endif