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
    void *kpage;   
    void *upage;  
    struct thread *t; 
    struct list_elem list_elem; 
  };
```

frame table entry로는 kernel page (kpage), user page (upage), t (해당 fte 를 소유하는 thread), 그리고 list_elem (frame table 에 연결될 list) 로 구현하였다. 그리고 ./vm/frame.c 파일에서  frame table 과 관련된 변수와 함수를 선언하였다. 

```
static struct list frame_table; 
static struct lock frame_lock; 
```

먼저, 실제로 fte 들을 소유하는 frame_table 을 list 형태로 선언하였다. 그리고 frame table 이 작동할 때, atomic 할 수 있도록 frame_lock 이라는 lock 을 선언하였다. 

```
// ./vm/frame.c
void
frame_init ()
{
  list_init (&frame_table);
  lock_init (&frame_lock);
}

// ./thread/init.c
int
main (void)
{
  ...
  frame_init();
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
  ...
  kpage = falloc_get_page(PAL_USER | PAL_ZERO, PHYS_BASE - PGSIZE);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success){
        *esp = PHYS_BASE;
      }
      else{
        falloc_free_page(kpage);
      }
    }
   ...
}
```

위의 setup_stack 함수에서 기존에는 palloc 을 사용해서 kernel virtual page 를 생성했다면, 이제는 falloc 을 사용해서 할당 및 해제해주었다. falloc 관련 함수는 아래에서 설명할 예정이다. 

```
void *
falloc_get_page(enum palloc_flags flags, void *upage)
{
  struct fte *e;
  void *kpage;

  lock_acquire (&frame_lock);

  kpage = palloc_get_page (flags);
  if (kpage == NULL)
  {
    lock_release (&frame_lock);
    return NULL;
  }

  e = (struct fte *)malloc (sizeof *e); 
  e->kpage = kpage; 
  e->upage = upage; 
  e->t = thread_current (); 
  list_push_back (&frame_table, &e->list_elem);

  lock_release (&frame_lock); 
  return kpage;
}
```

먼저, falloc 은 palloc 을 사용해 upage 에 translate 될 kpage 를 할당받는다. 그리고 frame table entry 를 malloc 을 사용해 할당한 뒤, 위에서 설명한 fte 의 각 element 를 할당하고, frame_table 에 list_push_back 을 이용해 추가해준다. 이때, frame_table 에 여러 thread 가 접근하는 것을 막기 위해  frame_lock 을 사용하여 atomic 하게 해당 과정이 이루어 질 수 있도록 하였다. 그리고 만약 palloc 이 실패한다면 lock release 를 해준 뒤 null 을 반환하도록 하였다. 성공한다면 새롭게 palloc 을 통해 할당된 kpage 를 반환해준다. 

```
void
falloc_free_page (void *kpage)
{
  struct fte *e;
  lock_acquire (&frame_lock);

  e = get_fte (kpage); 
  if (e == NULL)
    sys_exit (-1); 

  list_remove (&e->list_elem); 
  palloc_free_page (e->kpage); 
  pagedir_clear_page (e->t->pagedir, e->upage); 
  free (e);

  lock_release (&frame_lock);
}
```

위의 setup_stack 함수에서 만약 install_page 가 실패하면 falloc 해준 kpage 를 free 해주어야한다. 위의 함수를 보면 먼저, kpage 를 갖는 frame table entry 를 찾아와 (get_fte 함수 호출), 먼저, list_elem 에서 remove 해준다. 그리고, palloc_free_page 함수를 사용해 해당 kpage 를 free 해주고, 마지막으로, pagedir_clear_page 함수를 통해 kpage -> upage 접근을 막도록 하였다. 마지막으로 fte 를 free 해주었다. 이때, frame_table 에 대한 atomic 접근을 보장하기 위해 frame_lock 을 사용하였다. 

```
struct fte *
get_fte (void* kpage)
{
  struct list_elem *e;
  for (e = list_begin (&frame_table); e != list_end (&frame_table); e = list_next (e))
    if (list_entry (e, struct fte, list_elem)->kpage == kpage)
      return list_entry (e, struct fte, list_elem);
  return NULL;
}
```
falloc_free_page 에서 사용한 get_fte 는 kpage 에 대응되는 frame table entry 를 반환하는 함수이다. for loop 를 이용해 frame_table 의 entry 를 하나씩 확인하며 대응되는 kpage 를 찾으면 해당 list_entry 를 반환하고, 만약 대응되는 kpage 가 없으면 NULL 을 반환한다.  

### Difference from design report

디자인 리포트에서 작성한 pseudocode 를 바탕으로 작성하였다.  

### 2. Supplemental page table 

lazy loading에 spte 가 사용되어서, supplemental page table 을 먼저 설명할 예정이다. 

### Implementation & Improvement from the previous design 

```
// ./vm/page.h
struct spte
  {
    void *upage;
    void *kpage;
    struct hash_elem hash_elem;
    int status;
    struct file *file;  
    off_t ofs;  
    uint32_t read_bytes, zero_bytes;  
    bool writable; 
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
static hash_less_func spt_less_func;
```

다음으로, ./vm/page.c 에서 supplemental page table 에 관한 함수와 변수들을 선언해주었다. 먼저, hash init 함수를 이용해 supplemental page table 을 초기화해주어야 하는데, 이때, hash_hash_func 과 hash_less_func 가 필요하여 먼저 선언해주었다. 

```
static unsigned
spt_hash_func (const struct hash_elem *elem, void *aux)
{
  struct spte *p = hash_entry(elem, struct spte, hash_elem);

  return hash_bytes (&p->upage, sizeof (p->kpage));
}
```

spt_hash_func 함수는 인자로 받아오는 hash_elem 에 해당하는 hasg entry 를 가져와 이 값을 기반으로 hash 값을 생성해주는 함수이다.  

```
static bool 
spt_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
  void *a_upage = hash_entry (a, struct spte, hash_elem)->upage;
  void *b_upage = hash_entry (b, struct spte, hash_elem)->upage;

  return a_upage < b_upage;
}
```

spt_less_func 함수는 hash table entry 를 비교하는 boolean 함수이다. 

```
void
init_spt (struct hash *spt)
{
  hash_init (spt, spt_hash_func, spt_less_func, NULL);
}
```

위의 두 함수를 사용해 hash_init 함수를 호출하여 init_spt 함수를 구현하였다. 

```
static void page_destutcor (struct hash_elem *elem, void *aux);
static void
page_destutcor (struct hash_elem *elem, void *aux)
{
  struct spte *e;

  e = hash_entry (elem, struct spte, hash_elem);

  free(e);
}
```

그리고 spt 를 delete 하는 함수가 필요하다. hash_destroy 함수 호출을 통해 구현하여야 하는데, 이때 page_destructor 에 해당하는 함수가 필요하여 추가적으로 구현하였다. 위에 보이는 page_destructor 함수는 인자로 넘겨준 elem 에 해당하는 hash entry 를 가져와 free 함수를 통해 해제시켜준다. 

```
void
destroy_spt (struct hash *spt)
{
  hash_destroy (spt, page_destutcor);
}
```

위에서 설명한 page_destructor 를 인자로 넘겨 hash_destroy 함수를 통해 supplemental page table 을 제거하는 함수를 구현하였다. 이렇게 supplemental page table 에 해당 구현을 하였고 아래의 함수들은 supplemental page table entry 와 관련된 함수들이다.

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
init_zero_spte (struct hash *spt, void *upage)
{
  struct spte *e;
  e = (struct spte *) malloc (sizeof *e);
  
  e->upage = upage;
  e->kpage = NULL;
  
  e->status = PAGE_ZERO;
  
  e->file = NULL;
  e->writable = true;
  
  hash_insert (spt, &e->hash_elem);
}
```

