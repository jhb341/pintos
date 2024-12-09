#include "vm/swap.h"
#include "threads/synch.h"

#define SECTOR_NUM (PGSIZE / BLOCK_SECTOR_SIZE)

static struct bitmap *swap_valid_table;
static struct block *swapDisk;
static struct lock swapLock;

void init_swap_valid_table()
{
    swapDisk = block_get_role(BLOCK_SWAP);
    swap_valid_table = bitmap_create(block_size(swapDisk) / SECTOR_NUM);

    bitmap_set_all(swap_valid_table, true);
    lock_init(&swapLock);
}


void swap_in(struct spte *page, void *kva)
{
    int i;
    int id = page->swap_id;

    lock_acquire(&swapLock);
    {
        if (id > bitmap_size(swap_valid_table) || id < 0)
        {
            sys_exit(-1);
        }

        if (bitmap_test(swap_valid_table, id) == true)
        {
            // This swapping slot is empty. 
            sys_exit(-1);
        }

        bitmap_set(swap_valid_table, id, true);
    }

    lock_release(&swapLock);

    for (i = 0; i < SECTOR_NUM; i++)
    {
        block_read(swapDisk, id * SECTOR_NUM + i, kva + (i * BLOCK_SECTOR_SIZE));
    }
}

int swap_out(void *kva)
{
    int i;
    int id;

    lock_acquire(&swapLock);
    {
        id = bitmap_scan_and_flip(swap_valid_table, 0, 1, true);
    }
    lock_release(&swapLock);

    for (i = 0; i < SECTOR_NUM; ++i)
    {
        block_write(swapDisk, id * SECTOR_NUM + i, kva + (BLOCK_SECTOR_SIZE * i));
    }

    return id;
}

