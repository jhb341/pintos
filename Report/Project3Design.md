# CSED 312 Project 3 Design Report 
김지현(20220302) \
전현빈(20220259)

## Analyze the Current Implementation 

이번 프로젝트 3에서는 virtual memory를 다루며, 이를 이해하기 위해 memory와 storage와 관련된 네 가지 주요 개념에 대한 이해가 필요하다. Pintos 문서에서는 이 네 가지 개념을 우선적으로 설명하고 있으므로, 본 디자인 보고서에서도 해당 개념들을 먼저 다룬 후, 각 알고리즘과 구조체를 설명할 것이다.

1. Page  
Page는 virtual page를 의미하며, 4,096(=2^12)바이트 크기의 연속적인 가상 메모리 영역이다. 32비트 가상 주소는 페이지 번호(page number)와 오프셋(offset) 두 부분으로 나뉜다. 따라서 한 페이지 내의 임의의 주소는 12bit의 offset주소로 가르킬 수 있다. 따라서 32bit Virtual Address(VA라 함)은 아래와 같은 구조를 갖는다.

```
                  31                 12 11         0
                   +-------------------+-----------+
                   |    Page Number    |   Offset  |
                   +-------------------+-----------+
                            Virtual Address
```

각 프로세스는 서로 다른 user virtual page를 가지며, kernel page는 모든 프로세스에서 동일하다. kernel process는 사용자 페이지와 커널 페이지 모두에 접근할 수 있는 반면, user process는 자신의 사용자 페이지에만 접근할 수 있다.

2. Frames  
Frame은 physical frame 또는 page frame을 의미하며, physical memory의 연속적인 영역을 나타낸다. Page와 유사하게, 32비트 physical address(PA라 함)는 프레임 번호(frame number)와 오프셋(offset)으로 나뉜다.

```
                  31                 12 11         0
                   +-------------------+-----------+
                   |    Frame Number   |   Offset  |
                   +-------------------+-----------+
                            Physical Address
```

이때 frame size와 page size가 같으므로 Pintos에서는 page를 frame과 mapping하여 해당 memory에 접근할 수 있도록 구현되어 있다.

3. Page Tables  
Page table은 앞서 설명한 page와 frame 간의 mapping 과정에서 사용되는 data structure이다. 이후 설명할 `pagedir.c` 코드에서 상세히 다루겠지만, 간단히 설명하자면, page table은 page number를 frame number로 mapping하며, offset은 page와 frame에서 동일한 값을 갖도록 구현되어 있다. 이를통해 VA를 PA로 번역(대응)할 수 있다.

```
	
                         +----------+
        .--------------->|Page Table|-----------.
       /                 +----------+            |
   0   |  12 11 0                            0   V  12 11 0
  +---------+----+                          +---------+----+
  |Page Nr  | Ofs|                          |Frame Nr | Ofs|
  +---------+----+                          +---------+----+
   Virt Addr   |                             Phys Addr   ^
                \_______________________________________/
```

5. Swap Slots  
Swap slot은 disk의 swap partition에서 page size만큼의 연속적인 영역을 의미한다. 

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

`page_from_pool` 함수는 앞서 설명한 `palloc_free_multiple` 함수에서 사용되며, 전달받은 페이지가 사용자 메모리 풀(`user memory pool`)에 속하는지 또는 커널 메모리 풀(`kernel memory pool`)에 속하는지를 판별하여 반환하는 함수이다. 

이 함수는 인수로 전달받은 페이지(`page`)의 주소가 해당 메모리 풀의 시작 페이지 주소보다 크고, 마지막 페이지 주소보다 작은지를 확인한 뒤, 결과를 `boolean` 값으로 반환한다.

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

`load_segment` 함수는 프로세스의 메모리 공간에 파일 데이터를 저장하는 역할을 한다. 먼저, 프로젝트 2에서 사용했던 `file_seek` 함수를 이용해 파일 읽기의 시작 위치를 `ofs`로 설정한다. 