먼저 PAGE_ZERO 의 경우 데이터들을 0으로 채워야한다. 즉, 아직 kpage 매핑이 되지 않은 상태이기 때문에kpage 를 null 로 할당해준다. 그리고, upage 는 인자로 받아온 upage 로 할당해주고 file 과 writable 도 각각 null, true 로 할당해준다. 마지막으로 supplemental page table 에 hash_insert 함수를 사용해 추가해준다. 

```
void
init_frame_spte (struct hash *spt, void *upage, void *kpage)
{
  struct spte *e;
  e = (struct spte *) malloc (sizeof *e);

  e->upage = upage;
  e->kpage = kpage;
  
  e->status = PAGE_FRAME;

  e->file = NULL;
  e->writable = true;
  
  hash_insert (spt, &e->hash_elem);
}
```

다음으로, PAGE_FRAME 의 경우, 위와 비슷하지만 kpage 를 인자로 받아와 할당해주는 과정을 추가하였다. 

```
struct spte *
init_file_spte (struct hash *spt, void *_upage, struct file *_file, off_t _ofs, uint32_t _read_bytes, uint32_t _zero_bytes, bool _writable)
{
  struct spte *e;
  
  e = (struct spte *)malloc (sizeof *e);

  e->upage = _upage;
  e->kpage = NULL;
  
  e->file = _file;
  e->ofs = _ofs;
  e->read_bytes = _read_bytes;
  e->zero_bytes = _zero_bytes;
  e->writable = _writable;
  
  e->status = PAGE_FILE;
  
  hash_insert (spt, &e->hash_elem);
  
  return e;
}
```

