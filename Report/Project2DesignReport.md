# CSED 312 Project 2 Design Report 
김지현(20220302) \
전현빈(20220259)

## Analyze the Current Implementation 
procedure of process execution - thread init.c and userprog process.c (나중에 지우기)

### 1. Process execution 

Pintos가 실행된 후 user program이 어떻게 시작되는지 확인하기 위해, Pintos의 main entry point를 확인해보았다. 아래 코드에서 #ifdef USERPROG 부분을 보았을 때, USERPROG 매크로가 정의된 경우에만 user program과 관련된 초기화 코드가 컴파일된다는 것을 확인했다.

```
int
 main (void)
 {
  '''
   /* Segmentation. */
 #ifdef USERPROG
   tss_init ();
   gdt_init ();
 #endif
  
   /* Initialize interrupt handlers. */
   intr_init ();
   timer_init ();
   kbd_init ();
   input_init ();
 #ifdef USERPROG
   exception_init ();
   syscall_init ();
 #endif
  ...
 }
```

user program 초기화 과정을 확인하기 위해 먼저 tss_init() 함수와 gdt_init() 함수에 대해 알아보았다. 먼저, TSS의 구조체를 설명하자면, TSS는 "Task State Segment"를 정의하는 구조체로, kernel mode와 user mode 사이에서 context switch가 일어날 때 필요하다. 각 context 상태인 레지스터 값, 스택 포인터 등을 저장하고 복원하는 데 사용된다. 현재 사용되는 x86 OS에서는 이러한 TSS 기능이 거의 사용되지 않지만, user mode에서 interrupt가 발생하여 kernel mode로 전환될 때는 TSS가 사용된다.

아래 구조체의 각 변수와 포인터들을 확인해보면, 먼저 back_link는 이전 context의 TSS 세그먼트를 가리킨다. (esp0, ss0, esp1, ss1... 모르겠음??) eip는 context switch가 되고 난 다음 실행할 instruction을 가리키는 포인터이다. (eflags 모르겠음?? 플래그 상태?) 이후 프로세서의 레지스터 값(eax, ..., edi)을 저장한다. 이 값들은 나중에 다시 현재 context로 switch될 때 사용될 것이다. (segment selector, ldt, trace 모르겠음)

```
struct tss
   {
     uint16_t back_link, :16;
     void *esp0;                         /**< Ring 0 stack virtual address. */
     uint16_t ss0, :16;                  /**< Ring 0 stack segment selector. */
     void *esp1;
     uint16_t ss1, :16;
     void *esp2;
     uint16_t ss2, :16;
     uint32_t cr3;
     void (*eip) (void);
     uint32_t eflags;
     uint32_t eax, ecx, edx, ebx;
     uint32_t esp, ebp, esi, edi;
     uint16_t es, :16;
     uint16_t cs, :16;
     uint16_t ss, :16;
     uint16_t ds, :16;
     uint16_t fs, :16;
     uint16_t gs, :16;
     uint16_t ldt, :16;
     uint16_t trace, bitmap;
   };
```

pintos_init 함수에서 사용된 tss_init 함수는 위에서 소개한 TSS를 초기화하는 함수였다. 먼저 palloc을 사용해 TSS를 위한 메모리를 할당했다. 그리고 kernel mode에서 사용하는 stack segment selector인 ss0을 SEL_KDSEG로 초기화해주었다. SEL_KDSEG는 kernel data segment를 가리킨다고 한다. 그 다음, I/O 접근을 허용하거나 제한할 때 사용하는 bitmask인 bitmap을 0xdfff로 초기화해주었다. 이후 아래에서 설명한 tss_update 함수를 통해 현재 TSS의 esp0 값을 업데이트해주었다. esp0은 kernel mode stack pointer로, user mode에서 kernel mode로 전환될 때 사용할 stack 위치를 설정해준다. kernel mode에서는 stack이 위에서 아래로 쌓이기 때문에 esp0에는 현재 thread 주소에 PGSIZE를 더해 최상단 주소를 저장해주었다.

```
void
 tss_init (void) 
 {
   /* Our TSS is never used in a call gate or task gate, so only a
      few fields of it are ever referenced, and those are the only
      ones we initialize. */
   tss = palloc_get_page (PAL_ASSERT | PAL_ZERO);
   tss->ss0 = SEL_KDSEG;
   tss->bitmap = 0xdfff;
   tss_update ();
 }

 void
 tss_update (void) 
 {
   ASSERT (tss != NULL);
   tss->esp0 = (uint8_t *) thread_current () + PGSIZE;
 }
```

