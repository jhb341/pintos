## Design Implementation 
### 1. Frame table

#### Basics

위에서 설명한 바와 같이 Frame은 물리 메모리의 연속적인 영역을 나타낸다. Frame table은 이러한 물리 메모리의 frame들을 효율적으로 관리하기 위한 데이터 구조이다. Frame table의 각 entry는 특정 frame에 로드된 페이지의 포인터를 포함하며, 각 frame이 어떤 페이지와 매핑되어 있는지를 명시한다. 기존 PintOS에서는 page(즉, 가상 메모리)에 대한 관리만을 위해 page table이 구현되어 있을 뿐, frame table에 대한 구현은 존재하지 않는다.

#### Limitations and Necessity

Frame table이 없으면 물리 메모리를 효율적으로 관리하기 어렵다. 기존 PintOS에서는 특정 프로세스가 어떤 frame을 사용하는지 추적할 방법이 없으며, 메모리 부족 상황에서 swap을 수행할 수 없는 한계가 존재한다. Frame table을 도입하면 사용 중인 frame을 추적할 수 있으며, 비어 있는 frame이 없는 경우 eviction 정책을 통해 이미 할당된 페이지를 swap out하여 여유 공간을 확보할 수 있다. 이를 통해 새 페이지를 할당하고 프로세스의 정상적인 동작을 유지할 수 있다. (swap 에 관한 내용은 6번에서 자세히 설명하였다)

#### Blueprint (Proposal)

##### Data Structure

Frame table은 list를 사용해 구현할 예정이다. 이 리스트는 각 frame을 나타내는 frame table entry로 구성된다. frame table entry는 새로운 구조체로 선언할 것이며, 해당 구조체는 physical memory frame 주소와 virtual page 주소를 분리하여 저장한다. 또한, list_elem 필드를 통해 frame table 리스트에 연결 리스트 형태로 저장할 예정이다. Frame table은 global 데이터이므로, 이를 보호하기 위해 lock을 사용하여 atomic하게 접근하도록 할 예정이다.

##### Data Structure Definition
```c
// Frame table
struct list FrameTable;

// Frame table entry
struct FrameTableEntry {
    struct list_elem FTE_elem;  // List element for connection
    void *frame_adr;            // Address of the physical frame
    void *page_adr;             // Address of the mapped virtual page
};

// Lock for frame table
struct lock ftLock;
```

---

##### Pseudo Code or Algorithm

**Eviction Policy**  
Frame table 관리에는 Clock Algorithm을 기반으로 한 eviction policy를 사용하려고 한다. 특정 엔트리가 비어 있는 경우에는 새로운 페이지 포인터를 해당 엔트리에 추가한다. 하지만 모든 엔트리가 이미 특정 페이지 포인터를 보유한 경우 eviction policy에 따라 지정된 페이지를 swap out한 후 새로운 페이지를 할당한다.이를 구현하기 위해 다음과 같은 세 가지 함수를 추가 할 예정이다:

- `init_ft`: Frame table과 관련된 전역 데이터 구조(FrameTable과 ftLock)를 초기화한다.
- `alloc_new_frame`: 요청된 `page_adr`와 매핑되는 새 유효 `frame_adr`을 할당한다. 성공 시 FrameTableEntry에 해당 정보를 저장하고 요청된 페이지 주소에 매핑된 frame 주소를 반환한다.
- `free_frame`: `alloc_new_frame`을 통해 할당된 frame과 매핑된 페이지를 할당 해제하고 관련 엔트리를 정리한다.

### 2. Lazy loading 

#### Basics

Lazy loading은 access 요청이 올 때마다 필요한 page data를 memory에 저장하는 것을 말한다. 기존의 PintOS는 page의 모든 executable이 memory에 load되는 방식을 사용한다. 아래의 load_segment 를 보면 while loop 를 통해서 해당 세그먼트의 모든 데이터가 페이지 단위로 메모리에 load 되는 것을 볼 수 있다. 그리고 install_page 를 통해 user space 의 virtual address upage 와 kernel 에서 할당한 physical page kpage 를 매핑해준다. 

