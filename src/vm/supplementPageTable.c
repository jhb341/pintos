#include <string.h>

#include "vm/supplementPageTable.h"
#include "vm/frameTable.h"
#include "threads/thread.h"
#include "../lib/kernel/hash.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"


/*
these two data are from /lib/kernel/hash.h 
Computes and returns the hash value for hash element E, given
   auxiliary data AUX.
typedef unsigned hash_hash_func (const struct hash_elem *e, void *aux);

Compares the value of two hash elements A and B, given
   auxiliary data AUX.  Returns true if A is less than B, or
   false if A is greater than or equal to B.
typedef bool hash_less_func (const struct hash_elem *a,
                             const struct hash_elem *b,
                             void *aux);
*/
static struct lock supplementPageTableLock; 

// hash structure has num of elements in the table, number of buckets (a power of 2), 
// a list of buckets, hash function (hash_hash_func), comparison function (hash_less_func), aux?? (이건 뭔지 모르겠음)
static hash_hash_func hash_func; 
static hash_less_func comp_hash_func; 
static void supp_pt_destructor(struct hash_elem* elem_to_destroy, void* aux); 

// 구현 필요..!! 
static unsigned 
hash_func() {

}
static bool 
comp_hash_func(){

} 

void 
supp_pt_init(struct hash* supp_pt){
    hash_init (supp_pt, hash_func, comp_hash_func, NULL);  
}

// PAGE-ZERO : 빈페이지 생성 
void 
supp_pte_init_zero(struct hash* supp_pt, void* physical_page){

   struct supplementPageTableEntry* spte = (struct supplementPageTableEntry*) malloc (sizeof * spte); 

   spte -> physical_page = physical_page; 
   spte -> kernel_page = NULL; 

   spte -> current_status = PAGE_ZERO; 

   spte -> f = NULL; 
   spte -> isWritable = true; 

   hash_insert(supp_pt, &spte->supplementPageTableElem); 
}

// PAGE FRAME : 물리 메모리에 매핑되어있는 상태 
void 
supp_pte_init_frame(struct hash* supp_pt, void* physical_page, void* kernel_page){

   struct supplementPageTableEntry* spte = (struct supplementPageTableEntry*) malloc (sizeof * spte); 

   spte -> physical_page = physical_page; 
   spte -> kernel_page = kernel_page; 

   spte -> current_status = PAGE_FRAME; 

   spte -> f = NULL; 
   spte -> isWritable = true; 

   hash_insert(supp_pt, &spte->supplementPageTableElem); 
}

// PAGE SWAP : SWAP 공간에 임시 저장된 곳에서 DATA 가져오기  
void 
supp_pte_init_swap(struct hash* supp_pt, void* physical_page){

   struct supplementPageTableEntry* spte = (struct supplementPageTableEntry*) malloc (sizeof * spte); 

   spte -> physical_page = physical_page; 
   spte -> kernel_page = NULL; 

   spte -> current_status = PAGE_FRAME; 

   spte -> f = NULL; 
   spte -> isWritable = true; 

   hash_insert(supp_pt, &spte->supplementPageTableElem); 
}


// PAGE FILE: 파일에 매핑된 페이지? 
void 
supp_pte_init_file(struct hash* supp_pt, void* physical_page, struct file* fileToRead, off_t ofs, uint32_t bytesToRead, uint32_t bytesToSetZero, bool isWritable){

   struct supplementPageTableEntry* spte = (struct supplementPageTableEntry*) malloc (sizeof * spte); 

   spte -> physical_page = physical_page; 
   spte -> kernel_page = NULL; 

   spte -> current_status = PAGE_FILE; 

   spte -> f = fileToRead; 
   spte -> ofs = ofs; 
   spte -> bytesToRead = bytesToRead; 
   spte -> bytesToSetZero = bytesToSetZero; 
   spte -> isWritable = isWritable; 

   hash_insert(supp_pt, &spte->supplementPageTableElem); 
}

