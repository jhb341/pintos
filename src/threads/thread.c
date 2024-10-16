#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

/* global variable for mlfqs */
//int load_avg; // initialized at `thread_start`
int load_avg;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/*****************/
static struct list blocked_list;

void
thread_sleep(int64_t ticks)
{
  struct thread *t;
  enum intr_level old_level;

  old_level = intr_disable();
  t = thread_current();

  ASSERT(t != idle_thread);

  t->wakeup_tick = ticks;
  list_push_back(&blocked_list, &t->elem);
  thread_block();

  intr_set_level(old_level);
}

void
thread_awake(int64_t ticks)
{
  struct list_elem *e = list_begin (&blocked_list);

  while (e != list_end(&blocked_list)){
    struct thread *t = list_entry(e, struct thread, elem);
    if(t->wakeup_tick <= ticks){
      e = list_remove(e);
      thread_unblock(t);
      swap_running_thread();
    }else{
      e = list_next(e);
    }
  }

}

bool
cmp_pri(struct list_elem *a, struct list_elem *b, void *aux UNUSED)
{
  struct thread *x = list_entry(a, struct thread, elem);
  struct thread *y = list_entry(b, struct thread, elem);
  return x->priority > y->priority;
}


bool
is_running_valid(void)
{
  bool flag = (thread_current() == idle_thread) || list_empty(&ready_list);
  return flag;
}

void
swap_running_thread_helper(void)
{
  struct thread *c = thread_current();
  struct thread *r = list_entry(list_front(&ready_list), struct thread, elem);
  if( !intr_context() && c->priority < r->priority){
    thread_yield();
  }
}

void
swap_running_thread(void)
{
  if(is_running_valid()){
    return;
  }else{
    swap_running_thread_helper();
  }
}


bool
cmp_inheritor_pri(struct list_elem *x, struct list_elem *y, void *aux UNUSED)
{
  return list_entry(x, struct thread, manager.inheritor)->priority > list_entry(y, struct thread, manager.inheritor)->priority;
}

void
inherit_pri(void)
{
  int i;
  struct thread *t = thread_current ();

  for (i = 0; i < 8; i++){
    if (!t->manager.target_lock) break;
      struct thread *holder = t->manager.target_lock->holder;
      holder->priority = t->priority;
      t = holder;
  }
}

void
release_helper(struct lock *l)
{
  struct list_elem *e;
  struct thread *t = thread_current();

  for (e = list_begin (&t->manager.inheritor_list); e != list_end (&t->manager.inheritor_list); e = list_next (e)){
    struct thread *t = list_entry (e, struct thread, manager.inheritor);
    if (t->manager.target_lock == l)
      list_remove (&t->manager.inheritor);
  }
}

void
restore_pri(void)
{
  struct thread *t = thread_current();
  t->priority = t->manager.original_pri;

  if (!list_empty (&t->manager.inheritor_list)) {
    list_sort (&t->manager.inheritor_list, cmp_inheritor_pri, 0);

    struct thread *nxt = list_entry (list_front (&t->manager.inheritor_list), struct thread, manager.inheritor);
    if (nxt->priority > t->priority)
      t->priority = nxt->priority;
  }
}

/*****************/

//////////////////////////////////////////////
//                                          //
//      mlfqs, floating point operator      //
//                                          //
//////////////////////////////////////////////
int 
calc_n_to_fp(int a, int b)
{
  (void)b;
  return F * a;
}

int 
calc_x_to_int_z(int a, int b)
{
  (void)b;
  return a / F;
}

int 
calc_x_to_int_n(int a, int b)
{
  (void)b;
  if(a >= 0){
    return (a + (F/ 2)) / F;
  }else{
    return (a - (F / 2)) / F;
  }
}

int 
calc_add_x_y(int a, int b)
{
  return a + b;
}

int 
calc_add_x_n(int a, int b)
{
  return a + b*F;
}

int 
calc_sub_y_x(int a, int b)
{
  return a - b;
}

int 
calc_sub_n_x(int a, int b)
{
  return a - b*F;
}

int 
calc_mul_x_n(int a, int b)
{
  return a * b;
}

int 
calc_mul_x_y(int a, int b)
{
  int result;
  result = (((int64_t) a) * b) / F;
  return result;
}

int 
calc_div_x_n(int a, int b)
{
  return a / b;
}

int 
calc_div_x_y(int a, int b)
{
  int result;
  result = (((int64_t) a) * F) / b;
  return result;
}

binary_operation get_operation(enum calc_mode mode) {
    switch (mode) {
        case CONV_N_TO_FP:
            return calc_n_to_fp;
        case CONV_X_TO_INT_z:
            return calc_x_to_int_z;
        case CONV_X_TO_INT_n:
            return calc_x_to_int_n;
        case ADD_X_AND_Y:
            return calc_add_x_y;
        case ADD_X_AND_N:
            return calc_add_x_n;
        case SUB_Y_FROM_X:
            return calc_sub_y_x;
        case SUB_N_FROM_X:
            return calc_sub_n_x;
        case MUL_X_BY_N:
            return calc_mul_x_n;
        case MUL_X_BY_Y: 
            return calc_mul_x_y;
        case DIV_X_BY_N:
            return calc_div_x_n;
        case DIV_X_BY_Y: 
            return calc_div_x_y;
        default:    
            return NULL;
    }
}

