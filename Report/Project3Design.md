# CSED 312 Project 3 Design Report 
김지현(20220302) \
전현빈(20220259)

## Analyze the Current Implementation 

이번 프로젝트 3에서는 virtual memory를 다루며, 이를 이해하기 위해 memory와 storage와 관련된 네 가지 주요 개념에 대한 이해가 필요하다. Pintos 문서에서는 이 네 가지 개념을 우선적으로 설명하고 있으므로, 본 디자인 보고서에서도 해당 개념들을 먼저 다룬 후, 각 알고리즘과 구조체를 설명할 것이다.

1. Page  
Page는 virtual page를 의미하며, 4,096바이트 크기의 연속적인 가상 메모리 영역이다. 32비트 가상 주소는 페이지 번호(page number)와 오프셋(offset) 두 부분으로 나뉜다.

(pg 46 사진 캡처 삽입)

각 프로세스는 서로 다른 user virtual page를 가지며, kernel page는 모든 프로세스에서 동일하다. kernel process는 사용자 페이지와 커널 페이지 모두에 접근할 수 있는 반면, user process는 자신의 사용자 페이지에만 접근할 수 있다.

2. Frames  
Frame은 physical frame 또는 page frame을 의미하며, physical memory의 연속적인 영역을 나타낸다. Page와 유사하게, 32비트 physical address는 프레임 번호(frame number)와 오프셋(offset)으로 나뉜다.

(캡처 삽입)

Pintos에서는 page를 frame과 mapping하여 해당 memory에 접근할 수 있도록 구현되어 있다.

3. Page Tables  
Page table은 앞서 설명한 page와 frame 간의 mapping 과정에서 사용되는 data structure이다. 이후 설명할 `pagedir.c` 코드에서 상세히 다루겠지만, 간단히 설명하자면, page table은 page number를 frame number로 mapping하며, offset은 page와 frame에서 동일한 값을 갖도록 구현되어 있다.

4. Swap Slots  
Swap slot은 disk의 swap partition에서 page size만큼의 연속적인 영역을 의미한다. 
** 추가적인 설명 필요해 보임**

위와 같이, 프로젝트 3에서 다루는 virtual memory의 핵심 개념인 Page, Frame, Page Table, 그리고 Swap Slot에 대해 설명하였다. 아래에서는 이 개념들을 기반으로 한 알고리즘 및 구조체 구현 방식을 다룰 것이다.

먼저, page를 할당할 때 사용되는 주요 함수들에 대해 알아본다. 앞서 설명한 page와 frame은 모두 page-size 단위로 메모리가 할당되며, 이 과정에서 palloc 함수가 사용된다. Pintos의 시작점인 `init.c` 코드의 `main` 함수를 살펴보면, `palloc_init` 함수를 통해 전체 free memory를 계산한 후, 이를 user memory와 kernel memory로 나누는 것을 확인할 수 있다. 이후, 각각의 메모리는 `init_pool` 함수를 통해 초기화된다. 이 초기화 과정에서 사용자와 커널 메모리는 별도의 메모리 풀(pool)로 관리된다. 

```
main (void)
{
  /* Initialize memory system. */
  palloc_init (user_page_limit);
...
}

void
palloc_init (size_t user_page_limit)
{
  /* Free memory starts at 1 MB and runs to the end of RAM. */
  uint8_t *free_start = ptov (1024 * 1024);
  uint8_t *free_end = ptov (init_ram_pages * PGSIZE);
  size_t free_pages = (free_end - free_start) / PGSIZE;
  size_t user_pages = free_pages / 2;
  size_t kernel_pages;
  if (user_pages > user_page_limit)
    user_pages = user_page_limit;
  kernel_pages = free_pages - user_pages;

  /* Give half of memory to kernel, half to user. */
  init_pool (&kernel_pool, free_start, kernel_pages, "kernel pool");
  init_pool (&user_pool, free_start + kernel_pages * PGSIZE,
             user_pages, "user pool");
}
```

위에서 설명한 사용자와 커널 메모리를 초기화하기 위해 사용되는 `init_pool` 함수는 각 페이지의 사용 여부를 관리하는 bitmap을 설정한다. 이 함수에서는 전체 페이지 중 일부를 비트맵 저장 공간으로 사용하며, `bitmap_create_in_buf` 함수를 통해 비트맵을 초기화한다. 이후, 메모리 할당이 시작되는 주소를 `base + bm_pages * PGSIZE`로 설정하여 비트맵 영역과 할당 영역을 구분한다. 또한, 멀티스레드 환경에서 race condition을 방지하기 위해 메모리 풀 구조체의 락(`p->lock`)을 초기화한다. 