이후, `read_bytes` 또는 `zero_bytes`가 남아 있는 동안 `while` 루프를 실행한다. 루프 안에서는 `file_read` 함수를 호출해 `page_read_bytes`만큼 데이터를 읽고, 이를 `kpage`에 저장한다. 

데이터를 성공적으로 `page_read_bytes`만큼 읽었다면, 현재 페이지에서 남은 바이트를 0으로 초기화한다. 그러나 만약 `page_read_bytes`만큼 데이터를 읽지 못한 경우, `palloc_free_page`를 호출해 할당된 페이지를 해제한다.

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

`install_page` 함수는 가상 메모리 공간인 `upage`를 현재 프로세스의 물리적 메모리 공간인 `kpage`에 매핑하는 역할을 한다. 먼저, `pagedir_get_page` 함수를 사용해 `upage`가 페이지 디렉토리에 존재하지 않는지 확인한다. 이후, `pagedir_set_page` 함수를 호출해 `upage`를 `kpage`에 매핑하고, 매핑 성공 여부를 반환한다. 이 함수에서 사용되는 `pagedir_get_page`와 `pagedir_set_page` 함수에 대한 상세 설명은 아래에서 다룰 예정이다.

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

다음으로, 위에서 자주 언급된 페이지 디렉토리(`page directory`)와 관련된 함수들에 대해 살펴보았다. 페이지 디렉토리는 이전 프로젝트 2에서 다룬 개념으로, 가상 메모리 공간(`virtual memory space`)을 물리 메모리 공간(`physical memory space`)에 매핑하기 위해 페이지 테이블(`page table`)을 관리하는 역할을 한다. 

먼저, `pagedir_create` 함수는 새로운 페이지 디렉토리를 생성하는 함수이다. 이 함수는 `memcpy`를 사용해 `init_page_dir`의 `PGSIZE` 크기 데이터를 새로 생성된 `pd`에 복사한 뒤, 생성된 `pd`를 반환한다.

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

`pagedir_destroy` 함수는 지정된 페이지 디렉토리(`page directory`)에 포함된 모든 리소스를 해제(`free`)하는 역할을 한다. 이 함수는 `for` 루프를 사용해 페이지 디렉토리 내부의 각 엔트리(entry)를 순회하면서, 먼저 해당 페이지 테이블의 주소를 가져온다. 이후, 또 다른 `for` 루프를 통해 페이지 테이블의 각 엔트리를 순회하며, 각 엔트리를 `palloc_free_page`를 사용해 해제한다. 모든 엔트리를 해제한 후, 페이지 테이블의 모든 엔트리를 순회가 완료되면 마지막으로 해당 페이지 디렉토리 자체를 해제한다.

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

`lookup_page` 함수는 인자로 전달받은 `vaddr`에 해당하는 PTE(Page Table Entry)를 찾거나 생성하는 함수이다. 이 함수에서 주의할 점은, `create` 옵션이 활성화된 경우라도 `vaddr`이 커널 영역의 주소일 경우 새 페이지 테이블을 생성할 수 없으므로 예외 처리가 필요하다는 것이다. 먼저, `vaddr`의 상위 10비트를 추출하여 이를 `pde`(Page Directory Entry) 값으로 저장한다. 만약 새로운 엔트리를 생성(`create`)해야 한다면, `palloc_get_page`와 `pde_create` 함수를 사용하여 새로운 PDE를 생성한다. 그 후, `pde_get_pt` 함수를 통해 해당 PDE가 가리키는 페이지 테이블의 물리 메모리 주소를 가져온다. 마지막으로, `vaddr`에 해당하는 PTE를 반환한다.

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

`pagedir_activate` 함수는 앞서 생성된 페이지 디렉토리(`page directory`)를 활성화하는 역할을 한다. 

