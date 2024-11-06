#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  thread_exit (); /* 종료 문구는 여기서 출력 한다. */

  /* 실제 syscall의 처리는 여기에서 이루어짐 */
  /* 현재는 모든 syscall의 처리가 종료로 이루어짐. */

  /*
  switch(){
    case SYS_EXIT:
      // 여기에서 종료 문구 출력
  }
  */


}