PAGE_FILE 의 경우, 파일을 참조할 때 필요한 file, offset, bytes to read, bytes to set zero, writable 에 해당하는 값들을 인자로 받아와 할당해준다. PAGE_ZERO 와 유사하게 kpage 에 매핑은 되지 않기 때문에 null 로 설정해주었다. 

```
void
init_spte (struct hash *spt, void *upage, void *kpage)
{
  struct spte *e;
  e = (struct spte *) malloc (sizeof *e);
  e->upage = upage;
  e->kpage = kpage;
  e->status = PAGE_FRAME;
  hash_insert (spt, &e->hash_elem);
}
```

먼저, init_spte 함수의 경우, spte 구조체를 malloc 을 사용해 새롭게 할당한 후, 인자로 받아온 kpage 와 upage 를 정의해준다. 그리고 supplemental page table 에 해당entry 를 hash_insert 를 이용해 추가해준다. 이때, status 는 PAGE_FRAME 으로 설정해준다. 

PAGE_SWAP 의 경우 아래의 swap 과정에서 다시 설명하도록 하겠다. 다음으로는 supplemental page table entry 를 삭제하는 과정이다. 

```
void 
page_delete (struct hash *spt, struct spte *entry)
{
  hash_delete (spt, &entry->hash_elem);
  free (entry);
}
```

위의 page_delete 함수를 보면 entry 에 해당하는 hash entry 를 spt (supplemental page table) 에서 hash_delete 함수 호출을 통해 삭제 해 준다. 그리고 해당 entry 를 free 해주었다. 

이렇게 supplemental page table 과 그 entry 와 관련된 함수는 모두 구현하였다. 

```
// ./userprog/process.c
static bool
setup_stack (void **esp) 
{
  ...
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success){
        init_frame_spte(&thread_current()->spt, PHYS_BASE - PGSIZE, kpage); // 이 부분 추가 
        *esp = PHYS_BASE;
      }
...
}
```

setup_stack 함수에서 만약 install page 가 성공하면, init_frame_spte 함수를 실행하여 kpage 를 supplemental page table 에 등록해준다. 


### Difference from design report

