# CSED 312 Project 3 Final Report 
김지현(20220302) 
전현빈(20220259)

## Design Implementation 

### 1. Frame table

### Implementation & Improvement from the previous design 

frame table은 list 로 구현할 예정이며 각 list 에 들어갈 entry 를 vm directory 에 frame.h 파일을 만들어 생성하였다. 

```
./vm/frame.h
struct fte
  {
    void *frame_addr;    /* VA: Kernel virtual page. */
    void *page_addr;    /* PA: User virtual page. */
    struct thread *t;   /* 어떤 thread가 이 frame(그의 입장에서는 page겠지만,)을 소유하나? */
    struct list_elem list_elem; /* 실제 fte가 연결되는 frame table(table은 list로 구현되므로)에 insert*/
  };
```

frame table entry로는 kernel page (frame_addr), user page (page_addr), t (해당 fte 를 소유하는 thread), 그리고 list_elem (frame table 에 연결될 list) 로 구현하였다. 그리고 ./vm/frame.c 파일에서  frame table 과 관련된 변수와 함수를 선언하였다. 

```
static struct list frameTable; /* 프레임 테이블, 실제 fte 소유 주체 */
static struct lock fTableLock;  /* frame table에 대한 atomic access를 구현 */
```

먼저, 실제로 fte 들을 소유하는 frameTable 을 list 형태로 선언하였다. 그리고 frameTable이 작동할 때, atomic 할 수 있도록 fTableLock 이라는 lock 을 선언하였다. 

```
// ./vm/frame.c
void
init_Lock_and_Table ()
{
  list_init (&frameTable);
  lock_init (&fTableLock);
}

// ./thread/init.c
int
main (void)
{
  ...
  init_Lock_and_Table();
  ...
}
```

다음으로, frame table 을 initiation 해주는 함수를 구현하였다. list 데이터 타입으로 frame_table 을 선언하였으니 list_init 함수를 통해 frame_table 을 초기화해주었다. 그리고 frame_lock 이라는 lock 도 lock_init 함수를 통해초기화해주었다. 

위에서 설명한 frame_init 함수는 thread/init.c 의 main 함수에서 pintos 가 시작할 때 초기화된다. 

```
// ./userprog/process.c
static bool
setup_stack (void **esp) 
{
  uint8_t *frame_addr;
  bool success = false;

  //frame_addr = palloc_get_page (PAL_USER | PAL_ZERO);
  frame_addr = falloc_get_page(PAL_USER | PAL_ZERO, PHYS_BASE - PGSIZE);
  if (frame_addr != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, frame_addr, true);
      if (success){
        init_frame_spte(&thread_current()->spt, PHYS_BASE - PGSIZE, frame_addr);
        *esp = PHYS_BASE;
      }
      else{
        //palloc_free_page (frame_addr); // Old
        falloc_free_page(frame_addr); // New
      }
    }
  return success;
}
```

위의 setup_stack 함수에서 기존에는 palloc 을 사용해서 kernel virtual page 를 생성했다면, 이제는 falloc 을 사용해서 할당 및 해제해주었다. falloc 관련 함수는 아래에서 설명할 예정이다. 

(질문) 왜 이렇게 수정했는지 이유 추가 필요해보임 (왜 palloc->falloc 하는지?) 

```
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
    evict_page(); 
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
```

먼저, falloc 은 palloc 을 사용해 upage 에 translate 될 kpage 를 할당받는다. 그리고 frame table entry 를 malloc 을 사용해 할당한 뒤, 위에서 설명한 fte 의 각 element 를 할당하고, frameTable 에 list_push_back 을 이용해 추가해준다. 이때, frameTable 에 여러 thread 가 접근하는 것을 막기 위해  fTableLock 을 사용하여 atomic 하게 해당 과정이 이루어 질 수 있도록 하였다. 그리고 만약 palloc 이 실패한다면 lock release 를 해준 뒤 null 을 반환하도록 하였다. 성공한다면 새롭게 palloc 을 통해 할당된 kpage 를 반환해준다. 

```
void
falloc_free_page (void *frame_addr)
{
  struct fte *e;
  lock_acquire (&fTableLock); // Make ATOMIC
  e = getFte (frame_addr); // free할 PM의 fte를 찾는다. 
  if (e == NULL) // 그런게 없으면, 
    sys_exit (-1); // 시스엑싯
  do_free_frame(e);

  lock_release (&fTableLock);
}

void do_free_frame(struct fte *targetFTE)
{
    list_remove(&targetFTE -> list_elem);
    palloc_free_page(targetFTE -> frame_addr);
    pagedir_clear_page(targetFTE -> t -> pagedir, targetFTE -> page_addr);
    free(targetFTE);
}
```