```
static void
init_pool (struct pool *p, void *base, size_t page_cnt, const char *name) 
{
  /* We'll put the pool's used_map at its base.
     Calculate the space needed for the bitmap
     and subtract it from the pool's size. */
  size_t bm_pages = DIV_ROUND_UP (bitmap_buf_size (page_cnt), PGSIZE);
  if (bm_pages > page_cnt)
    PANIC ("Not enough memory in %s for bitmap.", name);
  page_cnt -= bm_pages;

  printf ("%zu pages available in %s.\n", page_cnt, name);

  /* Initialize the pool. */
  lock_init (&p->lock);
  p->used_map = bitmap_create_in_buf (page_cnt, base, bm_pages * PGSIZE);
  p->base = base + bm_pages * PGSIZE;
}
```

다음으로 `palloc.c`의 다른 함수들을 살펴보았다. 먼저, `palloc_get_page` 함수는 하나의 페이지를 할당하는 함수로, 내부적으로 `palloc_get_multiple()` 함수를 호출한다. 이때, 1을 인자로 넘겨 하나의 페이지만 할당되도록 한다. 

`palloc_get_multiple` 함수에서는 먼저 인자로 전달받은 `flag` 값이 `PAL_USER`인지 확인한다. `PAL_USER`인 경우 사용자 메모리 풀에서 메모리를 할당하도록 `pool` 포인터를 설정하고, 그렇지 않으면 커널 메모리 풀에서 메모리를 할당하도록 설정한다. 이후, `lock`을 사용해 원자적(atomic)으로 `bitmap_scan_and_flip` 함수를 호출하여, `page_cnt`만큼 연속적으로 비어 있는 페이지를 검색하고 이를 `used` 상태로 설정한다.

할당 가능한 페이지가 존재할 경우, 해당 페이지의 첫 번째 인덱스를 반환하며, 페이지가 존재하지 않으면 `BITMAP_ERROR`를 반환하여 에러를 처리한다. `lock`을 해제한 후, 반환받은 인덱스를 이용해 실제 할당된 메모리의 시작 주소를 계산한다. 이 계산은 현재 메모리 풀의 시작 주소에 `(페이지 크기 * 인덱스)`를 더하는 방식으로 이루어진다.

마지막으로, `pages`가 정상적으로 할당된 후 `flags` 값을 확인하여, 메모리를 0으로 초기화해야 할 경우 `memset` 함수를 사용해 초기화한다.

```
void *
palloc_get_page (enum palloc_flags flags) 
{
  return palloc_get_multiple (flags, 1);
}

void *
palloc_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
  struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;
  void *pages;
  size_t page_idx;

  if (page_cnt == 0)
    return NULL;

  lock_acquire (&pool->lock);
  page_idx = bitmap_scan_and_flip (pool->used_map, 0, page_cnt, false);
  lock_release (&pool->lock);

  if (page_idx != BITMAP_ERROR)
    pages = pool->base + PGSIZE * page_idx;
  else
    pages = NULL;

  if (pages != NULL) 
    {
      if (flags & PAL_ZERO)
        memset (pages, 0, PGSIZE * page_cnt);
    }
  else 
    {
      if (flags & PAL_ASSERT)
        PANIC ("palloc_get: out of pages");
    }

  return pages;
}
```

`palloc_free_page`는 하나의 페이지 메모리를 해제(release)하는 함수이다. 이와 유사하게 `palloc_free_multiple` 함수를 호출하여 여러 페이지를 해제할 수 있다. `palloc_free_multiple` 함수에서는 먼저 `page_from_pool` 함수를 사용해 해당 페이지(`page`)가 `kernel`에 속하는지, 혹은 `user`에 속하는지 확인한 뒤, 적절한 `pool` 포인터에 저장한다. 이후, 메모리 주소를 `pg_no` 함수를 통해 페이지 단위로 변환하여 페이지 번호를 계산한다. 이 계산 과정은, 현재 페이지의 메모리 주소에서 메모리 풀(pool)의 시작 주소를 빼주는 방식으로 이루어진다. 마지막으로, `bitmap_all` 함수를 통해 해당 페이지가 현재 `used` 상태인지 확인한 뒤, `bitmap_set_multiple` 함수를 사용하여 해제하려는 페이지를 `unused` 상태로 변경한다.