void 
supp_pt_destructor(struct hash_elem* elem_to_destroy, void* aux){
    if (elem_to_destroy == NULL) {
        return;
    }

    // Retrieve the spte struct
    /*
    #define hash_entry(HASH_ELEM, STRUCT, MEMBER)                   \
        ((STRUCT *) ((uint8_t *) &(HASH_ELEM)->list_elem        \
                     - offsetof (STRUCT, MEMBER.list_elem)))
    */

   // 왜 error 나는지 모르겠음.. # include 는 했는데??? 
    struct supplementPageTableEntry *spte = hash_entry(elem_to_destroy, struct supplementPageTableEntry, hash_elem);

    if (spte != NULL) {
        // 이 spte 에 있는 * 들 free 해주기 

        if (spte->kernel_page != NULL) {
            free(spte->kernel_page);
            spte->kernel_page = NULL;
        }

        if (spte->physical_page != NULL) {
            free(spte->physical_page);
            spte->physical_page = NULL;
        }

        if (spte->f != NULL) {
            file_close(spte->f); 
            spte->f = NULL;
        }

        // Free the spte itself
        free(spte);
        spte = NULL;
    }
}

// 전체 supplement page table 삭제 
void 
destroy_supp_pt(struct hash* supp_pt){
    hash_destroy (supp_pt, supp_pt_destructor); 
}

// spte 하나 삭제 
void 
supp_pte_delete (struct hash *supp_pt, struct supplementPageTableEntry *spte){
    /*
    struct hash_elem *
    hash_delete (struct hash *h, struct hash_elem *e)
    {
        struct hash_elem *found = find_elem (h, find_bucket (h, e), e);
        if (found != NULL) {
            remove_elem (h, found);
            rehash (h); 
        }
        return found;
    }
    */
    hash_delete (supp_pt, &spte->supplementPageTableElem);
    free (spte);
}

struct supplementPageTableEntry*
get_spte(struct hash *supp_pt, void *physical_page){
    struct supplementPageTableEntry spte;
    struct hash_elem *elem;

    spte.physical_page = physical_page;
    elem = hash_find(supp_pt, &spte.supplementPageTableElem);

    if (elem != NULL) {
        return hash_entry(elem, struct spte, hash_elem); // 여기서 hash_elem 이 뭐가 offset 으로 들어가야하는지 모르겟음 
    } else {
        return NULL;
    }
}


bool 
lazy_loading(struct hash *supp_pt, void *physical_page) {
    struct supplementPageTableEntry *spte = get_spte(supp_pt, physical_page);
    if (spte == NULL) {
        sys_exit(-1);
    }

    void *kernel_page = falloc_get_page(PAL_USER, physical_page);
    if (kernel_page == NULL) {
        sys_exit(-1);
    }

    // 공유 자원을 보호하기 위해 락을 획득
    lock_acquire(&supplementPageTableLock);

    switch (spte->current_status) {
        case PAGE_ZERO:
            // 메모리를 0으로 초기화
            memset(kernel_page, 0, PGSIZE);
            break;

        case PAGE_SWAP:
            // 스왑에서 데이터를 가져오는 로직 추가
            swap_in(spte, kernel_page); // 스왑 처리 함수 호출
            break;

        case PAGE_FILE:
            // 파일에서 데이터 읽기
            uint32_t bytes_read = file_read_at(spte->f, kernel_page, spte->bytesToRead, spte->ofs);
            if (bytes_read != spte->bytesToRead) {
                falloc_free_page(kernel_page);
                lock_release(&supplementPageTableLock);
                sys_exit(-1);
            }

            // 읽지 않은 영역을 0으로 초기화
            void *starting_addr = (uint8_t *)kernel_page + spte->bytesToRead;
            memset(starting_addr, 0, spte->bytesToSetZero);
            break;

        case PAGE_FRAME:
            // 이미 로드된 페이지 처리
            break;

        default:
            lock_release(&supplementPageTableLock);
            sys_exit(-1);
    }

    // 페이지 디렉터리 접근 보호
    uint32_t *pagedir = thread_current()->pagedir;
    if (!pagedir_set_page(pagedir, physical_page, kernel_page, spte->isWritable)) {
        falloc_free_page(kernel_page);
        lock_release(&supplementPageTableLock);
        sys_exit(-1);
    }

    // SPTE 업데이트 (락으로 보호된 상태)
    spte->kernel_page = kernel_page;
    spte->current_status = PAGE_FRAME;

    // 락 해제
    lock_release(&supplementPageTableLock);

    return true;
}