위의 setup_stack 함수에서 만약 install_page 가 실패하면 falloc 해준 kpage 를 free 해주어야한다. 위의 함수를 보면 먼저, kpage 를 갖는 frame table entry 를 찾아와 (getFte 함수 호출), 먼저, list_elem 에서 remove 해준다. 그리고, palloc_free_page 함수를 사용해 해당 kpage 를 free 해주고, 마지막으로, pagedir_clear_page 함수를 통해 kpage -> upage 접근을 막도록 하였다. 마지막으로 fte 를 free 해주었다. 이때, frameTable 에 대한 atomic 접근을 보장하기 위해 fTableLock 을 사용하였다. 

```
struct fte *
getFte (void* frame_addr)
{
  struct list_elem *e;
  for (e = list_begin (&frameTable); e != list_end (&frameTable); e = list_next (e))
    if (list_entry (e, struct fte, list_elem)->frame_addr == frame_addr)
      return list_entry (e, struct fte, list_elem);
  return NULL;
}
```
falloc_free_page 에서 사용한 get_fte 는 kpage 에 대응되는 frame table entry 를 반환하는 함수이다. for loop 를 이용해 frameTable 의 entry 를 하나씩 확인하며 대응되는 kpage 를 찾으면 해당 list_entry 를 반환하고, 만약 대응되는 kpage 가 없으면 NULL 을 반환한다.  

### Difference from design report

디자인 리포트에서 작성한 pseudocode 를 바탕으로 작성하였다.  

### 2. Supplemental page table 

lazy loading에 spte 가 사용되어서, supplemental page table 을 먼저 설명할 예정이다. 

### Implementation & Improvement from the previous design 

```
// ./vm/page.h
struct spte
  {
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
```

먼저, supplemental page table 은 hash table 을 이용해 구현하는 것이 권장되어서 위와 같이 선언해주었다. 위의 구조체는 supplemental page table entry 이며, 순서대로, physical page, virtual page, hash element, status (PAGE_FRAME, PAGE_ZERO, PAGE_SWAP, 또는 PAGE_FILE)이 element 로 있다. 그리고 페이지에 파일이 연결되어있을 경우를 위해, file pointer, offset, 읽어야 하는 바이트 수, 0으로 설정되어야 할 바이트 수, 그리고 페이지에 write 이 가능한지 여부를 저장해준다. 

```
// ./threads/thread.h
struct thread
  {
   ...
    struct hash spt;
  };

// ./threads/thread.c
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  ...
  init_spt(&t->spt);
  ...
}
```

그리고 thread 구조체에 hash 타입으로 supplement page table 을 추가해주었다. 그리고 thread_create 함수에서 supplemental page table 을 초기화해주었다. init_spt 함수는 아래의 ./vm/page.c 파일을 설명하면서 더 자세히 설명할 예정이다. 

```
static hash_hash_func spt_hash_func;
static hash_less_func comp_spt_va;
```

다음으로, ./vm/page.c 에서 supplemental page table 에 관한 함수와 변수들을 선언해주었다. 먼저, hash init 함수를 이용해 supplemental page table 을 초기화해주어야 하는데, 이때, hash_hash_func 과 hash_less_func 가 필요하여 먼저 선언해주었다. 

```
static unsigned
spt_hash_func (const struct hash_elem *elem, void *aux)
{
  struct spte *p = hash_entry(elem, struct spte, hash_elem);

  return hash_bytes (&p->page_addr, sizeof (p->frame_addr));
}
```

spt_hash_func 함수는 인자로 받아오는 hash_elem 에 해당하는 hasg entry 를 가져와 이 값을 기반으로 hash 값을 생성해주는 함수이다.  

```
static bool 
comp_spt_va (const struct hash_elem *e1, const struct hash_elem *e2, void *aux)
{

  return hash_entry (e1, struct spte, hash_elem)->page_addr < hash_entry (e2, struct spte, hash_elem)->page_addr;
}
```

comp_spt_va 함수는 hash table entry 를 비교하는 boolean 함수이다. 

```
void
init_spt (struct hash *spt)
{
  hash_init (spt, spt_hash_func, comp_spt_va, NULL);
}
```

위의 두 함수를 사용해 hash_init 함수를 호출하여 init_spt 함수를 구현하였다. 

```
static void spte_free (struct hash_elem *elem, void *aux);

static void
spte_free (struct hash_elem *e, void *aux)
{
  free(hash_entry (e, struct spte, hash_elem));
}
```