```
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
...

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
	...
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
	...
    }
  return true;
}
```

#### Limitation and Necessity

모든 executable을 한번에 load하면 불필요한 data까지 memory에 load되어 메모리 공간 낭비가 심각하다는 문제가 있다. 또한 page fault를 처리하는 방법이 process termination과 kernel panic에 의존하기 때문에 효율적이지 못하다. 반면 Lazy loading은 필요한 시점에 필요한 데이터만 메모리에 로드하기 때문에 메모리를 효율적으로 사용할 수 있다는 장점을 갖는다.  

#### Blueprint (Proposal)

##### Data Structure

Lazy loading이 가능하다는 것은 언제 어떤 page가 필요한지 관리할 수 있음을 의미하며, 이는 각 page의 정보를 추적할 수 있는 데이터 구조가 필요함을 뜻한다. 각 page의 정보를 저장하는 구조체는 아래와 같다.  

```
struct page_table {
    struct file *f;       // 해당 page가 속한 파일
    off_t pte_fo;         // 파일 offset
    bool isWritable;      // 쓰기 가능 여부
};
```

정리하자면, `page_table` 구조체는 해당 page가 어느 file의 일부인지, file 내의 offset, 그리고 쓰기 가능 여부와 같은 정보를 저장한다.  

##### Pseudo Code or Algorithm

Lazy loading은 page fault 발생 시 시작된다. page fault가 발생했을 때, fault가 난 가상 메모리 주소가 물리 메모리에 이미 load되어 있다면 그대로 반환한다. 그렇지 않은 경우 해당 페이지를 physical memory에 load한다. 이 과정이 lazy loading이며, 일종의 fault handler이다.  

페이지 데이터를 가져오는 출처는 page의 type에 따라 다르다. 예를들어 일부 경우는 swap 영역에서 데이터를 가져오고, 그 외의 경우 disk에서 file을 load한다. swap 영역에서의 동작에 대해서는 6번에서 자세히 설명하였다. 이 과정은 disk에서 데이터를 읽어오는 함수와 swap하는 함수를 호출하여 physical memory로의 load를 수행한다.  

- `load_page_from_disk`: disk에서 file 데이터를 load하는 함수  
- `swap_page`: swap 영역에서 데이터를 가져오는 함수 (6번에서 추가 설명)  

### 3. Supplemental page table

#### Basics
Supplemental page table은 기존 page table을 보완하는 데이터 구조로서 virtual memory의 정보를 관리할 수 있도록 한다. 따라서 virtual memory의 `VA`(virtual adrress)에 해당하는 정보를 하나의 구조체로 묶어 entry `spte`를 구성하고 이러한 entry를 `spt`(supplemental page table)에서 관리할 수 있도록 한다. 

#### Limitation and Necessity

original pintos의 `pte` format은 다음과 같다.

```
      31                                   12 11 9      6 5     2 1 0
     +---------------------------------------+----+----+-+-+---+-+-+-+
     |           Physical Address            | AVL|    |D|A|   |U|W|P|
     +---------------------------------------+----+----+-+-+---+-+-+-+
```

현재 구현된 Page Table은 `availability`(AVL), `present`(P), `read/write`(W), `user/supervisor`(U), `accessed`(A), `dirty`(D)의 정보를 0~11bit에 저장한다. 그러나 lazy loading과 같은 기능을 지원하고 page fault를 처리하기 위해서는 추가적인 정보를 포함할 필요가 있다. 현재 pintos는 virtual memory를 관리할 수 있는 table이 함수가 없므로 지정된 virtual memory를 spt를 통해 관리할 수 있도록 한다.

#### Blueprint (proposal)
##### Data structure
`spt`의 entry `spte`는 특정 virtual memory의 정보를 갖기 때문에 정해진 VA에 대한 다음과 같은 정보를 가져야 한다. 따라서 아래 요소들은 `spte` 구조체의 필드로 존재한다.

