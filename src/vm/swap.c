#include "vm/swap.h"
#include "threads/synch.h"

#define SECTOR_NUM (PGSIZE / BLOCK_SECTOR_SIZE)

static struct bitmap *swapTable;
static struct block *swapDisk;
static struct lock swapLock;

void init_swap_table_and_disk()
{
    swapDisk = block_get_role(BLOCK_SWAP);
    swapTable = bitmap_create(block_size(swapDisk) / SECTOR_NUM);
    bitmap_set_all(swapTable, true);
    lock_init(&swapLock);
}


void swap_in(struct spte *page, void *addr)
{

    lock_acquire(&swapLock);
    
        if (page->swap_id > bitmap_size(swapTable) || page->swap_id < 0)
        {
            sys_exit(-1);
        }

        if (bitmap_test(swapTable, page->swap_id) == true)
        {
            // This swapping slot is empty. 
            sys_exit(-1);
        }

        bitmap_set(swapTable, page->swap_id, true);
    

    lock_release(&swapLock);

    for (int i = 0; i < SECTOR_NUM; i++)
    {
        block_read(swapDisk, page->swap_id * SECTOR_NUM + i, addr + (i * BLOCK_SECTOR_SIZE));
    }
}

int swap_out(void *addr)
{
    int id;

    lock_acquire(&swapLock);
    
        id = bitmap_scan_and_flip(swapTable, 0, 1, true);
    
    lock_release(&swapLock);

    for (int i = 0; i < SECTOR_NUM; ++i)
    {
        block_write(swapDisk, id * SECTOR_NUM + i, addr + (BLOCK_SECTOR_SIZE * i));
    }

    return id;
}