그리고 spt 를 delete 하는 함수가 필요하다. hash_destroy 함수 호출을 통해 구현하여야 하는데, 이때 spte_free 에 해당하는 함수가 필요하여 추가적으로 구현하였다. 위에 보이는 spte_free 함수는 인자로 넘겨준 elem 에 해당하는 hash entry 를 가져와 free 함수를 통해 해제시켜준다. 

```
void
destroy_spt (struct hash *spt)
{
  hash_destroy (spt, spte_free);
}
```

위에서 설명한 spte_free 를 인자로 넘겨 hash_destroy 함수를 통해 supplemental page table 을 제거하는 함수를 구현하였다. 이렇게 supplemental page table 에 해당 구현을 하였고 아래의 함수들은 supplemental page table entry 와 관련된 함수들이다.

```
#define PAGE_ZERO 0
#define PAGE_FRAME 1
#define PAGE_FILE 2
#define PAGE_SWAP 3
```

먼저 매크로로 위의 네가지 상태를 정의하였다. PAGE ZERO 는 아직 페이지가 할당되지 않은 상태를 말한다. PAGE FRAME은 페이지가 physical memory 에 매핑된 상태를 말한다. PAGE_FILE 은 페이지가 파일 시스템에 저장되어 필요 시 파일 시스템에서 읽어와야할 때를 말한다. 이 경우는 아래에서 설명할 LAZY LOADING 에서 사용될 예정이다. 마지막으로 PAGE_SWAP 은 페이지가 swap 공간에 저장되어있는 상태를 말하며 아래의 swap table 설명에서 더 자세히 다룰 예정이다.

각 매크로에 맞게 spte initiation 함수를 작성하였다. 위의 매크로 순서대로 설명할 예정이다. 

```
void
init_zero_spte (struct hash *spt, void *page_addr)
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
```

먼저 PAGE_ZERO 의 경우 데이터들을 0으로 채워야한다. 즉, 아직 kpage 매핑이 되지 않은 상태이기 때문에kpage 를 null 로 할당해준다. 그리고, upage 는 인자로 받아온 upage 로 할당해주고 file 과 writable 도 각각 null, true 로 할당해준다. 마지막으로 supplemental page table 에 hash_insert 함수를 사용해 추가해준다. 

```
void
init_frame_spte (struct hash *spt, void *page_addr, void *frame_addr)
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
```

다음으로, PAGE_FRAME 의 경우, 위와 비슷하지만 kpage 를 인자로 받아와 할당해주는 과정을 추가하였다. 

```
struct spte *
init_file_spte (struct hash *spt, void *_page_addr, struct file *_file, off_t _ofs, uint32_t _read_bytes, uint32_t _zero_bytes, bool _isWritable)
{
  struct spte *e;
  
  e = (struct spte *)malloc (sizeof *e);

  e->page_addr = _page_addr;
  e->frame_addr = NULL;
  
  e->file = _file;
  e->ofs = _ofs;
  e->read_bytes = _read_bytes;
  e->zero_bytes = _zero_bytes;
  e->isWritable = _isWritable;
  
  e->status = PAGE_FILE;
  
  hash_insert (spt, &e->hash_elem);
  
  return e;
}
```

PAGE_FILE 의 경우, 파일을 참조할 때 필요한 file, offset, bytes to read, bytes to set zero, writable 에 해당하는 값들을 인자로 받아와 할당해준다. PAGE_ZERO 와 유사하게 kpage 에 매핑은 되지 않기 때문에 null 로 설정해주었다. 

```
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
```

먼저, init_spte 함수의 경우, spte 구조체를 malloc 을 사용해 새롭게 할당한 후, 인자로 받아온 kpage 와 upage 를 정의해준다. 그리고 supplemental page table 에 해당entry 를 hash_insert 를 이용해 추가해준다. 이때, status 는 PAGE_FRAME 으로 설정해준다. 

(질문) 이 함수 사용하는지??? init_spte

PAGE_SWAP 의 경우 아래의 swap 과정에서 다시 설명하도록 하겠다. 다음으로는 supplemental page table entry 를 삭제하는 과정이다. 

```
void 
delete_and_free (struct hash *spt, struct spte *spte)
{
  hash_delete (spt, &spte->hash_elem);
  free (spte);
}
```

위의 delete_and_free 함수를 보면 entry 에 해당하는 hash entry 를 spt (supplemental page table) 에서 hash_delete 함수 호출을 통해 삭제 해 준다. 그리고 해당 entry 를 free 해주었다. 

이렇게 supplemental page table 과 그 entry 와 관련된 함수는 모두 구현하였다. 