```
void
palloc_free_page (void *page) 
{
  palloc_free_multiple (page, 1);
}

void
palloc_free_multiple (void *pages, size_t page_cnt) 
{
  struct pool *pool;
  size_t page_idx;

  ASSERT (pg_ofs (pages) == 0);
  if (pages == NULL || page_cnt == 0)
    return;

  if (page_from_pool (&kernel_pool, pages))
    pool = &kernel_pool;
  else if (page_from_pool (&user_pool, pages))
    pool = &user_pool;
  else
    NOT_REACHED ();

  page_idx = pg_no (pages) - pg_no (pool->base);

#ifndef NDEBUG
  memset (pages, 0xcc, PGSIZE * page_cnt);
#endif

  ASSERT (bitmap_all (pool->used_map, page_idx, page_cnt));
  bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
}
```

(문법 검사) 
page_from_pool 함수는 위에서 설명한 palloc_free_multiple 함수에서 사용된 함수로 user 메모리 풀에 해당하는지 kernel 메모리 풀에 해당하는지를 반환해주는 함수이다. 인수로 받아온 page 의 주소가 해당 메모리 풀의 시작 페이지 주소보다 크고 마지막 페이지 주소보다 작은지 확인 후 boolean 변수를 반환한다. 

```
static bool
page_from_pool (const struct pool *pool, void *page) 
{
  size_t page_no = pg_no (page);
  size_t start_page = pg_no (pool->base);
  size_t end_page = start_page + bitmap_size (pool->used_map);

  return page_no >= start_page && page_no < end_page;
}
```

(문법 검사)
load_segment 함수는 프로세스의 메모리 공간에 파일의 데이터를 저장하는 함수이다. 먼저 프로젝트 2에서 사용했던 file_seek 함수를 이용해서 file read 가 시작하는 위치를 ofs 로 설정해준다. 그리고 read_bytes 혹은 zero_bytes 가 남아있는 경우 while loop 를 돌면서 file_read 함수를 통해 page_read_bytes 만큼 읽고 kpage 에 저장해준다. page_read_bytes 만큼 읽었다면 현재페이지로부터 남은 파이트를 0 으로 초기화해주고 만약 page_read_bytes 만큼 읽지 못했다면 palloc_free_page 를 해준다. 

```
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}
```

(문법 검사)
install_page 함수는 가상메모리 공간인 upage 에 현제 프로세스의 physical 메모리 공간인 kpage를 매핑하는 역할을 한다. pagedir_get_page 함수와 pagedir_set_page 함수를 이용해서 먼저 upage 가 page directory 에 없는 것을 확인하고, upage 를 kpage 에 매핑하고 성공 여부를 반환해준다. 여기서 사용하는 pagedir_get_page 와 pagedir_set_page 함수는 아래에서 설명할 예정이다. 

```
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
```

(추가)
stack -> 설명 추가 필요 

```
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}
```

(문법 검사)
다음으로, 위에서 자주 나왔던 page directory 와 관련된 함수에 대해서 알아보았다. page directory 는 저번 프로젝트 2에서 나온 개념으로 virtual memory space 를 physical memory space 로 매핑하기 위해 page table을 관리한다. 먼저, pagedir_create 함수는 새로운 page directory 를 생성하는 함수로 memcpy 를 통해 init_page_dir 에서 PGSIZE 크기의 데이터를 pd 에 복사 후 pd 를 반환하는 역할을 한다. 

```
uint32_t *
pagedir_create (void) 
{
  uint32_t *pd = palloc_get_page (0);
  if (pd != NULL)
    memcpy (pd, init_page_dir, PGSIZE);
  return pd;
}
```

(문법 검사)
pagedir_destroy 함수는 해당 page directory 에 있는 모든 resource 를 free 해주는 함수이다. for loop 를 통해 page directory 내부의 각 entry 를 순회하면서 먼저 page table 의 주소를 가져온다. for loop 를 통해서 해당 page table 의 entry 를 순회하면서 각 entry 를 palloc_free_page 해준 다음 모든 entry 가 free 되고 모든 page directory 의 entry 를 순회하고 나면 마지막으로 해당 page directory 자체를 free 해준다. 

```
void
pagedir_destroy (uint32_t *pd) 
{
  uint32_t *pde;

  if (pd == NULL)
    return;

  ASSERT (pd != init_page_dir);
  for (pde = pd; pde < pd + pd_no (PHYS_BASE); pde++)
    if (*pde & PTE_P) 
      {
        uint32_t *pt = pde_get_pt (*pde);
        uint32_t *pte;
        
        for (pte = pt; pte < pt + PGSIZE / sizeof *pte; pte++)
          if (*pte & PTE_P) 
            palloc_free_page (pte_get_page (*pte));
        palloc_free_page (pt);
      }
  palloc_free_page (pd);
}
```

