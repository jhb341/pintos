#include "vm/frame.h"
#include "threads/synch.h"
#include "vm/swap.h"

static struct list frame_table; /* 실제 fte 소유 주체 */
static struct lock frame_lock;  /* frame table에 대한 atomic access를 구현 */
static struct fte *clock_cursor; /* fte에서 어떤 frame을 evict해야하나? (가르키는 대상이 fte이므로 type도 fte)*/

/*
    frame, fte등의 initialization.
    frame_table의 초기화 -> lock의 초기화 -> evict pointer의 초기화
    각 요소의 초기화는 기존에 구현된 함수를 사용한다.

    어디서 호출? 
        -> thread system이 initialized되는 곳에서 같이 호출되어야 함.
        -> ~/src/threads/init.c의 main()
        -> "Boot complete!" 위에
*/
void
frame_init ()
{
  list_init (&frame_table);
  lock_init (&frame_lock);
  clock_cursor = NULL;
}


/*
    (recall) 모든 PM의 cell들은 frame으로 할당되지 않음 -> fte가 생기고 해당 fte가 PM의 주소를 가져야 비로소 frame이 됨
    바꿔말하면, process가 특정 frame(들)을 요청한다? -> 해당 fte를 찾거나 만들어서 주면 됨.
    fte를 준다는것? -> fte의 주인스레드를 요청한 프로세스로 기록

    falloc_get_page는 프로세스 입장에서 get new page하는 함수
    = 새로운 fte를 만들고 테이블에 insert 
*/
void *
falloc_get_page(enum palloc_flags flags, void *upage)
{
  struct fte *e;
  void *kpage;
  lock_acquire (&frame_lock); // table에 대한 접근은 atomic 하게
  kpage = palloc_get_page (flags);
  if (kpage == NULL)
  {
    /*
    kpage = NULL 은, evict후 새로운 자리를 만들어야 함을 의미한다. 
    물리 메모리가 부족해 페이지 요청이 실패한 경우이다. 
     = need to SWAP! and EVICT
    */
    evict_page(); 
    kpage = palloc_get_page (flags);
    if (kpage == NULL)
      return NULL; // 그래도 안된다? -> NULL..
  }
  /* if문에 capture되지 않은 이상, 요청된 물리 페이지가 kpage에 저장됨 */
  e = (struct fte *)malloc (sizeof *e); /* fte만큼 할당 */

  // 아래는 FTE initiallizing process.
  e->kpage = kpage; /* 요청받은 kpage를 fte의 kpage로 설정 */
  e->upage = upage; 
  e->t = thread_current (); /* 현재 요청한 스레드가 마스터 스레드*/

  // 마무리
  list_push_back (&frame_table, &e->list_elem); /* 테이블에 인서트 */

  lock_release (&frame_lock); 
  return kpage; /* 해당 물리 페이지 반환 */
}


/*
    위의 함수가 PM을 acquire했으므로, release하는 함수도 짜야한다.
    release하는 과정에서는 기존에 free되는 PM의 upage로 접근하는 요청을 fault tlzudi gksek. 
    userprog/process.c에서 page를 acquire, release하는 과정에서 palloc을 썼던 부분을 falloc으로 바꿔야한다. 
*/
void
falloc_free_page (void *kpage)
{
  struct fte *e;
  lock_acquire (&frame_lock); // Make ATOMIC
  e = get_fte (kpage); // free할 PM의 fte를 찾는다. 
  if (e == NULL) // 그런게 없으면, 
    sys_exit (-1); // 시스엑싯

  // free할 PM의 fte가 있으면:
  list_remove (&e->list_elem); /* table에서 fte 제거 */
  palloc_free_page (e->kpage); /* release */
  pagedir_clear_page (e->t->pagedir, e->upage); // free하는 kpage에 대응되던 옛날 upage는 pg fault시켜야 한다.
  free (e);

  lock_release (&frame_lock);
}


/*
    kpage에 해당하는 fte의 pointer를 반환한다.
 */
struct fte *
get_fte (void* kpage)
{
  struct list_elem *e;
  for (e = list_begin (&frame_table); e != list_end (&frame_table); e = list_next (e))
    if (list_entry (e, struct fte, list_elem)->kpage == kpage)
      return list_entry (e, struct fte, list_elem);
  return NULL;
}

void evict_page() {
  ASSERT(lock_held_by_current_thread(&frame_lock));

  struct fte *e = clock_cursor;
  struct spte *s;

  /* BEGIN: Find page to evict */
  do {
    if (e != NULL) {
      pagedir_set_accessed(e->t->pagedir, e->upage, false);
    }

    if (clock_cursor == NULL || list_next(&clock_cursor->list_elem) == list_end(&frame_table)) {
      e = list_entry(list_begin(&frame_table), struct fte, list_elem);
    } else {
      e = list_next (e);
    }
  } while (!pagedir_is_accessed(e->t->pagedir, e->upage));
  /*  END : Find page to evict */

  s = get_spte(&thread_current()->spt, e->upage);
  s->status = PAGE_SWAP;
  s->swap_id = swap_out(e->kpage);

  lock_release(&frame_lock); {
    falloc_free_page(e->kpage);
  } lock_acquire(&frame_lock);
}