```
// ./userprog/process.c
static bool
setup_stack (void **esp) 
{
  ...
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, frame_addr, true);
      if (success){
        init_frame_spte(&thread_current()->spt, PHYS_BASE - PGSIZE, frame_addr);
        *esp = PHYS_BASE;
      }
      ...
```

setup_stack 함수에서 만약 install page 가 성공하면, init_frame_spte 함수를 실행하여 kpage 를 supplemental page table 에 등록해준다. 


### Difference from design report

디자인 레포트에서는 4 가지 status 에 대해서 생각하지 못하여 initiation 과정을 하나만 구상하였는데, 네 가지 다른 status 각각에 맞게 initiation 과정을 추가하였다. 그리고 hash 내부의 함수 사용이 미흡하여 supplemental page table 을 init 하고 delete 하는 함수 구현을 구상하지 못하였는데 supplemental page table entry 에 해당하는 함수를 구현하면서 추가해주었다. 

### 2. Lazy loading 

### Implementation & Improvement from the previous design 

```
// ./userprog/process.c
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ...
  init_file_spte (&thread_current()->spt, page_addr, file, ofs, page_read_bytes, page_zero_bytes, writable);
  ...
}
```

기존의 load_segment 의 경우 file 을 바로 memory 에 추가하였다면 이제는 lazy loading 과정을 구현해야하기 때문에 위와 같이 init_file_spte 함수를 실행하여 page fault 가 발생한다면 해당 supplemental page table entry element 값 (즉, file) 을 통해 페이지를 저장할 수 있도록 수정하였다.  

```
static void
page_fault (struct intr_frame *f) 
{
  ...
  page_addr = pg_round_down (fault_addr);

  if (is_kernel_vaddr (fault_addr) || !not_present) {sys_exit (-1);}
   
  spt = &thread_current()->spt;

  spe = get_spte(spt, page_addr);

  if(user == true){
      esp = f -> esp;
  }else{
      thread_current()->esp;
  }

  if (load_page (spt, page_addr)) {
     return;
  }
  ...
}
```

그리고 page fault handler 에서 load_page 라는 함수를 통해 page fault 가 발생하였을 때, lazy loading 이 실행될 수 있도록 하였다. load_page 과정은 아래와 같다. 

```
extern struct lock FileLock;

bool
load_page (struct hash *spt, void *page_addr)
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
```

page fault 가 났기 때문에 kpage (kernel page) 를 falloc 을 통해 새롭게 할당해준다. 그리고 prepare_mem_page 함수에서 switch case 를 사용하여 각 상황 (PAGE_ZERO, PAGE_SWAP, 그리고 PAGE_FILE) 각각에 대해서 처리해준다. 먼저, PAGE_ZERO 의 경우 memset 함수를 통해 해당 메모리를 0으로 초기화해준다. 그리고, PAGE_SWAP 의 경우 아래에서 설명할 swap table 과정을 통해 구현하여 아래에서 설명할 예정이다. 마지막으로 PAGE_FILE의 경우, file_read_at 함수를 통해 파일에서 데이터를 읽어와서 추가하고 memset 을 통해 나머지 영역을 0으로 초기화해주는 함수를 추가해주었다. 이때, 여러 process 에서 파일에 접근하는 것을 막기 위해서 FileLock 을 사용해 atomic 하게 구현하였다. 마지막으로, 새롭게 가져온 데이터를 기반으로 page directory 를 설정하고, supplemental page table entry 도 업데이트 해주었다.  

```
struct spte *
get_spte (struct hash *spt, void *page_addr)
{
  struct spte e;
  struct hash_elem *elem;

  e.page_addr = page_addr;
  elem = hash_find (spt, &e.hash_elem);

  return elem != NULL ? hash_entry (elem, struct spte, hash_elem) : NULL;
}
```

위에서 supplemental page table entry 를 가져오기 위해서 위에 보이는 get_spte 함수를 사용하였다. get_spte 함수는 upage 를 인자로 받아와 이에 해당하는 hash table entry 를 반환해주는 역할을 한다.  


### Difference from design report

기존에는 supplemental page table 구조체를 구현하기 전에 lazy loading 을 구현하는 방법에 대해 구상하였어서 page_table 이라는 구조체를 따로 만들었으나 supplemental page table 을 이용해 구현하는 방식으로 수정하였다. 그리고, 디자인 레포트에서는 PAGE_FILE 과 PAGE_SWAP 의 경우만 구상하였는데 구현하면서 PAGE_ZERO 에 해당하는 구현을 추가해주었다.   


### 4. Stack growth

### Implementation & Improvement from the previous design 

```
struct thread
  {
  ...
    void *esp;
  ...
  };
```

