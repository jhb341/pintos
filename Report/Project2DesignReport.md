# CSED 312 Project 1 Design Report 
김지현(20220302) \
전현빈(20220259)

## Analyze the Current Implementation 
procedure of process execution - thread init.c and userprog process.c (나중에 지우기)

### 1. Process execution 
핀토스가 실행되고 나서 user program 이 어떻게 시작되는지 확인하기 위해서 pintos main entry point 를 확인해보았다. 아래의 코드를 보면 #ifdef USERPROG 부분을 보면 USERPROG 매크로가 정의되어 있는 경우에만 user program과 관련된 초기화 코드가 컴파일된다. 

```
int
 pintos_init (void)
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

user program 초기화 과정을 확인하기 위해 먼저 tss_init() 함수와 gdt_init() 함수에 대해서 알아볼 것이다. 먼저, tss 의 구조체를 설명하자면, tss 는 "task state segment 를 정의하는 구조체로, kernel mode 와 user mode 사이에 context switch 할 때 필요하다. (설명 더 필요) 

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
```


```
struct tss *
 tss_get (void) 
 {
   ASSERT (tss != NULL);
   return tss;
 }
```

```
 void
 tss_update (void) 
 {
   ASSERT (tss != NULL);
   tss->esp0 = (uint8_t *) thread_current () + PGSIZE;
 }
```

how to call syscall_handler() in userprog syscall.c(나중에 지우기)
“lib/user/syscall.c”, “threads/intr-stubs.S”, “threads/interrupt.c”(나중에 지우기)

초반에 설명한 pintos의 main entry point 인 pintos_init 함수에 나와있는 exeception_init()과 syscall_init() 함수에 대해서 알아볼 것이다. 

structure(file, inode), functions(need to implement system call) of the file system (나중에 지우기)
(“filesys/ file.c”, “filesys/ inode.c” “filesys/filesys.c”)(나중에 지우기)


## Design Implementation 
how to solve problems (나중에 지우기)
data structure and detailed algorithm (나중에 지우기)

1. process termination messages
2. argument passing
3. system call
4. denying writes to executables 