만약 인자로 전달받은 페이지 디렉토리가 비어있거나(NULL인 경우), 해당 디렉토리가 유효하지 않다면, `init_page_dir` 함수를 호출하여 초기 페이지 디렉토리 형태로 활성화한다.

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

다음으로, `pagedir_set_page` 함수는 매핑을 수행하는 함수로, 몇 가지 조건을 확인한 후 매핑을 진행한다. 먼저, `upage`(가상 주소)가 반드시 페이지의 시작을 가리켜야 한다. 또한, `kpage`(물리 주소) 역시 페이지의 시작 주소여야 한다. 그다음으로, `upage`는 사용자 메모리 풀(user memory pool)에 속해야 하며, `kpage`는 물리 메모리 영역 내에 있어야 한다. 마지막으로, 페이지 디렉토리가 `init_page_dir`에 매핑되지 않도록 해야 하는데, 이는 사용자 프로세스의 페이지 디렉토리만 접근할 수 있도록 하기 위함이다. 

위의 조건들을 확인한 후, 앞서 설명한 `lookup_page` 함수를 이용해 PTE를 찾고, `pte_create_user` 함수를 사용해 `kpage`를 `upage`에 매핑한다. 이때 매핑의 성공 여부는 `boolean` 값으로 반환된다. 

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

`pagedir_get_page` 함수는 `uaddr`에 매핑된 물리적 주소(physical address)를 반환하는 함수이다. 먼저, `lookup_page` 함수를 호출해 `uaddr`에 해당하는 PTE(Page Table Entry)를 찾고 이를 저장한다. 그런 다음, `pte_get_page` 함수를 사용해 해당 PTE에 매핑된 물리적 페이지(physical page)의 시작 주소를 가져온다. 마지막으로, `uaddr`의 오프셋(offset)을 이 물리적 페이지의 시작 주소에 더한 값을 반환하여, 최종적으로 물리적 주소를 계산해 반환한다. 

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

`pagedir_clear_page` 함수는 `upage`와 매핑된 PTE(Page Table Entry)를 해제하는(clear) 함수이다. 이 함수는 먼저 `lookup_page` 함수를 사용해 `upage`에 해당하는 PTE를 찾는다. 이후, `invalidate_pagedir` 함수를 호출하여 페이지 매핑을 제거하고, TLB(Translation Lookaside Buffer)를 재활성화하여 최신 상태의 매핑을 유지할 수 있도록 한다. 또한, 이 함수에서도 `upage`가 페이지의 시작 주소인지와 사용자 영역의 가상 주소인지 여부를 확인하여 조건을 만족하는 경우에만 동작을 수행한다.

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

먼저, 네 개의 함수를 설명하기 전에 `dirty`와 `clean`의 차이에 대해 알아보았다다. 가상 주소가 `dirty`하다는 것은 해당 데이터가 변경되었음을 의미한다. 
`pagedir_is_dirty` 함수는 `lookup_page` 함수를 사용해 `vpage`에 해당하는 PTE(Page Table Entry)를 찾은 후, 해당 PTE가 수정된 적이 있는지 `PTE_D` 비트를 확인하여 결과를 `boolean` 값으로 반환한다.

```
bool
pagedir_is_dirty (uint32_t *pd, const void *vpage) 
{
  uint32_t *pte = lookup_page (pd, vpage, false);
  return pte != NULL && (*pte & PTE_D) != 0;
}
```

`pagedir_set_dirty` 함수는 위에서 언급한 `vpage`의 `PTE_D` 비트를 수정하는 함수이다. 만약 `dirty` 인수가 `true`라면 `PTE_D`를 설정하고, 그렇지 않은 경우 `PTE_D`를 해제한 뒤 `invalidate_pagedir` 함수를 호출해 TLB(Translation Lookaside Buffer)를 재활성화한다.

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

`pagedir_is_accessed` 함수는 `vpage`가 접근된 적이 있는지를 확인하는 함수이다. `PTE_A` 값을 활용하여 반환하며, `PTE_A`는 CPU가 해당 `vpage`에 대해 읽기 또는 쓰기 작업을 수행했는지 나타내는 플래그이다.

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