stack growth 를 위해서는 현재 가리키고 있는 stack pointer 위치를 알아야하기 때문에 thread 구조체에 esp 포인터를 추가해주었다. 
(질문) 이거 새로 추가한거 맞나??

```
static void
page_fault (struct intr_frame *f) 
{
  ...
  page_addr = pg_round_down (fault_addr);

  if (is_kernel_vaddr (fault_addr) || !not_present) {sys_exit (-1);}
   
  spt = &thread_current()->spt;

  spe = get_spte(spt, page_addr);

  if(user == true){
      esp = f -> esp;
  }else{
      thread_current()->esp;
  }

  bool isValidExtend = esp - STACK_BUFFER <= fault_addr && STACK_LIMIT <= fault_addr;
  if (isValidExtend) {
    init_zero_spte(spt, page_addr);
  }
  ...
}
```

stack growth 는 page fault 가 발생했을 때 실행된다. 먼저 pg_round_down 함수를 사용해 페이지 크기의 배수로 내림하여 해당 주소가 속한 페이지의 시작 주소를 upage 에 할당해준다. 그리고 esp 확장 가능한지 확인하기 위해서 if 문을 사용해 컨디션을 확인한 수, init_zero_spte 를 사용해 새로운 supplemental page table entry 를 생성한 후 supplemental page table에 추가해 주었다. 


### Difference from design report

기존의 디자인과 두가지 차별점이 생겼다. 먼저, stack grow 가 가능한지 확인하는 함수를 따로 구현하려고 하였으나 if 문을 사용해 간단히 확인이 가능할 것 같아 page fault 함수에서 실행하였다. 그리고 stack memory 를 확장해주는 함수는 supplemental page table 을 구현하는 과정에서 데이터를 zero 로 할당해주는 함수인 init_zero_spte를 사용하면 더 일관성있게 구현할 수 있을 것 같아 수정하였다. 


### 5. File memory mapping

### Implementation & Improvement from the previous design

```
// ./threads/thread.h
struct mmf 
{
   int id;                          /* 이름 */
   struct file* file;               /* 무슨 파일에 해당하는지? */
   struct list_elem mmf_list_elem;  /* 마스터 스레드가 갖는 mmf리스트에 끼울 elem */
   void *page_addr;                 /* mapping된 VA */
};
```

memory mapped file 구조체를 새롭게 정의하였다. 해당 구조체 안에는 memory mapped file 의 unique id, 해당 구조체가 가리키는 file, 각 thread 의 mmf 들을 list 형태로 저장한 mmf_list_elem, 그리고 mmf 에 대응되는 user page 를 갖고 있다. 

```
struct thread
  {
   ...
    struct list mmf_list; // 슬레이브 mmf 리스트
    int t_mmf_id;            // 스레드 mmf id
  };
```

다음으로, 위에서 정의한 mmf 구조체를 각 thread 의 element 로 정의해주었다. mapid 는 현재 이 thread 가 갖고 있는 mmf 의 수, 즉, mmf_list_elem 의 사이즈를 갖고 있다. 그리고 mmf_list 를 list 형태로 mmf 를 저장한다.  

```
// ./threads/thread.c
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  ...
  thread_mmf_init(t);
  ...
}

void thread_mmf_init(struct thread *t)
{
  list_init (&t->mmf_list);
  t->t_mmf_id = 0;   
}
```

이렇게 생성한 mmf를 초기화해주기 위해서 thread_create 함수에서 list_init 함수를 통해 mmf_list 리스트를 초기화하고 mapid 를 0으로 초기화해주었다. 

```
// ./threads/thread.c
struct mmf *init_mmf (int id, struct file *file, void *page_addr)
{
  /* mmf 동적 할당으로 공간 확보 */
  struct mmf *mmf = (struct mmf *) malloc (sizeof *mmf);
  
  /* mmf 내용 채우기 */
  mmf->id = id;
  mmf->file = file;
  mmf->page_addr = page_addr;


  uint64_t ofs;
  int size = file_length (file);
  struct hash *spt = &thread_current ()->spt;

  for (ofs = 0; ofs < size; ofs += PGSIZE)
    if (get_spte (spt, page_addr + ofs))
      return NULL;

  for (ofs = 0; ofs < size; ofs += PGSIZE)
  {
    uint32_t read_bytes = ofs + PGSIZE < size ? PGSIZE : size - ofs;
    init_file_spte (spt, page_addr, file, ofs, read_bytes, PGSIZE - read_bytes, true);
    page_addr += PGSIZE;
  }

  list_push_back (&thread_current ()->mmf_list, &mmf->mmf_list_elem);

  return mmf;
}
```