추가적으로, tss_get 함수는 현재 TSS의 포인터를 반환하는 역할을 하는 함수이다. 

```
struct tss *
 tss_get (void) 
 {
   ASSERT (tss != NULL);
   return tss;
 }
```

TSS를 초기화해준 다음 gdt를 초기화해주기 때문에, 이번에는 gdt에 대해서 알아보았다. GDT (Global Descriptor Table)는 code, data, 그리고 TSS로 구성된 메모리 세그먼트를 저장하는 테이블이다. 아래의 gdt_init 함수를 보면, 이후에 설명할 함수들을 이용해 kernel의 code, data 등을 테이블에 추가하는 것을 볼 수 있었다. 먼저 null을 넣는데, 이는 CPU가 잘못된 세그먼트를 참조할 때를 대비해 null을 가장 먼저 추가한 것이다. 그 다음으로, kernel code segment, kernel data segment, user code segment, user data segment, 마지막으로 TSS를 추가해주었다. 이후 해당 GDT의 크기와 주소를 저장하는 GDTR (GDT register) 값을 저장한 다음, 어셈블리 명령어를 통해 CPU가 GDT와 TSS를 알 수 있도록 했다. 이 함수는 아래에서 다시 설명할 것이다.

```
 static uint64_t gdt[SEL_CNT];

 /** Sets up a proper GDT.  The bootstrap loader's GDT didn't
    include user-mode selectors or a TSS, but we need both now. */
 void
 gdt_init (void)
 {
   uint64_t gdtr_operand;
  
   /* Initialize GDT. */
   gdt[SEL_NULL / sizeof *gdt] = 0;
   gdt[SEL_KCSEG / sizeof *gdt] = make_code_desc (0);
   gdt[SEL_KDSEG / sizeof *gdt] = make_data_desc (0);
   gdt[SEL_UCSEG / sizeof *gdt] = make_code_desc (3);
   gdt[SEL_UDSEG / sizeof *gdt] = make_data_desc (3);
   gdt[SEL_TSS / sizeof *gdt] = make_tss_desc (tss_get ());
  
   /* Load GDTR, TR.  See [IA32-v3a] 2.4.1 "Global Descriptor
      Table Register (GDTR)", 2.4.4 "Task Register (TR)", and
      6.2.4 "Task Register".  */
   gdtr_operand = make_gdtr_operand (sizeof gdt - 1, gdt);
   asm volatile ("lgdt %0" : : "m" (gdtr_operand));
   asm volatile ("ltr %w0" : : "q" (SEL_TSS));
 }
```

위의 설명에서 kernel/user code segment와 data segment를 정의하는 함수들로 make_seg_desc 함수를 사용해 GDT에 추가될 entry를 생성했다. 여기서 함수 인자로 dpl(descriptor privilege level)을 받아오는데, 이는 각각 kernel(0)과 user(3) mode를 확인하기 위한 것이다.

```
 static uint64_t
 make_code_desc (int dpl)
 {
   return make_seg_desc (0, 0xfffff, CLS_CODE_DATA, 10, dpl, GRAN_PAGE);
 }

 static uint64_t
 make_data_desc (int dpl)
 {
   return make_seg_desc (0, 0xfffff, CLS_CODE_DATA, 2, dpl, GRAN_PAGE);
 }
```

make_seg_desc 함수를 보면, 입력으로 받아오는 base, limit, class type, dpl, granularity를 GDT에 들어갈 64비트로 저장해 반환하는 역할을 했다. 32비트인 e0과 e1에 나눠서 저장한 다음, 둘을 합쳐 64비트로 반환했다. e0에는 limit의 하위 16비트와 base의 하위 16비트를 저장했고, e1에는 base의 중간 8비트, type, class, dpl, limit의 상위 4비트, granularity, 그리고 base의 상위 8비트를 저장했다. 여기서 말하는 type 은 segment 의 유형을 나타내는 변수로 read/write 이 가능한지 등을 알려준다. 그리고 class 는 segment 의 클래스로 code/data 인지 system 인지를 확인할 때 필요하다. 
(왜 32bit 로 두개로 나눠서 하는지 설명?? 수정 필요) 

