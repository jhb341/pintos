#include "vm/frame.h"
#include "threads/synch.h"
#include "vm/swap.h"

static struct list frameTable; /* 프레임 테이블, 실제 fte 소유 주체 */
static struct lock fTableLock;  /* frame table에 대한 atomic access를 구현 */
static struct fte *victim_pointer; /* fte에서 어떤 frame을 evict해야하나? (가르키는 대상이 fte이므로 type도 fte)*/

/*
    frame, fte등의 initialization.
    frameTable의 초기화 -> lock의 초기화 -> evict pointer의 초기화
    각 요소의 초기화는 기존에 구현된 함수를 사용한다.

    어디서 호출? 
        -> thread system이 initialized되는 곳에서 같이 호출되어야 함.
        -> ~/src/threads/init.c의 main()
        -> "Boot complete!" 위에
*/
void
init_Lock_and_Table ()
{
  list_init (&frameTable);
  lock_init (&fTableLock);
  victim_pointer = NULL;
}


/*
    (recall) 모든 PM의 cell들은 frame으로 할당되지 않음 -> fte가 생기고 해당 fte가 PM의 주소를 가져야 비로소 frame이 됨
    바꿔말하면, process가 특정 frame(들)을 요청한다? -> 해당 fte를 찾거나 만들어서 주면 됨.
    fte를 준다는것? -> fte의 주인스레드를 요청한 프로세스로 기록

    falloc_get_page는 프로세스 입장에서 get new page하는 함수
    = 새로운 fte를 만들고 테이블에 insert 
*/
void *
falloc_get_page(enum palloc_flags flags, void *page_addr)
{
  struct fte *e; // 임시로 fte를 만들어둔다.
  void *frame_addr; // 임ㅅ
  lock_acquire (&fTableLock); // table에 대한 접근은 atomic 하게
  frame_addr = palloc_get_page (flags);
  if (frame_addr == NULL)
  {
    /*
    frame_addr = NULL 은, evict후 새로운 자리를 만들어야 함을 의미한다. 
    물리 메모리가 부족해 페이지 요청이 실패한 경우이다. 
     = need to SWAP! and EVICT
    */
    choose_victim(); 
    frame_addr = palloc_get_page (flags);
    if (frame_addr == NULL)
      return NULL; // 그래도 안된다? -> NULL..
  }
  /* if문에 capture되지 않은 이상, 요청된 물리 페이지가 frame_addr에 저장됨 */
  e = (struct fte *)malloc (sizeof *e); /* fte만큼 할당 */

  // 아래는 FTE initiallizing process.
  e->frame_addr = frame_addr; /* 요청받은 frame_addr를 fte의 frame_addr로 설정 */
  e->page_addr = page_addr; 
  e->t = thread_current (); /* 현재 요청한 스레드가 마스터 스레드*/

  // 마무리
  list_push_back (&frameTable, &e->list_elem); /* 테이블에 인서트 */

  lock_release (&fTableLock); 
  return frame_addr; /* 해당 물리 페이지 반환 */
}


void do_free_frame(struct fte *targetFTE)
{
    list_remove(&targetFTE -> list_elem);
    palloc_free_page(targetFTE -> frame_addr);
    pagedir_clear_page(targetFTE -> t -> pagedir, targetFTE -> page_addr);
    free(targetFTE);
}


/*
    위의 함수가 PM을 acquire했으므로, release하는 함수도 짜야한다.
    release하는 과정에서는 기존에 free되는 PM의 page_addr로 접근하는 요청을 fault tlzudi gksek. 
    userprog/process.c에서 page를 acquire, release하는 과정에서 palloc을 썼던 부분을 falloc으로 바꿔야한다. 
*/
void
falloc_free_page (void *frame_addr)
{
  struct fte *e;
  lock_acquire (&fTableLock); // Make ATOMIC
  e = getFte (frame_addr); // free할 PM의 fte를 찾는다. 
  if (e == NULL) // 그런게 없으면, 
    sys_exit (-1); // 시스엑싯

  // free할 PM의 fte가 있으면:
  //list_remove (&e->list_elem); /* table에서 fte 제거 */
  //palloc_free_page (e->frame_addr); /* release */
  //pagedir_clear_page (e->t->pagedir, e->page_addr); // free하는 frame_addr에 대응되던 옛날 page_addr는 pg fault시켜야 한다.
  //free (e);
  do_free_frame(e);

  lock_release (&fTableLock);
}


