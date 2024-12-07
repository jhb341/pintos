#include <string.h>

#include "vm/supplementPageTable.h"
#include "vm/frameTable.h"
#include "threads/thread.h"
#include "../lib/kernel/hash.h"
#include "threads/vaddr.h"


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
lazy_loading (struct hash* supp_pt, void* physical_page){

    struct supplementPageTableEntry* spte = get_spte(supp_pt, physical_page); 

    if (spte == NULL){
        sys_exit (-1);
    }
  
    void* kernel_page = falloc_get_page(enum palloc_flags flags, physical_page); // 여기 flag 가 뭐가 되야하는지 모르겟음 
    if (kernel_page == NULL){
        sys_exit (-1);
    }

    bool hold_lock = supplementPageTableLock.holder == thread_current()? true: false; 

    switch (spte->current_status){
        case PAGE_ZERO:
            //void *memset(void *s, int c, size_t n);
            // s = 초기화 할 메모리의 시작 주소 
            // c = 메모리 채울 값 
            // n = 채울 바이트 수 
            memset (kernel_page, 0, PGSIZE);
            break;
        case PAGE_SWAP:
            // swapping logic needs to be added 
            break;
        
        case PAGE_FILE:
            if (!hold_lock)
            lock_acquire (&supplementPageTableLock);
            
            // kernel page 에 파일 data 저장하기 
            uint32_t bytes_read = file_read_at (spte->f, kernel_page, spte->bytesToRead, spte->ofs); 

            if (bytes_read != spte -> bytesToRead) { // 파일 읽기 실패 시 
            falloc_free_page (kernel_page);
            lock_release (&supplementPageTableLock);
            sys_exit (-1);
            }
            
            void* starting_addr = (uint8_t*)kernel_page + spte->bytesToRead;

            memset (starting_addr, 0, spte -> bytesToSetZero);
            lock_release (&supplementPageTableLock);
            
            break;

        case PAGE_FRAME:
            // 이미 로드된 페이지
            break;
        default:
            sys_exit (-1);
    }
    
    uint32_t* pagedir = thread_current ()->pagedir;

    // src\userprog\pagedir.c
    //bool pagedir_set_page (uint32_t *pd, void *upage, void *kpage, bool writable)

    if (!pagedir_set_page (pagedir, physical_page, kernel_page, spte -> isWritable)){
        falloc_free_page (kernel_page);
        sys_exit (-1);
    }

    spte->kernel_page = kernel_page;
    spte->current_status = PAGE_FRAME;

    return true;
}