```
 enum seg_class
   {
     CLS_SYSTEM = 0,             /**< System segment. */
     CLS_CODE_DATA = 1           /**< Code or data segment. */
   };

 enum seg_granularity
   {
     GRAN_BYTE = 0,              /**< Limit has 1-byte granularity. */
     GRAN_PAGE = 1               /**< Limit has 4 kB granularity. */
   };

 static uint64_t
 make_seg_desc (uint32_t base,
                uint32_t limit,
                enum seg_class class,
                int type,
                int dpl,
                enum seg_granularity granularity)
 {
   uint32_t e0, e1;
  
   ASSERT (limit <= 0xfffff);
   ASSERT (class == CLS_SYSTEM || class == CLS_CODE_DATA);
   ASSERT (type >= 0 && type <= 15);
   ASSERT (dpl >= 0 && dpl <= 3);
   ASSERT (granularity == GRAN_BYTE || granularity == GRAN_PAGE);
  
   e0 = ((limit & 0xffff)             /**< Limit 15:0. */
         | (base << 16));             /**< Base 15:0. */
  
   e1 = (((base >> 16) & 0xff)        /**< Base 23:16. */
         | (type << 8)                /**< Segment type. */
         | (class << 12)              /**< 0=system, 1=code/data. */
         | (dpl << 13)                /**< Descriptor privilege. */
         | (1 << 15)                  /**< Present. */
         | (limit & 0xf0000)          /**< Limit 16:19. */
         | (1 << 22)                  /**< 32-bit segment. */
         | (granularity << 23)        /**< Byte/page granularity. */
         | (base & 0xff000000));      /**< Base 31:24. */
  
   return e0 | ((uint64_t) e1 << 32);
 }
```

그리고 TSS를 GDT에 넣을 때, 아래의 make_tss_desc 함수를 통해 추가했다. 이 함수에서도 위에서 설명한 make_seg_desc 함수를 통해 entry를 생성했다.

```
 static uint64_t
 make_tss_desc (void *laddr)
 {
   return make_seg_desc ((uint32_t) laddr, 0x67, CLS_SYSTEM, 9, 0, GRAN_BYTE);
 }
```

make_gdtr_operand 함수는 GDT register 를 가져오는 함수로 GDT 의 크기인 limit 과 시작 주소인 base 를 반환해준다. 

```
 static uint64_t
 make_gdtr_operand (uint16_t limit, void *base)
 {
   return limit | ((uint64_t) (uint32_t) base << 16);
 }
```

(수정 필요: 그래서 processor execution 순서어떻게 되는지 설명? 위에는 그냥 다 initiation 밖에 없는것 같은데..?) 

### System call 

(수정 필요: /thread/interrupt.c 파일 내의 함수 추가 필요..??) 

초반에 설명한 Pintos의 main entry point인 pintos_init 함수에 나와 있는 exception_init()과 syscall_init() 함수에 대해 알아보았다. 먼저, 아래의 syscall_init 함수를 보면 intr_register_init 함수를 호출해 syscall interrupt를 등록했다. 그리고 intr_register_int 함수를 호출했다.

```
 void
 syscall_init (void) 
 {
   intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
 }
```

intr_register_init 함수는 vector number, dpl, level, handler 포인터, 그리고 이름을 argument로 받아온 다음, 유효한 벡터 값인지 확인한 후 다시 register_handler 함수로 변수들을 넘겨주었다. 이 intr_register_init 함수는 아래에서 설명할 exception_init에서도 사용된다.

register_handler에서는 interrupt 혹은 exception이 발생했을 때, 각 vector number에 맞는 interrupt/exception handler를 IDT (Interrupt Descriptor Table)에 등록하는 함수였다. 만약 interrupt가 활성화된 상태에서 처리해야 한다면 make_trap_gate 함수를 통해 IDT 엔트리를 설정하고, 그렇지 않다면 make_intr_gate 함수를 통해 CPU가 interrupt를 비활성화한 상태에서 handler를 실행한다.

```
 void
 intr_register_int (uint8_t vec_no, int dpl, enum intr_level level,
                    intr_handler_func *handler, const char *name)
 {
   ASSERT (vec_no < 0x20 || vec_no > 0x2f);
   register_handler (vec_no, dpl, level, handler, name);
 }

static void
 register_handler (uint8_t vec_no, int dpl, enum intr_level level,
                   intr_handler_func *handler, const char *name)
 {
   ASSERT (intr_handlers[vec_no] == NULL);
   if (level == INTR_ON)
     idt[vec_no] = make_trap_gate (intr_stubs[vec_no], dpl);
   else
     idt[vec_no] = make_intr_gate (intr_stubs[vec_no], dpl);
   intr_handlers[vec_no] = handler;
   intr_names[vec_no] = name;
 }
```

