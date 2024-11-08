#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "synch.h"
//#include "userprog/process.h"

#define USERPROG



/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */
struct inherit_manager
{
   int original_pri; /* 원래 스레드의 pri */
   int current_pri;  /* 현재 상속받아 임시로 부여된 pri */
   struct list inheritor_list; /* 나한테 상속해준 스레드 연결리스트 */

   struct list_elem inheritor; /* 내가 상속해주는 스레드의 상속리스트에 적을 이름표 */
   struct lock *target_lock; /* 내가 대기중인 lock */

};

struct user_process_manager
{
   int exit_syscall_num;               /* syscall중 exit시의 number */
   struct file *exc_file;              /* process가 실행하는 file */
   struct thread *my_parent;           /* 내 부모 프로세스 */
   struct list my_child_list;          /* 내 자식 프로세스들의 리스트 */
   struct list_elem child_list_elem;   /* 내가 자식으로 있는 부모 프로세스의 자식 리스트에 연결할 대기표 */
   struct semaphore sema_wait;         /* ? */
   struct semaphore sema_load;         /* 내가 load하는 동안 내 부모가 안꺼지게 하는 세마포 */
};
            
/* Values for Advance Scheduler */               
#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0
struct mlfqs_manager
{
   int nice;
   int cpu;
};

enum calc_mode
{
   CONV_N_TO_FP,        /* Convert n to fixed point */
   CONV_X_TO_INT_z,     /* Convert x to integer(rounding toward 0) */
   CONV_X_TO_INT_n,     /* Convert x to integer(rounding to nearest) */
   ADD_X_AND_Y,         /* x + y */
   ADD_X_AND_N,         /* x + n*f */
   SUB_Y_FROM_X,        /* x - y */
   SUB_N_FROM_X,        /* x - n*f */
   MUL_X_BY_N,          /* x*n */
   MUL_X_BY_Y,          
   DIV_X_BY_N,
   DIV_X_BY_Y
};

// 이항 연산 함수 타입 정의
typedef int (*binary_operation)(int, int);
#define F (1<<14)
#define INT_MAX ((1<<31)-1)
#define INT_MIN (-(1<<31))
//

int calc_n_to_fp(int a, int b);
int calc_x_to_int_z(int a, int b);
int calc_x_to_int_n(int a, int b);
int calc_add_x_y(int a, int b);
int calc_add_x_n(int a, int b);
int calc_sub_y_x(int a, int b);
int calc_sub_n_x(int a, int b);
int calc_mul_x_n(int a, int b);
int calc_mul_x_y(int a, int b);
int calc_div_x_n(int a, int b);
int calc_div_x_y(int a, int b);

binary_operation get_operation(enum calc_mode mode);

int do_fp_calc(int x, int a, enum calc_mode mode);


//void manager_init_helper(struct thread *t, struct inherit_manager *m);



/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {

   /*************************/
   int64_t wakeup_tick;
   
   struct inherit_manager manager;
   //struct mlfqs_manager manager2;

   int nice;
   int recent_cpu;
   /*************************/

    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

    struct user_process_manager process_manager;
#ifdef USERPROG
   /* Owned by userprog/process.c. */
   uint32_t *pagedir;                  
   /* Page directory. */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

//

void mlfqs_calc_pri(struct thread *t);
void mlfqs_calc_pri2(void);
void mlfqs_calc_cpu(struct thread *t);
void mlfqs_calc_cpu2(void);
void mlfqs_incr_cpu(void);
void mlfqs_calc_ld(void);

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);


/*****************/
void thread_sleep(int64_t ticks);
void thread_awake(int64_t ticks);
bool cmp_pri(struct list_elem *a, struct list_elem *b, void *aux UNUSED);
bool is_running_valid(void);
void swap_running_thread_helper(void);
void swap_running_thread(void);
bool cmp_inheritor_pri(struct list_elem *x, struct list_elem *y, void *aux);
void inherit_pri(void);
void release_helper(struct lock *l);
void restore_pri(void);

#endif /* threads/thread.h */