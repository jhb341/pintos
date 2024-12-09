#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "filesys/file.h"
#include "filesys/off_t.h"

#define PAGE_ZERO 0
#define PAGE_FRAME 1
#define PAGE_FILE 2
#define PAGE_SWAP 3

struct spte
  {
    //////////
    //void *frame_addr;    /* VA: Kernel virtual page. */
    //void *page_addr;    /* PA: User virtual page. */

    //struct thread *t;   /* 어떤 thread가 이 frame(그의 입장에서는 page겠지만,)을 소유하나? */

    //struct list_elem list_elem; /* 실제 fte가 연결되는 frame table(table은 list로 구현되므로)에 insert*/

    void *frame_addr;  /* PA */
    void *page_addr;   /* VA */
  
    struct hash_elem hash_elem;  // list_elem대신 hash_elem을 써야함
  
    int status;
        // 가능한 TYPE, prepare memory에서 처리
        // PAGE_ZERO  : zeroing
        // PAGE_FILE  : file 읽고 load
        // PAGE_SWAP  : from swap disk

    struct file *file;  // File to read.
    off_t ofs;  // File off set.
    uint32_t read_bytes, zero_bytes;  // Bytes to read or to set to zero.
    bool isWritable;  // whether the page is writable.
    int swap_id;
  };

void init_spt (struct hash *spt);
void destroy_spt (struct hash *spt);
void init_spte (struct hash *spt, void *page_addr, void *frame_addr);
void init_zero_spte (struct hash *spt, void *page_addr);
void init_frame_spte (struct hash *spt, void *page_addr, void *frame_addr);
struct spte *init_file_spte (struct hash *spt, void *p_a, struct file *f, off_t os, uint32_t rb, uint32_t zb, bool flag);
void prepare_mem_page(struct spte *spte, void *frame_addr, bool flag);
bool do_lazy_load (struct hash *, void *);
struct spte *get_spte (struct hash *, void *);
void delete_and_free (struct hash *spt, struct spte *entry);

#endif