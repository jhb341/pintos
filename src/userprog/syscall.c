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
void 
shf_halt()
{
  shutdown_power_off(); 
}

// 2. exit -- termincate current user program, return status to the kernel 
void
shf_exit(int status)
{
  struct thread *current = thread_current(); 
  currnet -> process_manager -> exit_syscall_num = status; 
  // semaphore 처리? 
  thread_exit(); 
}

// 3. exec -- cmd_line 에서 주어진 executable 실행 ? return new processes program id (invalid program id 일 경우 -1)
pid_t
shf_exec(const char* cmd_line)
{
  pid_t processID = process_execute(cmd_line); 

  return processID; 
}

// 4. wait -- wait for child process to finish, 
int 
shf_wait(pid_t pid)
{
  return process_wait (pid) 
}

// 5. create
//file 관련 함수 - src/filesys/filesys.c 참고 

bool 
shf_create (const char *file, unsigned initial_size)
{
  //create file with "initial_size" size 
  if(file == NULL)
    shf_exit(-1); 

  return filesys_create (file, initial_size); 
}

// 6. remove
bool 
shf_remove (const char *file)
{
  if(file == NULL)
    shf_exit(-1); 

  return filesys_remove (file, initial_size); 
}

// 7. open
int 
shf_open (const char *file)
{

}

// 8. filesize
int 
shf_filesize (int fd)
{

}

// 9. read
int 
shf_read (int fd, void *buffer, unsigned size)
{

}

// 10. write
int 
shf_write (int fd, const void *buffer, unsigned size)
{

}

// 11. seek
void 
shf_seek (int fd, unsigned position)
{

}

// 12. tell
unsigned 
shf_tell (int fd)
{

}

// 13. close
void 
shf_close (int fd)
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
