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

먼저 page 를 할당할 때 사용되는 page allocation 함수들에 대해서 알아볼 것이다. 위에서 설명한 page 와 fram 모두 page-size 로 memory 가 할당되는데, 이때 palloc 에 해당하는 함수들이 사용된다. 먼저 pintos의 시작점인 init.c 코드의 main 함수를 살펴보면 palloc_init 함수를 통해 

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

page allocation 
```
static bool
install_page
```

```
void *
palloc_get_multiple
```

```
void *
palloc_get_page
```

```
void
palloc_free_multiple
```

```
void
palloc_free_page
```

```
static bool
load_segment
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