- `va_vpn`: Virtual Page Number
- `va_offset`: Offset
- `isLoad`: physical memory에 load시 1, otherwise 0
- `file`: mapping된 file의 pointer
- `srcType`: file의 source가 binary인지, mapping file인지, swap disk인지 구분한다.
- `isWritable`: 해당 VA이 writable하면 1, otherwise 0

그러나 제공된 VA는 매우 많기 때문에 이러한 개별 VA에 대응되는 spte를 list로 관리하면 비효율적이다. 따라서 우리는 pintos에서 이미 구현되어 제공되는 `hash table`을 이용해 spt를 hash table로 구현하기로 한다. 따라서 `struct spte`는 아래의 필드도 포함하여야 한다.

- `spte_hashElem`: spte의 hash table element

##### pseudo code or algorithm
spt는 아래와 같이 thread의 필드로 선언한다.
```
struct thread{
/* thread.h의 thread 구조체 선언부 */
	struct hash spt;
...
};
```

hash algorithm을 이용하나, pintos에 미리 구현된 hash table을 사용하며 아래의 함수들로 spt를 관리한다. 
- `get_spte_key`: spte의 hash key를 반환한다.
- `get_spte`: spte의 evict등을 위해 spt에서 spte를 return한다.
- `init_spt`: `hash_init`을 이용해 spt를 초기화한다.
- `get_new_spte`: malloc을 이용해 새로운 frame을 할당 후 spt에 `has_insert`로 삽입하도록 한다.


### 4. Stack growth

#### Basics

Growing stack은 프로세스의 요청에 따라 유한하게 크기를 동적으로 늘릴 수 있는 메커니즘이다. 그러나 기존 PintOS는 4KB 크기의 fixed-size stack만을 지원하며, dynamic하게 크기를 조정할 수 없다. 이를 해결하기 위해, 프로세스 요청에 따라 4KB보다 더 큰 stack을 제공할 수 있도록 수정해야 한다.

#### Limitation and Necessity

기존 PintOS는 고정된 4KB의 fixed-stack size를 제공하며, 이를 초과한 stack 요청이 있을 경우 page fault가 발생하도록 설계되어 있다. 그러나 fixed-size stack은 local variable과 argument 수에 제한을 두게 되며, 이는 실제 프로그램 실행에서 유용하지 않다. 따라서 dynamic하게 stack size를 확장할 수 있는 기능이 필요하다.

#### Blueprint (Proposal)

##### Data Structure

Stack grow 기능을 구현하기 위해 각 thread의 stack pointer(esp) 값을 추적해야 하므로, 기존 thread 구조체에 esp 포인터를 추가한다. 별도의 새로운 구조체를 선언하지는 않는다.

##### Pseudo Code or Algorithm

Page fault 발생 시 stack grow가 필요한 상황을 확인하고, 조건에 맞으면 stack을 확장하도록 구현한다. 이를 위해 다음과 같은 함수를 구현하거나 수정한다:

- `is_stack_access(addr, esp)`: Page fault가 발생한 주소(addr)가 현재 thread의 esp와 충분히 가까운지 확인한다. 조건을 만족할 경우 stack grow를 허용한다.  
- `grow_stack(fault_addr)`: Page fault가 발생한 주소를 기준으로 stack 영역을 확장한다. 이때, free page를 supplemental page table을 통해 할당받아 물리 메모리에 매핑한다.  
- `page_fault`(기존 함수 수정) : Page fault가 발생했을 때 fault address를 확인하고, stack grow 조건을 만족하면 `grow_stack`을 호출하여 stack을 확장한다. 조건에 부합하지 않을 경우 기존 page fault 처리 방식을 유지한다.   


### 5. File memory mapping

#### Basics

