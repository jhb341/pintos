#include "vm/FrameTable.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include <stdlib.h>


static struct list frameTable; 
static struct lock frameTableLock; 

// frame table 초기화 함수 
void 
frame_table_init() {
    list_init(&frameTable); 
    lock_init(&frameTableLock); 
}


// 프레임 테이블 엔트리 생성
static struct frameTableEntry *create_frame_table_entry(void *kpage, void *upage) {

    struct frameTableEntry *fte = (struct frameTableEntry *)malloc(sizeof(*fte));

    if (fte == NULL) { //malloc 실패시 null 반환 
        return NULL; 
    }

    // frame table entry element 들 할당 
    fte->kernel_page = kpage; 
    fte->physical_page = upage;
    fte->t = thread_current();
    list_push_back(&frameTable, &fte->frameTableList); 

    // 새롭게 할당된 fte 반환 
    return fte;
}

// 페이지 프레임 할당 함수
void *falloc_get_page(enum palloc_flags flags, void *upage) {

    struct frameTableEntry *fte;
    void *kpage;

    lock_acquire(&frameTableLock); // 프레임 테이블 락 획득

    // kernel page 할당 
    kpage = palloc_get_page(flags);
    if (kpage == NULL) { // 페이지 할당 실패 시 lock release 후 반환 
        lock_release(&frameTableLock);
        return NULL; 
        // 나중에 swapping 구현할 때 null 말고 disk 에서 값 가져오는 걸로 수정해야될 듯..!! 
    }

    // 프레임 테이블 엔트리 생성
    fte = create_frame_table_entry(kpage, upage);
    if (fte == NULL) { // 프래임 테이블 엔트리 생성 실패 시 kernel page 해제, lock 해제, null 반환 
        palloc_free_page(kpage);
        lock_release(&frameTableLock);
        return NULL;
    }

    lock_release(&frameTableLock);  // 프레임 테이블 락 해제

    return kpage; 
}

// 프레임 엔트리 제거
void remove_frame_entry(struct frameTableEntry *fte) {
    list_remove(&fte->frameTableList); // 프레임 테이블에서 제거
    palloc_free_page(fte->kernel_page); // 물리 페이지 반환
    pagedir_clear_page(fte->t->pagedir, fte->physical_page); // 페이지 매핑 제거 (아래 참고)
    free(fte); // 엔트리 메모리 해제
}

/*
void
pagedir_clear_page (uint32_t *pd, void *upage) 
{
  uint32_t *pte;

  ASSERT (pg_ofs (upage) == 0);
  ASSERT (is_user_vaddr (upage));

  pte = lookup_page (pd, upage, false);
  if (pte != NULL && (*pte & PTE_P) != 0)
    {
      *pte &= ~PTE_P;
      invalidate_pagedir (pd);
    }
}
*/

// kpage 와 일치하는 프레임 테이블 entry 반환하는 함수 
struct frameTableEntry *get_fte(void *kpage) {
    struct list_elem *e;
    for (e = list_begin(&frameTable); e != list_end(&frameTable); e = list_next(e)) {
        struct frameTableEntry *fte = list_entry(e, struct frameTableEntry, frameTableList);
        if (fte->kernel_page == kpage) {
            return fte; 
        }
    }
    return NULL;
}

void falloc_free_page(void *kpage) {
    struct fte *e;

    lock_acquire(&frameTableLock); // 프레임 테이블 락 획득

    e = get_fte(kpage);
    if (e == NULL) { //kpage 와 일치하는 entry 가 없을 경우 error 처리 
        printf("Error: Frame not found.\n");
        lock_release(&frameTableLock);
        return; 
    }

    remove_frame_entry(e);// 프레임 테이블에서 엔트리 제거 및 메모리 해제

    lock_release(&frameTableLock); // 프레임 테이블 락 해제
}