(문법 검사)
lookup_page 함수는 인수로 받아오는 vaddr 에 해당하는 PTE (page table entry) 를 찾거나 생성하는 함수이다. 이때 유의할 점은 만약 create 를 하는 경우 vaddr 이 kernel 영역의 주소라면 새 페이지 테이블을 생성할 수 없기 때문에 예외처리를 해주어야 한다. 먼저, vaddr 의 상위 10비트를 pde(page directory entry) 값으로 저장하고 만약 create 해주어야 한다면 palloc_get_page 함수와 pde_create 함수를 통해 새로운 PDE 를 생성해준다. 이후, pde_get_pt 함수를 통해 해당 PDE 에 해당하는 page table 의 physical memory 주소를 가져온다. 이후에 vaddr 에 해당하는 page table entry 를 반환해준다. 

```
static uint32_t *
lookup_page (uint32_t *pd, const void *vaddr, bool create)
{
  uint32_t *pt, *pde;

  ASSERT (pd != NULL);

  /* Shouldn't create new kernel virtual mappings. */
  ASSERT (!create || is_user_vaddr (vaddr));

  /* Check for a page table for VADDR.
     If one is missing, create one if requested. */
  pde = pd + pd_no (vaddr);
  if (*pde == 0) 
    {
      if (create)
        {
          pt = palloc_get_page (PAL_ZERO);
          if (pt == NULL) 
            return NULL; 
      
          *pde = pde_create (pt);
        }
      else
        return NULL;
    }

  /* Return the page table entry. */
  pt = pde_get_pt (*pde);
  return &pt[pt_no (vaddr)];
}
```

(문법 검사)
pagedir_activate 함수는 위에서 create한 page directory 를 활성화해주는 역할을 한다. 만약 인수로 받아온 page directory 가 비어있다면 (NULL 일 경우) init_page_dir 함수를 이용해 initial page directory 형태로 활성화해준다. 

(추가)
"asm volatile ("movl %0, %%cr3" : : "r" (vtop (pd)) : "memory");" 부분 설명

```
void
pagedir_activate (uint32_t *pd) 
{
  if (pd == NULL)
    pd = init_page_dir;

  /* Store the physical address of the page directory into CR3
     aka PDBR (page directory base register).  This activates our
     new page tables immediately.  See [IA32-v2a] "MOV--Move
     to/from Control Registers" and [IA32-v3a] 3.7.5 "Base
     Address of the Page Directory". */
  asm volatile ("movl %0, %%cr3" : : "r" (vtop (pd)) : "memory");
}
```

(문법 검사)
다음으로, pagedir_set_page 함수는 매핑을 해주는 함수로 몇가지 조건 확인 후 매핑을 진행한다. 먼저, upage (가상 주소)가 페이지의 시작을 가리겨야한다. 그리고 kpage (physical 주소) 역시 페이지의 시작 주소여야 한다. 다음으로, upage 가 user memory pool 에 있어야하며, kpage 가 physical memory 영역 안에 있어야한다. 마지막으로, page directory 를 init_page_dir 에 매핑하지 않도록 한다. 왜냐면 user process 의 page directory 만 접근하도록 해야되기 때문이다. 

위의 사항들을 확인 한 후, 위에서 설명한 lookup_page 함수를 이용해 PTE 를 찾고, pte_create_user 함수를 이용해 kpage 를 upage 에 매핑해준다. 이때 매핑의 성공 여부를 boolean 변수로 반환해준다. 

```
bool
pagedir_set_page (uint32_t *pd, void *upage, void *kpage, bool writable)
{
  uint32_t *pte;

  ASSERT (pg_ofs (upage) == 0);
  ASSERT (pg_ofs (kpage) == 0);
  ASSERT (is_user_vaddr (upage));
  ASSERT (vtop (kpage) >> PTSHIFT < init_ram_pages);
  ASSERT (pd != init_page_dir);

  pte = lookup_page (pd, upage, true);

  if (pte != NULL) 
    {
      ASSERT ((*pte & PTE_P) == 0);
      *pte = pte_create_user (kpage, writable);
      return true;
    }
  else
    return false;
}
```

(문법 검사)
pagedir_get_page 함수는 uaddr 에 매핑된 physical address 를 반환해주는 함수이다. loopup_page 함수를 이용해 uaddr 에 해당하는 pte 를 저장하고 pte_get_page 함수를 통해 pte 에 해당하는 physical page 의 시작 주소를 가져온다. 그리고 uaddr 의 offset 을 더해준 값을 반환하여 결과적으로 physical address 를 반환한다. 

