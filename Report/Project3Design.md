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

다음으로 palloc.c 의 다른 함수들을 살펴보았다. 

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


stack 

```
static bool
setup_stack
```

page table 
```
uint32_t *
pagedir_create
```

```
void
pagedir_destroy
```

```
void
pagedir_activate
```

```
bool
pagedir_set_page
```

```
void *
pagedir_get_page
```

```
void
pagedir_clear_page
```

```
// File: userprog/pagedir.h
bool pagedir_is_dirty (uint32_t *pd, const void *upage);
void pagedir_set_dirty (uint32_t *pd, const void *upage, bool dirty);
bool pagedir_is_accessed (uint32_t *pd, const void *upage);
void pagedir_set_accessed (

```

page fault 

```
static void
page_fault
```

frame table 










## Design Implementation 

### 1. Supplement Page Table 

### 2. Frame Table 

### 3. Swap Table 

### 4. Memory Mapped Files 