/*
    frame_addr에 해당하는 fte의 pointer를 반환한다.
 */
struct fte *
getFte (void* frame_addr)
{
  struct list_elem *e;
  for (e = list_begin (&frameTable); e != list_end (&frameTable); e = list_next (e))
    if (list_entry (e, struct fte, list_elem)->frame_addr == frame_addr)
      return list_entry (e, struct fte, list_elem);
  return NULL;
}


/*
void choose_victim() {
  ASSERT(lock_held_by_current_thread(&fTableLock));

  struct fte *e = victim_pointer;
  struct spte *s;

   //BEGIN: Find page to evict 
  do {
    if (e != NULL) {
      pagedir_set_accessed(e->t->pagedir, e->page_addr, false);
    }

    if (victim_pointer == NULL || list_next(&victim_pointer->list_elem) == list_end(&frameTable)) {
      e = list_entry(list_begin(&frameTable), struct fte, list_elem);
    } else {
      e = list_next (e);
    }
  } while (!pagedir_is_accessed(e->t->pagedir, e->page_addr));
  // END : Find page to evict 

  s = get_spte(&thread_current()->spt, e->page_addr);
  s->status = PAGE_SWAP;
  s->swap_id = swap_out(e->frame_addr);

  lock_release(&fTableLock); {
    falloc_free_page(e->frame_addr);
  } lock_acquire(&fTableLock);
}
*/

void choose_victim() {
    /* fTableLock 락이 현재 스레드에 의해 획득되어 있는지 검증 */
    ASSERT(lock_held_by_current_thread(&fTableLock));

    /* Clock 알고리즘을 위한 커서 */
    struct fte *candidate = victim_pointer;
    struct spte *pte_entry;

    /* ============================
       희생 페이지 선택 과정
       Clock 알고리즘:
       accessed 비트를 0으로 클리어 후
       여전히 accessed이면 다음 페이지로 이동
       accessed가 false인 페이지 발견 시 중단
       ============================ */
    for (;;) {
        /* 현재 candidate 페이지의 accessed 비트 비활성화 */
        if (candidate != NULL) {
            pagedir_set_accessed(candidate->t->pagedir, candidate->page_addr, false);
        }

        /* 다음 프레임으로 이동:
           victim_pointer가 끝이나 NULL이라면 frameTable의 처음으로 돌아감 */
        if (victim_pointer == NULL 
            || list_next(&victim_pointer->list_elem) == list_end(&frameTable)) 
        {
            candidate = list_entry(list_begin(&frameTable), struct fte, list_elem);
        } else {
            candidate = list_entry(list_next(&candidate->list_elem), struct fte, list_elem);
        }

        /* accessed 비트 다시 확인:
           false면 희생 페이지로 결정 후 반복 종료 */
        if (!pagedir_is_accessed(candidate->t->pagedir, candidate->page_addr)) {
            break;
        }
    }

    /* SPTE 가져와 스왑 상태로 전환 */
    pte_entry = get_spte(&thread_current()->spt, candidate->page_addr);
    pte_entry->status = PAGE_SWAP;
    pte_entry->swap_id = swap_out(candidate->frame_addr);

    /* 프레임 해제:
       락 해제 후 페이지 해제, 다시 락 획득 */
    lock_release(&fTableLock); {
        falloc_free_page(candidate->frame_addr);
    } lock_acquire(&fTableLock);
}