File memory mapping은 파일 내용을 page(가상 메모리)에 매핑하여 프로세스가 메모리에서 파일 데이터에 직접 접근할 수 있도록 하는 메커니즘이다. 기존 PintOS는 파일 입출력을 file_read 및 file_write를 통해서만 처리하도록 구현되어 있다. 이번 프로젝트에서는 페이지를 파일에 매핑하는 file memory mapping을 구현할 것이다.

#### Limitation and Necessity

기존 PintOS는 file_read와 file_write를 통해서만 파일에 접근할 수 있기 때문에 효율성이 떨어지며, shared resource에 대한 지원이 부족하다. 이를 개선하기 위해 mmap syscall과 munmap syscall을 구현하여 파일 데이터를 디스크에서 메모리로 로드하고, 이를 프로세스가 직접 메모리에서 접근할 수 있도록 한다. 이를 통해 효율적인 메모리 사용이 가능하도록 할 예정이다. 

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

#### Basics

Swap에는 두 가지 주요 동작이 있다. Swap-in은 page fault가 발생했을 때 disk의 특정 영역에서 페이지를 가져와 memory의 frame에 로드하는 과정을 말하며, Swap-out은 swap-in을 수행하기 위해 정해진 policy에 따라 frame에 로드되어 있던 페이지를 evict하여 swap disk에 저장하고 free frame을 확보하는 과정을 말한다. Swap disk를 관리하기 위해 사용하는 데이터 구조를 swap_table이라 한다.

Swap 기능을 통해 virtual address를 사용한 address translation은 프로세스 관점에서 무한히 넓은 메모리 공간을 제공하는 것처럼 보이며, 실제 physical memory보다 넓은 영역을 사용할 수 있도록 한다.


#### Limitation and Necessity

기존 PintOS는 swap 기능이 구현되어 있지 않다. 이로 인해 메모리가 부족한 경우 프로세스가 종료되도록 설계되어 있다. 이는 메모리 활용의 유연성을 크게 제한한다.


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

#### Basics

프로세스가 종료(exit)할 때, 할당된 모든 자원을 해제하여 메모리 누수를 방지해야 한다. 여기서 자원이란 위에서 구현한 frame, supplemental page table 등을 포함한다. 또한, 위의 기능들을 구현하면서 사용된 lock도 반드시 release해야 한다. Lock을 release하지 않고 프로세스를 종료하면 이후 deadlock이 발생할 수 있다.

#### Limitation and Necessity

위의 1~6번에서 설명한 것처럼, 몇몇 구조체와 데이터는 기존 PintOS에 구현되어 있지 않다. 따라서 이러한 리소스를 새롭게 선언하거나 기존 thread 구조체의 필드로 추가하였다. 그러나 기존 PintOS는 이러한 추가된 리소스에 대한 해제를 구현하고 있지 않다. 이를 해결하기 위해 `process_exit` 함수를 수정하여, 종료 시점에서 모든 리소스를 적절히 release하도록 구현할 것이다.

#### Blueprint (Proposal)

##### Data Structure

`all_LockList`: 모든 lock을 관리하는 리스트 데이터 구조로, 생성된 lock을 추적하기 위해 global하게 선언한다. 이를 통해 acquire되거나 release된 모든 lock을 추적할 수 있다. 그리고, Lock 구조체의 필드에 list_elem을 추가하여 `all_LockList`에 연결할 수 있도록 한다. 그 외의 추가적인 구조체나 변수는 필요하지 않다.

##### Pseudo Code or Algorithm

- `release_frame_resources` : 현재 thread가 사용한 frame을 모두 해제한다. Frame table에서 해당 thread의 frame들을 제거한다.
- `release_supplemental_page_table` : Thread에 연결된 supplemental page table 엔트리를 순회하며 모든 가상 메모리 매핑 정보를 삭제한다.
- `release_file_mappings` : Memory-mapped 파일 목록을 순회하며 모든 매핑을 해제하고 관련 자원을 반환한다.
- `release_all_locks`** : `all_LockList`를 순회하며 해당 thread가 보유한 모든 lock을 release한다.