다음으로, 앞서 설명한 mmf 구조체를 초기화해주기 위해 init_mmf 함수를 추가해주었다. 먼저, mmf 구조를 malloc 을 사용해 동적 할당해준 다음 init_mmf 함수의 인자로 받아온 id, file, upage 를 각각 매핑해주었다. 

(질문) for loop 두개 설명 추가 필요 

```
// ./threads/thread.c
struct mmf *
get_mmf (int t_mmf_id)
{
  struct list *list = &thread_current ()->mmf_list;
  struct list_elem *e;

  for (e = list_begin (list); e != list_end (list); e = list_next (e))
  {
    struct mmf *f = list_entry (e, struct mmf, mmf_list_elem);

    if (f->id == t_mmf_id)
      return f;
  }

  return NULL;
}
```

(질문) 이거 어디서 씀?? 

```
// ./userprog/syscall.c

static void
syscall_handler(struct intr_frame *f)
{
  if(verify_mem_address(f->esp)){
    int argv[3];
    switch (*(uint32_t *)(f->esp))
    {
      ...
      case SYS_MMAP:
        getArgs(f->esp + 4, &argv[0], 2);
        f->eax = sys_mmap((int) argv[0], (void *) argv[1]);
        break;
      case SYS_MUNMAP:
        getArgs(f->esp + 4, &argv[0], 1);
        sys_munmap((int) argv[0]);
        break;
  ...
}
```

이런 file memory mapping 은 syscall_handler 에서 실행된다. 각각 mapping 과정과 unmapping 과정을 syscall_handler switch case 에 추가하였다. 각각 과정에서 사용된 sys_mmap 함수와 sys_munmap 과정은 아래에서 설명할 예정이다. 

```
// ./userprog/syscall.c

int
sys_mmap(int fd, void *addr) {
    struct thread *t = thread_current();
    //struct file *f = t->pcb->fd_table[fd];
    struct file *f = t->fileTable[fd];
    struct file *opened_f;
    struct mmf *mmf;

    if (f == NULL)
        return -1;

    if (addr == NULL || (int) addr % PGSIZE != 0)
        return -1;

    lock_acquire(&FileLock);

    opened_f = file_reopen(f);
    if (opened_f == NULL) {
        lock_release(&FileLock);
        return -1;
    }

    mmf = init_mmf(t->t_mmf_id++, opened_f, addr);
    if (mmf == NULL) {
        lock_release(&FileLock);
        return -1;
    }

    lock_release(&FileLock);

    return mmf->id;
}
```

sys_mmap 함수는 fd (file descriptor) 와 addr(address) 를 인자로 받아오며, 파일을 메모리에 매핑하는 역할을 한다. file 에 접근하기 때문에 file_lock 을 사용해 atomic 하게 진행하였고, file_reopen 함수를 이용해 복사본으로 file 을 열었다. 그리고 이렇게 복사 후 오픈 된 파일을 바탕으로 init_mmf 함수를 사용해 mmf 구조체를 초기화하고 해당 file 과 addr 를 매핑해주었다. 그리고 thread 객체의 mapid 도 1 증가시켜주었다. 

```
// ./userprog/syscall.c

int
sys_munmap(int t_mmf_id) {
    struct thread *t = thread_current();
    struct list_elem *e;
    struct mmf *mmf;
    void *page_addr;

    if (t_mmf_id >= t->t_mmf_id)
        return;

    for (e = list_begin(&t->mmf_list); e != list_end(&t->mmf_list); e = list_next(e)) {
        mmf = list_entry(e, struct mmf, mmf_list_elem);
        if (mmf->id == t_mmf_id)
            break;
    }
    if (e == list_end(&t->mmf_list))
        return;

    page_addr = mmf->page_addr;

    lock_acquire(&FileLock);

    off_t ofs;
    for (ofs = 0; ofs < file_length(mmf->file); ofs += PGSIZE) {
        struct spte *entry = get_spte(&t->spt, page_addr);
        if (pagedir_is_dirty(t->pagedir, page_addr)) {
            void *frame_addr = pagedir_get_page(t->pagedir, page_addr);
            file_write_at(entry->file, frame_addr, entry->read_bytes, entry->ofs);
        }
        delete_and_free(&t->spt, entry);
        page_addr += PGSIZE;
    }
    list_remove(e);

    lock_release(&FileLock);
}
```