int
do_fp_calc(int x, int a, enum calc_mode mode)
{
  binary_operation operation = get_operation(mode);
  if(operation != NULL){
    return operation(x,a);
  }else{
    return 0;
  }
}


void 
mlfqs_calc_pri(struct thread *t)
{
  if(t == idle_thread){return;}
  int calced_pri;
  int u,v,x;
  
  u = do_fp_calc((t->recent_cpu),-4,DIV_X_BY_N);
  v = PRI_MAX - 2 * (t->nice);
  x = do_fp_calc(u,v,ADD_X_AND_N);
  calced_pri = do_fp_calc(x,0,CONV_X_TO_INT_z);

  t->priority = calced_pri;
}

void
mlfqs_calc_pri2(void)
{
  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e)) {
    struct thread *t = list_entry (e, struct thread, allelem);
    mlfqs_calc_pri (t);
  }
}

void mlfqs_calc_cpu(struct thread *t)
{
  if(t == idle_thread){return;}
  int calced_cpu;
  int u,v,w,x,y;

  u = do_fp_calc(load_avg ,2,MUL_X_BY_N);
  y = do_fp_calc(load_avg,2,MUL_X_BY_N);
  v = do_fp_calc(y,1,ADD_X_AND_N);
  w = do_fp_calc(u,v,DIV_X_BY_Y);
  x = do_fp_calc(w,t->recent_cpu,MUL_X_BY_Y);
  calced_cpu = do_fp_calc(x,t->nice ,ADD_X_AND_N);

  t->recent_cpu = calced_cpu;
}

void
mlfqs_calc_cpu2(void)
{
  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e)) {
    struct thread *t = list_entry (e, struct thread, allelem);
    mlfqs_calc_cpu (t);
  }

}


void
mlfqs_incr_cpu(void)
{
  if(thread_current() != idle_thread){
    thread_current ()-> recent_cpu = do_fp_calc(thread_current()->recent_cpu,1,ADD_X_AND_N);
  }
}

void
mlfqs_calc_ld(void)
{
  int num_t;
  int ld;
  if(thread_current() == idle_thread){
    num_t = list_size(&ready_list);
  }else{
    num_t = 1 + list_size(&ready_list);
  }

  int u, v, w, x, y;

  u = do_fp_calc(59,0,CONV_N_TO_FP);
  v = do_fp_calc(60,0,CONV_N_TO_FP);
  w = do_fp_calc(u,v,DIV_X_BY_Y);
  x = do_fp_calc(w,load_avg,MUL_X_BY_Y);

  u = do_fp_calc(1,0,CONV_N_TO_FP);
  //v = do_fp_calc(60,0,CONV_N_TO_FP);
  w = do_fp_calc(u,v,DIV_X_BY_Y);
  y = do_fp_calc(w,num_t,MUL_X_BY_N);

  ld = do_fp_calc(x,y,ADD_X_AND_Y);

  load_avg = ld;
}


/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&blocked_list);

  

  //inherit_manager_init();

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
  
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);
  //load_avg = LOAD_AVG_DEFAULT;
  load_avg = LOAD_AVG_DEFAULT;

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);
  swap_running_thread();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  //list_push_back (&ready_list, &t->elem);
  list_insert_ordered(&ready_list, &t->elem, cmp_pri, 0);

  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    //list_push_back (&ready_list, &cur->elem);
    list_insert_ordered(&ready_list, &cur->elem, cmp_pri, 0);

  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  if(thread_mlfqs){return;}

  thread_current ()->priority = new_priority;
  thread_current ()->manager.original_pri = new_priority;
  restore_pri();  
  swap_running_thread();
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  /* Not yet implemented. */
  enum intr_level old_level = intr_disable ();
  thread_current ()->nice = nice;
  mlfqs_calc_pri(thread_current());
  swap_running_thread ();
  intr_set_level (old_level);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  /* Not yet implemented. */
  enum intr_level old_level = intr_disable ();
  int nice = thread_current ()-> nice;
  intr_set_level (old_level);
  return nice;
}


/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  /* Not yet implemented. */
  enum intr_level old_level = intr_disable ();
  int load_avg_value = do_fp_calc(do_fp_calc(load_avg,100,MUL_X_BY_N),0,CONV_X_TO_INT_n);

  intr_set_level (old_level);
  return load_avg_value;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  /* Not yet implemented. */
  enum intr_level old_level = intr_disable ();
  int recent_cpu = do_fp_calc(do_fp_calc(thread_current ()->recent_cpu,100,MUL_X_BY_N),0,CONV_X_TO_INT_n);
  intr_set_level (old_level);
  return recent_cpu;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;


  /*  init manager */
  t->manager.original_pri = priority;
  t->manager.target_lock = NULL;
  list_init(&(t->manager.inheritor_list));

  /* init manager2 */
  //t->recent_cpu = RECENT_CPU_DEFAULT;
  //t->nice = NICE_DEFAULT;
  t->nice = NICE_DEFAULT;
  t->recent_cpu = RECENT_CPU_DEFAULT;


  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);