위에서 설명했듯이, make_trap_gate와 make_intr_gate는 interrupt 활성화/비활성화의 차이를 가진다. 하지만 내부 로직을 보면, 두 함수 모두 make_gate 함수를 호출한다. make_gate 함수는 위에서 넘겨준 인자들로 64비트인 gate descriptor를 생성해 반환해준다. 이 descriptor는 IDT에 추가될 것이다.

```
 static uint64_t
 make_trap_gate (void (*function) (void), int dpl)
 {
   return make_gate (function, dpl, 15);
 }

 static uint64_t
 make_intr_gate (void (*function) (void), int dpl)
 {
   return make_gate (function, dpl, 14);
 }

 static uint64_t
 make_gate (void (*function) (void), int dpl, int type)
 {
   uint32_t e0, e1;
  
   ASSERT (function != NULL);
   ASSERT (dpl >= 0 && dpl <= 3);
   ASSERT (type >= 0 && type <= 15);
  
   e0 = (((uint32_t) function & 0xffff)     /**< Offset 15:0. */
         | (SEL_KCSEG << 16));              /**< Target code segment. */
  
   e1 = (((uint32_t) function & 0xffff0000) /**< Offset 31:16. */
         | (1 << 15)                        /**< Present. */
         | ((uint32_t) dpl << 13)           /**< Descriptor privilege level. */
         | (0 << 12)                        /**< System. */
         | ((uint32_t) type << 8));         /**< Gate type. */
  
   return e0 | ((uint64_t) e1 << 32);
 }
```

Pintos에서 system call이 발생하면, 아래의 syscall_handler 함수가 실행된다. 현재 함수를 보면, 단순히 system call이 발생했다는 것을 알리기 위해 printf를 사용해 메시지를 출력하고, thread_exit 함수를 이용해 해당 thread를 종료해주었다.

```
 static void
 syscall_handler (struct intr_frame *f UNUSED) 
 {
   printf ("system call!\n");
   thread_exit ();
 }
```

아래의 exception_init 함수를 보면, 위에서 설명한 intr_register_init 함수를 이용해 각 exception을 처리하는 handler들을 IDT에 추가하는 역할을 한다. 

```
void
 exception_init (void) 
 {
   /* These exceptions can be raised explicitly by a user program,
      e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
      we set DPL==3, meaning that user programs are allowed to
      invoke them via these instructions. */
   intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
   intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
   intr_register_int (5, 3, INTR_ON, kill,
                      "#BR BOUND Range Exceeded Exception");
  
   /* These exceptions have DPL==0, preventing user processes from
      invoking them via the INT instruction.  They can still be
      caused indirectly, e.g. #DE can be caused by dividing by
      0.  */
   intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
   intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
   intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
   intr_register_int (7, 0, INTR_ON, kill,
                      "#NM Device Not Available Exception");
   intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
   intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
   intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
   intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
   intr_register_int (19, 0, INTR_ON, kill,
                      "#XF SIMD Floating-Point Exception");
  
   /* Most exceptions can be handled with interrupts turned on.
      We need to disable interrupts for page faults because the
      fault address is stored in CR2 and needs to be preserved. */
   intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
 }
```

(수정 필요: intr-stubs.S라는 어셈블리 파일 설명 적어야됨) 

```
 #ifndef THREADS_INTR_STUBS_H
 #define THREADS_INTR_STUBS_H
  
 /** Interrupt stubs.
  
    These are little snippets of code in intr-stubs.S, one for
    each of the 256 possible x86 interrupts.  Each one does a
    little bit of stack manipulation, then jumps to intr_entry().
    See intr-stubs.S for more information.
  
    This array points to each of the interrupt stub entry points
    so that intr_init() can easily find them. */
 typedef void intr_stub_func (void);
 extern intr_stub_func *intr_stubs[256];
  
 /** Interrupt return path. */
 void intr_exit (void);
  
 #endif /**< threads/intr-stubs.h */
```

structure(file, inode), functions(need to implement system call) of the file system (나중에 지우기)
(“filesys/ file.c”, “filesys/ inode.c” “filesys/filesys.c”)(나중에 지우기)


## Design Implementation 
how to solve problems (나중에 지우기)
data structure and detailed algorithm (나중에 지우기)

1. process termination messages
2. argument passing
3. system call
   -> syscall_handler 함수 수정? 
4. denying writes to executables 