먼저, for loop 를 통해 인자로 받아온 mapid 에 해당하는 mmf 를 찾고 get_spte 함수를 사용해 현재 페이지인 upage 에 해당하는 entry 를 받아온다. 그리고 pagedir_is_dirty 함수를 이용해 페이지가 수정되었는지 여부를 확인 후, 만약 수정되었다면, file_write_at 함수를 이용해 수정사항을 파일에도 기록해준다. 그리고 unmapping 해주기 위해서 page_delete 함수를 사용해 supplemental page table 에서 해당 entry 를 제거해준다. 그리고 현재 mmf 역시 list_remove 함수를 사용해 list 에서 제거해준다. 이 과정역시 file 에 접근하기 때문에 file_lock 을 사용해 atomic 하게 진행하도록 하였다. 

### Difference from design report

#### 디자인 레포트 내용 (참고)

기존의 디자인 레포트에서는 mmf 에 upage 에 대한 내용을 저장하지 않았다. 하지만 해당 파일이 어떤 physical memory 에 매핑되는지를 저장하기 위해서 upage 라는 element 를 추가해주었다. 그리고 syscall d을 통해 mapping 과 unmapping 하는 것을 구상하였는데 이에 추가하여 mmf 구조체를 초기화해주는 함수를 추가하였다. 

### 6. Swap table

### Implementation & Improvement from the previous design 

```
// ./vm/swap.c

static struct bitmap *swap_valid_table;
static struct block *swap_disk;
```

swap_valid_table 을 bitmap 형식으로 선언하여 swap 할 공간을 효율적으로 찾을 수 있도록 하였다. 0 이면 swap in 가능, 1 이면 swap out 이 가능하도록 아래에서 구현하였다. 그리고 해당 block 에 read/write 과정이 필요하기 때문에 block 포인터로 swap_disk 를 추가하였다. 

```
// ./vm/swap.c

#define SECTOR_NUM (PGSIZE / BLOCK_SECTOR_SIZE)

void init_swap_valid_table()
{
    swap_disk = block_get_role(BLOCK_SWAP);
    swap_valid_table = bitmap_create(block_size(swap_disk) / SECTOR_NUM);

    bitmap_set_all(swap_valid_table, true);
    lock_init(&swapLock);
}
```

그리고 위의 bitmap 을 활성화 시키기 위해서 init_swap_valid_table 함수를 추가하였다. 먼저 block_get_role 함수 호출을 통해서 여러 블록 중 swap 될 블락 하나를 swap_block 변수에 저장해주었다. 그리고 bitmap_create 함수를 통해 swap_valid_table 를 활성화시켜주었다. 이때 bitmap 의 크기는 swap_table 의 크기를 SECTOR_NUM 으로 나눈 값을 갖도록 하였다. 그리고 bitmap_set_all 함수를 호출하여 모두 true 값, 즉, swap out 이 가능한 상태로 설정해주었다. 

```
// ./thread/init.c 
int
main (void)
{
  init_swap_valid_table() 
}
```

그리고 main 함수에서 init_swap_valid_table 함수를 호출하여 초기화해주었다. 

```
// ./vm/swap.c

static struct lock swap_lock;

void init_swap_valid_table()
{
    ...
    lock_init(&swapLock);
}
```

다음으로 swapping in 과 out 을 구현하였는데 해당 과정을 위해 swap_lock 을 선언하여 atomic 하게 진행할 수 있도록 하였다. 

```
// ./vm/swap.c

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
        block_read(swap_disk, id * SECTOR_NUM + i, kva + (i * BLOCK_SECTOR_SIZE));
    }
}
```

먼저, swap in 함수 에서는 먼저 swap_lock 을 통해 atomic 하게 접근하고, id 가 bitmap 내에서 맞는 범위에 해당하는지를 확인해준다. 그리고 bitmap_test 함수를 사용해 ID 가 참조하는 swap slot 이 사용중인지를 확인해준다. 이때, true 면 swap slot 이 비어있다는 것을 뜻한다. 그리고 bitmap_set 함수를 사용해 해당 swap slot 을 true 로 설정하여 사용중임을 나타내도록 한다. 이 과정이 끝나면 lock 을 다시 release 해준다. 

```
bool
load_page (struct hash *spt, void *upage)
{
  ...
  switch (e->status)
  {
  case PAGE_ZERO:
    memset (kpage, 0, PGSIZE);
    break;

  case PAGE_SWAP:
    swap_in(e, kpage);  
    break;
  ...
}
```

그리고 앞서 설명한 load_page 함수에서 PAGE_SWAP 일 경우에 swap_in 함수를 통해 swapping 을 구현하였다. 

```
// ./vm/swap.c

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
        block_write(swap_disk, id * SECTOR_NUM + i, kva + (BLOCK_SECTOR_SIZE * i));
    }

    return id;
}
```

