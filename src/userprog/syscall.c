#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

bool
verify_thread_memory(void *addr)
{
  bool flag = (addr != 0) && (0x8048000 <= addr) && (addr < 0xc0000000);
  return flag; // addr가 스레드 스택에 해당하는 주소일때 true 반환
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/********** syscall helper functions (13) **********/
// 1. halt
shf_halt()
{

}

// 2. exit
shf_exit(int exit_code)
{

}

// 3. exec
shf_exec()
{

}

// 4. wait
shf_wait()
{

}

// 5. create
shf_create()
{

}

// 6. remove
shf_remove()
{

}

// 7. open
shf_open()
{

}

// 8. filesize
shf_filesize()
{

}

// 9. read
shf_read()
{

}

// 10. write
shf_write()
{

}

// 11. seek
shf_seek()
{

}

// 12. tell
shf_tell()
{

}

// 13. close
shf_close()
{

}

/***************************************************/

static void
syscall_handler (struct intr_frame *f) 
{
  if(verify_thread_memory(f->esp) == false){
    shf_exit(-1);
  }


  //thread_current()->esp
  switch (*(uint32_t *)(f->esp))
  {
  case SYS_HALT:
    /* Code HERE! */
    break;
  
  case SYS_EXIT:
    break;
  
  case SYS_EXEC:
    break;
  
  case SYS_WAIT:
    break;
  
  case SYS_CREATE:
    break;
  
  case SYS_REMOVE:
    break;
  
  case SYS_OPEN:
    break;
  
  case SYS_FILESIZE:
    break;
  
  case SYS_READ:
    break;
  
  case SYS_WRITE:
    break;
  
  case SYS_SEEK:
    break;
  
  case SYS_TELL:
    break;
  
  case SYS_CLOSE:
    break;
  
  default:
    // same as SYS_HALT
    shf_halt();
    break;
  }

}