```
void *
pagedir_get_page (uint32_t *pd, const void *uaddr) 
{
  uint32_t *pte;

  ASSERT (is_user_vaddr (uaddr));
  
  pte = lookup_page (pd, uaddr, false);
  if (pte != NULL && (*pte & PTE_P) != 0)
    return pte_get_page (*pte) + pg_ofs (uaddr);
  else
    return NULL;
}
```

(문법 검사)
pagedir_clear_page 함수는 upage 와 매핑되어있는 pte 를 clear 하는 함수이다. 이번 함수 역시 lookup_page 함수를 통해 PTE 를 찾은 다음, invalidate_pagedir 함수를 통해 페이지 매핑을 없앤 것을 TLB를 재활성화 시켜 up to date 매핑을 갖고 있도록한다다 . 이 함수에서도 비슷하게 upage 가 시작 주소인지 와 사용자 영역의 가상 주소인지 확인해준다. 

```
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
```

(문법 검사)
아래의 네 개의 함수들을 설명하기 전에 먼저 dirty 와 clean 의 차이에 대해서 설명하자면 어떤 가상 주소가 dirty 라는 것은 변경되었다는 것을 의미하고 clean 하다는 것은 up to date 정보가 저장되어있다는 것을 뜻한다. 아래의 pagedir_is_dirty 함수는 lookup_page 함수를 통해 먼저 vpage 에 해당하는 PTE 를 찾고, 해당 pte 가 수정된 적이 있는지 PTE_D 비트를 사용해 확인하고 결과를 boolean 타입으로 반환해준다. 

```
bool
pagedir_is_dirty (uint32_t *pd, const void *vpage) 
{
  uint32_t *pte = lookup_page (pd, vpage, false);
  return pte != NULL && (*pte & PTE_D) != 0;
}
```

(문법 검사)
아래의 pagedir_set_dirty 함수는 위에서 언급한 vpage 의 PTE_D 를 수정해주는 함수이다. 만약 dirty 인수가 true 라면 PTE_D 를 설정해주고 만약 dirty 가 아닐 경우, PTE_D 를 해제해주고 invaidate_pagedir 함수를 통해 TLB (translation lookaside buffer) 를 재활성화시켜주어야 한다. 


```
void
pagedir_set_dirty (uint32_t *pd, const void *vpage, bool dirty) 
{
  uint32_t *pte = lookup_page (pd, vpage, false);
  if (pte != NULL) 
    {
      if (dirty)
        *pte |= PTE_D;
      else 
        {
          *pte &= ~(uint32_t) PTE_D;
          invalidate_pagedir (pd);
        }
    }
}
```

(문법 검사)
pagedir_is_accessed 함수는 vpage 에 접근되었는지를 확인하는 함수이다. 위에서와 비슷하게 PTE_A 값을 활용해 반환해준다. PTE_A 값을 CPU 가 해당 vpage 에 읽기 또는 쓰기를 했는지 여부를 나타내주는 flag 이다. 


```
bool
pagedir_is_accessed (uint32_t *pd, const void *vpage) 
{
  uint32_t *pte = lookup_page (pd, vpage, false);
  return pte != NULL && (*pte & PTE_A) != 0;
}

void
pagedir_set_accessed (uint32_t *pd, const void *vpage, bool accessed) 
{
  uint32_t *pte = lookup_page (pd, vpage, false);
  if (pte != NULL) 
    {
      if (accessed)
        *pte |= PTE_A;
      else 
        {
          *pte &= ~(uint32_t) PTE_A; 
          invalidate_pagedir (pd);
        }
    }
}
```

(문법 검사)
이렇게 앞에서 palloc 과 stack, 그리고 page directory 에 대해서 알아봤고, 이제는 page fault 가 발생했을 때 어떤 동작이 작동하는지에 대해서 알아볼 것이다. 아래의 page_fault 함수를 보면 kill 함수를 통해 page fault 가 발생한 프로세스를 바로 종료해주는 것을 볼 수 있다. 하지만 이번 과제에서는 바로 kill 하는 것이 아니라 disk 에서 알맞은 page 를 메모리에서 load 해주는 과정을 구현할 것이다. 관련 내용은 아래의 design 파트에서 더 자세히 설명할 예정이다. 

```
static void
page_fault (struct intr_frame *f) 
{
  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */

  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;


  printf (...);
  kill (f);
}
```

```install page 설명 ?```

## Design Implementation 

### 1. Supplement Page Table 

### 2. Frame Table 

### 3. Swap Table 

### 4. Memory Mapped Files 