다음으로는 swap_out 함수이다. 위와 비슷하게 bitmap_scan_and_flip 함수를 사용해 비어있는 상태의 swap slot 을 찾은 뒤 false (사용중)으로 수정해준다. 이 과정은 atomic 하게 진행된다. 그리고 해당 swap slot 에 맞는 swap block에 block_write 함수를 사용하여 데이터를 저장해준다. 이 과정은 for loop 를 이용해 구현하였다. 

```
./vm/frame.c

static struct fte *clock_cursor; /* fte에서 어떤 frame을 evict해야하나? (가르키는 대상이 fte이므로 type도 fte)*/

void
frame_init ()
{
  ...
  clock_cursor = NULL;
}

void *
falloc_get_page(enum palloc_flags flags, void *upage)
{
  ...
  if (frame_addr == NULL)
  {
    evict_page(); 
    frame_addr = palloc_get_page (flags);
    if (frame_addr == NULL)
      return NULL; // 그래도 안된다? -> NULL..
  }
  ...
}
```

그리고 clock_cursor 라는 frame table entry 를 정의하였는데 이는 free 상태인 frame 이 없을 경우, evict 할 frame 을 찾기 위해 정의하였다. 위의 falloc_get_page 함수에서 evict_page() 함수를 호출하게 되는데, 이때 이 entry 가 사용된다. 


```
void evict_page() {
  ASSERT(lock_held_by_current_thread(&fTableLock));

  struct fte *e = clock_cursor;
  struct spte *s;

  /* BEGIN: Find page to evict */
  do {
    if (e != NULL) {
      pagedir_set_accessed(e->t->pagedir, e->page_addr, false);
    }

    if (clock_cursor == NULL || list_next(&clock_cursor->list_elem) == list_end(&frameTable)) {
      e = list_entry(list_begin(&frameTable), struct fte, list_elem);
    } else {
      e = list_next (e);
    }
  } while (!pagedir_is_accessed(e->t->pagedir, e->page_addr));
  /*  END : Find page to evict */

  s = get_spte(&thread_current()->spt, e->page_addr);
  s->status = PAGE_SWAP;
  s->swap_id = swap_out(e->frame_addr);

  lock_release(&fTableLock); {
    falloc_free_page(e->frame_addr);
  } lock_acquire(&fTableLock);
}
```

`evict_page` 함수는 물리 메모리가 부족할 때 사용되지 않는 페이지를 선택하여 스왑 영역으로 내보내는 역할을 수행한다. 시계 알고리즘(Clock Algorithm)을 사용하여 교체 대상 페이지를 찾도록 하였다.

먼저, 함수는 `frame_lock`을 호출한 스레드가 보유하고 있는지 확인하고, 현재 프레임 테이블 항목을 가리키는 `clock_cursor`를 기준으로 탐색을 시작한다. `pagedir_set_accessed`를 호출하여 현재 페이지의 `accessed` 비트를 `false`로 초기화하고, 시계 알고리즘을 통해 최근에 접근되지 않은 페이지를 찾는다. 탐색 중 프레임 테이블의 끝에 도달하면 다시 처음부터 탐색을 이어간다. 조건을 만족하는 페이지를 찾을 때까지 `pagedir_is_accessed`를 사용하여 확인한다.

교체 대상 페이지가 결정되면, 해당 페이지의 페이지 테이블 엔트리(`spte`)를 가져와 상태를 `PAGE_SWAP`으로 설정하고, `swap_out`을 호출하여 페이지 내용을 스왑 영역에 저장한다. 이때 반환된 `swap_id`는 페이지의 저장 위치를 추적하기 위함이다.

페이지 내용이 스왑 영역으로 저장된 후, 락을 해제(`lock_release`)하고 `falloc_free_page`를 호출하여 physical 메모리를 해제한다. 

### Difference from design report

디자인 레포트와 비슷한 형식으로 구현하였다.   

### 7. On process termination

### Implementation & Improvement from the previous design 

```
./userprog/process.c
void
process_exit (void)
{
 ...
  for (i = 0; i < cur->mapid; i++){
    sys_munmap (i);
  }
  destroy_spt (&cur->spt); 
  ...
}
```

먼저, process 가 exit 할 때, 모든 mmf 들을 닫아주어야하기 때문에 모든 mapid 에 대해 sys_munmap 함수를 이용해 unmapping 해준다. 그리고 위에서 설명한 destroy_spt 함수를 통해 supplemental page table 함수도 삭제해준다. 이 함수는 위에서 자세히 설명되어있다. 

### Difference from design report

디자인 레포트와 달라진 점?? 

### Overall Limitations 

fail 뜬거 왜그런건지 설명 

### Overall Discussion 


# Result 