지금까지 `palloc`, `stack`, 그리고 `page directory`에 대해 알아보았다. 이제는 페이지 폴트(page fault)가 발생했을 때 어떤 동작이 수행되는지 살펴보자. 아래의 `page_fault` 함수는 `kill` 함수를 호출해 페이지 폴트가 발생한 프로세스를 즉시 종료한다. 그러나 이번 과제에서는 프로세스를 바로 종료하지 않고, 디스크에서 적절한 페이지를 메모리에 로드하는 과정을 구현할 것이다. 이에 대한 자세한 내용은 아래 `design` 파트에서 설명할 예정이다.

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

### 1. Frame table

#### Basics
Physical memory를 관리하고 효율적으로 Page를 load하고 관리하기 위해서는 frame에 대한 효율적인 관리가 선행되어야 한다. 이를 위해서 Frame table을 구현하여야 한다. Frame table의 entry는 각 frame에 load된 page의 pointer를 포함하여 각 frame이 어떤 page를 갖는지 명시한다. 이때 특정 page의 pointer를 갖지 않는 entry가 존재하는 경우에는 단순히 새로운 page의 pointer를 해당 entry에 추가하면 된다. 그러나 모든 entry가 특정 page pointer를 갖는경우, eviction policy에 따라 정해진 page를 evict하고 할당한다. 

** 기존 pintos의 page table 구현은 `userprog/pagedir.c`에 구현되어 있다. 
** 기존 pintos의 page allocation/deallocation은 `threads/palloc.c`에 구현되어 있다.

#### Limitations and Necessity
원본 pintos가 갖는 기존 구현에서는 free frame이 없는 경우 process가 원하는 동작을 할 수 없게 된다. 따라서 위에서 설명한 구조의 Frame table을 도입하므로써  free frame이 없는 경우에도 이미 할당된 page를 evict하고 여유 공간을 만들어냄으로써 새로운 page를 할당해 process의 동작을 유지할 수 있다. 

#### Blueprint (proposal)
##### Data structure
`Frame table`은 하나의 table이므로 list로 구현한다. 이 list에는 frame table entry가 list entry로서 존재한다. 이때 frame table entry는 새로운 구조체를 선언하여 구현하되 해당 구조체는 virtual page의 frame address, page address영역을 구분하여 저장하고 동시에 `list_elem`형식의 필드를 통해 앞선 frame table이 구현된 list에 연결될 수 있도록 해야한다. 이때 이 frame table (list)는 global data이므로 lock으로 atomic하게 접근됨을 보장해야 한다. 

##### pseudo code or algorithm
위의 data structure의 구현에 대한 pseudo code는 다음과 같다.
```
// pseudo code for frame table
/* frame table */
struct list FrameTable;

/* frame table entry */
struct FrameTableEntry
{
	struct list_elem FTE_elem;
	*frame_adr; 	// mapping되는 frame의 주소
	*page_adr;    	// mapping되는 page의 주소
};

/* lock for frame table */
struct lock ftLock;
```

evict policy는 `clock algorithm`을 따른다.
frame table을 관리하는 알고리즘은 다음의 함수에서 구현된다.

`init_ft`: `FrameTable`과 `ftLock`을 초기화 한다.
`alloc_new_frame`: 요청된 `page_adr`에 mapping되는 새로운 valid한 `frame_adr`을 얻어 (성공시) `FrameTableEntry`에 해당 정보를 저장하고 요청된 page주소에 대응되는 frame주소를 반환한다.
`free_frame`: 앞선 `alloc_new_frame`에 의해 할당된 frame에 mapping된 page를 할당 해제한다.


### 2. Lazy loading 

### 3. Supplemental page table

### 4. Stack growth

### 5. File memory mapping

### 6. Swap table

### 7. On process termination
