#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include "threads/malloc.h"

/*
    fte는 frame table의 element로서 실제 PM의 한 frame에 대한 정보를 저장한다.
*/
struct fte
  {
    void *frame_addr;    /* VA: Kernel virtual page. */
    void *page_addr;    /* PA: User virtual page. */

    struct thread *t;   /* 어떤 thread가 이 frame(그의 입장에서는 page겠지만,)을 소유하나? */

    struct list_elem list_elem; /* 실제 fte가 연결되는 frame table(table은 list로 구현되므로)에 insert*/
  };

void init_Lock_and_Table (void);
void *falloc_get_page(enum palloc_flags, void *);
void do_free_frame(struct fte *targetFTE);
void  falloc_free_page (void *); 
struct fte *getFte (void* );   // fte의 pointer를 qksghks

#endif