디자인 레포트에서는 4 가지 status 에 대해서 생각하지 못하여 initiation 과정을 하나만 구상하였는데, 네 가지 다른 status 각각에 맞게 initiation 과정을 추가하였다. 그리고 hash 내부의 함수 사용이 미흡하여 supplemental page table 을 init 하고 delete 하는 함수 구현을 구상하지 못하였는데 supplemental page table entry 에 해당하는 함수를 구현하면서 추가해주었다. 

(질문) 각 initiation 이랑 delete 함수가 어디서 사용되는지?? 


### 2. Lazy loading 

### Implementation & Improvement from the previous design 

```
// ./userprog/process.c
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ...
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      init_file_spte (&thread_current()->spt, upage, file, ofs, page_read_bytes, page_zero_bytes, writable);
  ...
}
```

기존의 load_segment 의 경우 file 을 바로 memory 에 추가하였다면 이제는 lazy loading 과정을 구현해야하기 때문에 위와 같이 init_file_spte 함수를 실행하여 page fault 가 발생한다면 해당 supplemental page table entry element 값 (즉, file) 을 통해 페이지를 저장할 수 있도록 수정하였다.  

```
static void
page_fault (struct intr_frame *f) 
{
  ...
  upage = pg_round_down (fault_addr);
   
  spt = &thread_current()->spt;
  spe = get_spte(spt, upage);

  if (load_page (spt, upage)) {
     return;
  }
  ...
}
```

그리고 page fault handler 에서 load_page 라는 함수를 통해 page fault 가 발생하였을 때, lazy loading 이 실행될 수 있도록 하였다. load_page 과정은 아래와 같다. 

```
extern struct lock FileLock;

bool
load_page (struct hash *spt, void *upage)
{
  struct spte *e;
  uint32_t *pagedir;
  void *kpage;
  e = get_spte (spt, upage);
  if (e == NULL)
    sys_exit (-1);

  kpage = falloc_get_page (PAL_USER, upage);
  if (kpage == NULL)
    sys_exit (-1);

  bool was_holding_lock = lock_held_by_current_thread (&FileLock);

  switch (e->status)
  {
  case PAGE_ZERO:
    memset (kpage, 0, PGSIZE);
    break;

  case PAGE_SWAP:
    // implement swapping  
    break;

  case PAGE_FILE:
    if (!was_holding_lock)
      lock_acquire (&FileLock);
    if (file_read_at (e->file, kpage, e->read_bytes, e->ofs) != e->read_bytes)
    {
      falloc_free_page (kpage);
      lock_release (&FileLock);
      sys_exit (-1);
    }
    memset (kpage + e->read_bytes, 0, e->zero_bytes);
    if (!was_holding_lock)
      lock_release (&FileLock);
    break;

  default:
    sys_exit (-1);
  }
    
  pagedir = thread_current ()->pagedir;

  if (!pagedir_set_page (pagedir, upage, kpage, e->writable))
  {
    falloc_free_page (kpage);
    sys_exit (-1);
  }

  e->kpage = kpage;
  e->status = PAGE_FRAME;

  return true;
}
```

page fault 가 났기 때문에 kpage (kernel page) 를 falloc 을 통해 새롭게 할당해준다. 그리고 switch case 를 사용하여 각 상황 (PAGE_ZERO, PAGE_SWAP, 그리고 PAGE_FILE) 각각에 대해서 처리해준다. 먼저, PAGE_ZERO 의 경우 memset 함수를 통해 해당 메모리를 0으로 초기화해준다. 그리고, PAGE_SWAP 의 경우 아래에서 설명할 swap table 과정을 통해 구현하여 아래에서 설명할 예정이다. 마지막으로 PAGE_FILE의 경우, file_read_at 함수를 통해 파일에서 데이터를 읽어와서 추가하고 memset 을 통해 나머지 영역을 0으로 초기화해주는 함수를 추가해주었다. 이때, 여러 process 에서 파일에 접근하는 것을 막기 위해서 file_lock 을 사용해 atomic 하게 구현하였다. 마지막으로, 새롭게 가져온 데이터를 기반으로 page directory 를 설정하고, supplemental page table entry 도 업데이트 해주었다.  

