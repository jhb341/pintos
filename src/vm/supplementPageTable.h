#ifndef VM_supplementPageTable_H
#define VM_supplementPageTable_H

#include <hash.h>
#include "filesys/file.h"

enum page_status {
    PAGE_ZERO = 0,   // 빈 페이지 (0으로 초기화된 페이지)
    PAGE_FRAME = 1,  // 물리 메모리에 있는 페이지
    PAGE_FILE = 2,   // 파일 매핑에 사용된 페이지
    PAGE_SWAP = 3    // 스왑 공간에 있는 페이지
};


struct supplementPageTableEntry{
    void* kernel_page; 
    void* physical_page; 

    enum page_status current_status; 

    struct hash_elem supplementPageTableElem; 

    struct file* f; 
    off_t ofs;

    uint32_t bytesToRead; 
    uint32_t bytesToSetZero; 
    bool isWritable; 
}; 

void supp_pt_init(struct hash* supp_pt);

void supp_pte_init_zero(struct hash* supp_pt, void* physical_page); 
void supp_pte_init_frame(struct hash* supp_pt, void* physical_page, void* kernel_page); 
void supp_pte_init_swap(struct hash* supp_pt, void* physical_page); 
void supp_pte_init_file(struct hash* supp_pt, void* physical_page, struct file* fileToRead, off_t ofs, uint32_t bytesToRead, uint32_t bytesToSetZero, bool isWritable); 

void supp_pt_destructor(struct hash_elem* elem_to_destroy, void* aux); 
void destroy_supp_pt(struct hash* supp_pt); 
void supp_pte_delete (struct hash *supp_pt, struct supplementPageTableEntry *spte); 

struct supplementPageTableEntry* get_spte(struct hash *supp_pt, void *physical_page); 
bool lazy_loading (struct hash* supp_pt, void* physical_page); 


#endif