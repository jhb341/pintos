#include "vm/page.h"
#include "vm/swap.c"
#include "threads/thread.h"
#include "vm/frame.h"
#include "vm/frame.c"
#include <string.h>
#include "threads/vaddr.h"

static hash_hash_func make_spt_hash;
static hash_less_func comp_spt_va;
static void spte_free (struct hash_elem *elem, void *aux);
extern struct lock FileLock;

void
init_spt (struct hash *spt)
{
  hash_init (spt, make_spt_hash, comp_spt_va, NULL);
}

void
free_spt_ (struct hash *spt)
{
  hash_destroy (spt, spte_free);
}



void
init_spte (struct hash *spt, void *page_addr, void *frame_addr)
{
  struct spte *e;
  e = (struct spte *) malloc (sizeof *e);
  
  e->page_addr = page_addr;
  e->frame_addr = frame_addr;
  
  e->status = PAGE_FRAME;
  
  hash_insert (spt, &e->hash_elem);
}

void
init_spte_zero (struct hash *spt, void *page_addr)
{
  struct spte *e;
  e = (struct spte *) malloc (sizeof *e);
  
  e->page_addr = page_addr;
  e->frame_addr = NULL;
  
  e->status = PAGE_ZERO;
  
  e->file = NULL;
  e->isWritable = true;
  
  hash_insert (spt, &e->hash_elem);
}

void
init_spte_frame (struct hash *spt, void *page_addr, void *frame_addr)
{
  struct spte *e;
  e = (struct spte *) malloc (sizeof *e);

  e->page_addr = page_addr;
  e->frame_addr = frame_addr;
  
  e->status = PAGE_FRAME;

  e->file = NULL;
  e->isWritable = true;
  
  hash_insert (spt, &e->hash_elem);
}

struct spte *
init_spte_file(struct hash *s, void *_p, struct file *_f, off_t _o, uint32_t _r, uint32_t _z, bool _i)
{
  struct spte *e;
  
  e = (struct spte *)malloc (sizeof *e);

  e->page_addr = _p;
  e->frame_addr = NULL;
  e->file = _f;
  e->ofs = _o;
  e->read_bytes = _r;
  e->zero_bytes = _z;
  e->isWritable = _i;
  
  e->status = PAGE_FILE;
  
  hash_insert (s, &e->hash_elem);
  
  return e;
}

void prepare_mem_page(struct spte *spte, void *frame_addr, bool flag)
{
  switch (spte->status)
  {
  case PAGE_ZERO:
    memset (frame_addr, 0, PGSIZE);
    break;
  case PAGE_SWAP:
    swap_in(spte, frame_addr);
    break;
  case PAGE_FILE:
    if (!flag)
      lock_acquire (&FileLock);
    
    if (file_read_at (spte->file, frame_addr, spte->read_bytes, spte->ofs) != spte->read_bytes)
    {
      falloc_free_page (frame_addr);
      lock_release (&FileLock);
      sys_exit (-1);
    }
    memset (frame_addr + spte->read_bytes, 0, spte->zero_bytes);
    if (!flag)
      lock_release (&FileLock);

    break;

  default:
    sys_exit (-1);
  }

}



/*
    page의 lazy한 load를 구현함. 
    즉, page fault시의 요청된 page를 선택적으로 load.
    따라서 do_lazy_load는 pg fault handler에서 호출됨
    ..
*/
bool
do_lazy_load (struct hash *spt, void *page_addr)
{
  struct spte *e;
  uint32_t *pagedir;
  void *frame_addr;

  e = get_spte (spt, page_addr);
  if (e == NULL){ sys_exit (-1); }

  frame_addr = falloc_get_page (PAL_USER, page_addr);
  if (frame_addr == NULL){ sys_exit (-1); }

  bool was_holding_lock = lock_held_by_current_thread (&FileLock);

  prepare_mem_page(e, frame_addr, was_holding_lock);
    
  pagedir = thread_current ()->pagedir;

  if (!pagedir_set_page (pagedir, page_addr, frame_addr, e->isWritable))
  {
    falloc_free_page (frame_addr);
    sys_exit (-1);
  }

  e->frame_addr = frame_addr;
  e->status = PAGE_FRAME;

  return true;
}

struct spte *
get_spte (struct hash *spt, void *page_addr)
{
  struct spte e;
  struct hash_elem *elem;

  e.page_addr = page_addr;
  elem = hash_find (spt, &e.hash_elem);

  return elem != NULL ? hash_entry (elem, struct spte, hash_elem) : NULL;
}

static unsigned
make_spt_hash (const struct hash_elem *elem, void *aux)
{
  //struct spte *p = hash_entry(elem, struct spte, hash_elem);

  return hash_bytes (&hash_entry(elem, struct spte, hash_elem)->page_addr, sizeof (hash_entry(elem, struct spte, hash_elem)->frame_addr));
}

static bool 
comp_spt_va (const struct hash_elem *e1, const struct hash_elem *e2, void *aux)
{


  return hash_entry (e1, struct spte, hash_elem)->page_addr < hash_entry (e2, struct spte, hash_elem)->page_addr;
}

static void
spte_free (struct hash_elem *e, void *aux)
{
  free(hash_entry (e, struct spte, hash_elem));
}

void 
delete_and_free (struct hash *spt, struct spte *spte)
{
  hash_delete (spt, &spte->hash_elem);
  free (spte);
}