```
struct spte *
get_spte (struct hash *spt, void *upage)
{
  struct spte e;
  struct hash_elem *elem;

  e.upage = upage;
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
  upage = pg_round_down (fault_addr);
   
  spt = &thread_current()->spt;
  spe = get_spte(spt, upage);

  esp = user ? f->esp : thread_current()->esp;
  if (esp - 32 <= fault_addr && PHYS_BASE - MAX_STACK_SIZE <= fault_addr) {
    if (!get_spte(spt, upage)) {
      init_zero_spte (spt, upage);
    }
  }
  ...
```

stack growth 는 page fault 가 발생했을 때 실행된다. 먼저 pg_round_down 함수를 사용해 페이지 크기의 배수로 내림하여 해당 주소가 속한 페이지의 시작 주소를 upage 에 할당해준다. 그리고 esp 확장 가능한지 확인하기 위해서 if 문을 사용해 컨디션을 확인한 수, init_zero_spte 를 사용해 새로운 supplemental page table entry 를 생성한 후 supplemental page table에 추가해 주었다. 


### Difference from design report

기존의 디자인과 두가지 차별점이 생겼다. 먼저, stack grow 가 가능한지 확인하는 함수를 따로 구현하려고 하였으나 if 문을 사용해 간단히 확인이 가능할 것 같아 page fault 함수에서 실행하였다. 그리고 stack memory 를 확장해주는 함수는 supplemental page table 을 구현하는 과정에서 데이터를 zero 로 할당해주는 함수인 init_zero_spte를 사용하면 더 일관성있게 구현할 수 있을 것 같아 수정하였다. 


### 5. File memory mapping

### Implementation & Improvement from the previous design

```
struct thread
  {
   ...
    /* for PRJC3 */
    struct hash spt;
    void *esp;

    int mapid; /* 이 스레드가 얼마나 많은 mmf갖고 있나? */
    struct list mmf_list; /* 그 리스트 */
  };
```

```
// ./threads/thread.c
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  ...
  list_init (&t->mmf_list);
  t->mapid = 0;
  ...
}
```

### Difference from design report

#### Blueprint (Proposal)

##### Data Structure

File memory mapping(이하 FMM)은 여러 개의 매핑을 동시에 관리해야 하므로, 현재 관리 중인 모든 FMM을 `FMMList`라는 리스트에 저장한다. 이 리스트는 각 개별 FMM을 연결하는 list_elem을 포함하며, 각 FMM은 고유한 데이터 구조로 선언된다. 이를 위해 `FMM` 구조체를 정의하고, 다음과 같은 필드를 포함한다.

```c
struct FMM {
    int fmm_id;                  // 고유 ID
    struct file *f;              // 매핑된 파일 포인터
    struct list_elem FMM_elem;   // FMMList와 연결하는 필드
};
```

이러한 FMM 정보는 프로세스별로 관리되므로, `thread` 구조체에 FMM 리스트를 추가하여 해당 프로세스의 모든 매핑 정보를 추적할 수 있도록 한다.

##### Pseudo Code or Algorithm

File memory mapping 관리 알고리즘은 다음의 syscall 함수들을 통해 구현된다:

- `sys_mmap` : 특정 파일을 메모리에 매핑한다. 요청된 파일에 대해 새로운 FMM 구조체를 생성하고, 해당 구조체를 `FMMList`에 추가한다. 이를 통해 특정 프로세스가 접근하는 모든 FMM을 효율적으로 관리할 수 있다.  
- `sys_munmap` : 매핑된 파일의 메모리 매핑을 해제한다. `sys_mmap`에 의해 생성된 FMM을 해제하기 위해 `FMMList`를 순회하면서 매핑된 FMM 구조체를 찾고, 이를 할당 해제한다.  


### 6. Swap table

### Implementation & Improvement from the previous design 

```
./vm/frame.c

static struct fte *clock_cursor; /* fte에서 어떤 frame을 evict해야하나? (가르키는 대상이 fte이므로 type도 fte)*/
```

```
void
frame_init ()
{
  ...
  clock_cursor = NULL;
}
```

```
void *
falloc_get_page(enum palloc_flags flags, void *upage)
{
  ...
  if (kpage == NULL)
  {
    evict_page(); // 이부분 추가! 
    kpage = palloc_get_page (flags); 
    if (kpage == NULL)
      return NULL; 
  }
  ...
}
```

```
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
```

### Difference from design report
#### Blueprint (Proposal)

##### Data Structure

Swap 영역의 사용 여부를 추적하기 위해 bitmap을 사용하며, 이를 swap_table로 정의한다. 특정 bit가 1로 설정된 경우 해당 영역이 swap-out 가능하다는 것을 의미하도록 한다. 이 외에도 disk와 swap 작업의 동기화를 관리하기 위해 다음과 같은 구조체를 사용한다.

아래의 내용 맞는지 확인 필요 
- `swap_disk`: swap 영역이 위치한 디스크를 관리.
- `swap_lock`: swap 작업이 동기화되도록 보호.

```
struct swap_disk {
    struct block *disk;      // Disk block 포인터
    size_t size;             // Swap 영역 크기
    struct bitmap *swap_map; // Bitmap으로 swap 상태 관리
};

struct lock swap_lock;       // Swap 작업 동기화
```

##### Pseudo Code or Algorithm

Swap-in과 Swap-out 각각의 동작을 다음과 같이 정의한다:

- `swap_out(frame)`:  
  - Disk에서 사용할 빈 슬롯을 bitmap에서 찾는다.  
  - 해당 페이지를 디스크에 저장하고, Supplemental page table을 업데이트한다.  
  - Physical memory에서 페이지 매핑을 제거한다.  

- `swap_in(page)`: 
  - Physical memory의 frame을 새로 할당한다.  
  - Disk에서 swap table을 참조하여 해당 데이터를 읽어온다.  
  - 데이터를 메모리에 로드한 후, Supplemental page table을 업데이트한다.  

- `evict_page()`:   
  - Clock algorithm을 통해 swap-out할 페이지를 선택한다.  
  - 선택된 페이지를 swap_out 함수로 처리하여 공간을 확보한다.


### 7. On process termination

### Implementation & Improvement from the previous design 
### Difference from design report

#### Blueprint (Proposal)

##### Data Structure

`all_LockList`: 모든 lock을 관리하는 리스트 데이터 구조로, 생성된 lock을 추적하기 위해 global하게 선언한다. 이를 통해 acquire되거나 release된 모든 lock을 추적할 수 있다. 그리고, Lock 구조체의 필드에 list_elem을 추가하여 `all_LockList`에 연결할 수 있도록 한다. 그 외의 추가적인 구조체나 변수는 필요하지 않다.

##### Pseudo Code or Algorithm

- `release_frame_resources` : 현재 thread가 사용한 frame을 모두 해제한다. Frame table에서 해당 thread의 frame들을 제거한다.
- `release_supplemental_page_table` : Thread에 연결된 supplemental page table 엔트리를 순회하며 모든 가상 메모리 매핑 정보를 삭제한다.
- `release_file_mappings` : Memory-mapped 파일 목록을 순회하며 모든 매핑을 해제하고 관련 자원을 반환한다.
- `release_all_locks`** : `all_LockList`를 순회하며 해당 thread가 보유한 모든 lock을 release한다.

---

### Overall Limitations 


### Overall Discussion 


# Result 
