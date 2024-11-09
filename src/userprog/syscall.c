#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h" //for halt 
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"

static void syscall_handler (struct intr_frame *);
struct lock file_system_lock; 

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
  lock_init(&file_system_lock); 
}

void 
stack_arguments(int* esp, int* arg, int count){
  int first = esp + 1; 
  for (int i = 0, i < count; i++){
    if(verify_thread_memory(first + i)){
      arg[i] = *(first + i)
    }
  }
}

void initialize_argv(int *argv, int size) {
    for (int i = 0; i < size; i++) {
        argv[i] = 0;  // 초기값으로 0을 설정
    }
}

/********** syscall helper functions (13) **********/
// 1. halt -- call shutdown_power_off() function (pintos document)

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
  currnet -> process_manager.exit_syscall_num = status; 
  // semaphore 처리? 
  printf ("%s: exit(%d)\n", current->name, status);
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
  if(file == NULL || !verify_thread_memory(file))
    shf_exit(-1); 

  return filesys_create (file, initial_size); 
}

// 6. remove
bool 
shf_remove (const char *file)
{
  if(file == NULL || !verify_thread_memory(file))
    shf_exit(-1); 

  return filesys_remove (file, initial_size); 
}

// 7. open
int 
shf_open (const char *file)
{
  if (file == NULL || !verify_thread_memory(file))
    shf_exit(-1); 

  struct thread* current = thread_current(); 
  int *current_directory_num = current->process_manager.file_directory_num; 

  struct file* new_file; 
  new_file = filesys_open(file); 

  if(new_file == NULL){
    return -1; 
  }

  current->process_manager->file_directory_table[*current_directory_num] = new_file; 

  (*current_directory_num) ++; 

  return *current_directory_num;  
}

// 8. filesize

int 
shf_filesize (int fd)
{
  struct thread* current = thread_current(); 

  struct file* current_file = current -> process_manager ->file_directory_table[fd]; 

  if(current_file == NULL){
    return -1; 
  }

  return file_length(current_file); 
}

// 9. read
int 
shf_read (int fd, void *buffer, unsigned size)
{
  if(!verify_thread_memory(fd) || !verify_thread_memory(buffer)){
    sys_exit(-1); 
  }

  struct thread* current = thread_current(); 

  struct file* current_file = current -> process_manager ->file_directory_table[fd]; 

  if(current_file == NULL)
    sys_exit(-1); 

  int result = 0; 

  lock_acquire(&file_system_lock); 
  result = file_read(current_file, buffer, size); 
  lock_release(&file_system_lock); 

  return result; 
}

// 10. write
int 
shf_write (int fd, const void *buffer, unsigned size)
{
  // buffer -> fd 에 저장 (size 만큼)
  // return written 된 byte 수 만큼 
  // written 할 수 없으면 0 return 
  // 원래는 write 한 만큼 늘어나야하는데 현재 filesys 는 늘어나는게 구현 X

  if(!verify_thread_memory(fd) || !verify_thread_memory(buffer)){
    shf_exit(-1); 
  }

  struct thread* current = thread_current(); 

  int current_directory_num = current -> process_manager.file_directory_num; 
  
  if(fd == 1){
    lock_acquire(&file_system_lock); 
    putbuf(buffer,size); 
    lock_release(&file_system_lock); 
    return size; 
  }else if (1 < fd || fd < current_directory_num){
    struct file* current_file = current -> process_manager ->file_directory_table[fd]; 
    if(current_file == NULL){
      return -1; 
    }

    int written_bytes = 0; 
    lock_acquire(&file_system_lock); 
    written_bytes = file_write(current_file,buffer,size); 
    lock_release(&file_system_lock); 

    return written_bytes; 
  }else{
    shf_exit(-1); 
  }
  return -1;
}

// 11. seek
void 
shf_seek (int fd, unsigned position)
{
  struct thread* current = thread_current(); 
  struct file* current_file = current -> process_manager ->file_directory_table[fd];
  if(current_file == NULL){
    return; 
  }
  file_seek(current_file, position); 
}

// 12. tell
unsigned 
shf_tell (int fd)
{
  struct thread* current = thread_current(); 
  struct file* current_file = current -> process_manager ->file_directory_table[fd];
  if(current_file == NULL){
    return -1; 
  }
  return file_tell(current_file); 
}

// 13. close

/*
(pintos document description)
Exiting or terminating a process implicitly closes all its open 
file descriptors, as if by calling this function for each one.
*/
void 
shf_close (int fd)
{
  struct thread* current = thread_current(); 
  struct file* current_file = current -> process_manager ->file_directory_table[fd];

  file_close(current_file); 
  current -> process_manager ->file_directory_table[fd] = NULL; 

  for(int i = fd; i < current -> process_manager ->file_directory_num; i++){
    current -> process_manager ->file_directory_table[i] = current -> process_manager ->file_directory_table[i + 1];  
  }

  current -> process_manager.file_directory_num --; 
}

/***************************************************/


static void
syscall_handler (struct intr_frame *f) 
{
  if(verify_thread_memory(f->esp) == false){
    shf_exit(-1);
  }

  int argv[3];
  initialize_argv(argv, 3);


  //thread_current()->esp
  switch (*(uint32_t *)(f->esp))
  {
  case SYS_HALT:
    shf_halt(); 
    break;
  
  case SYS_EXIT:
    // print exit message here ? 
    stack_arguments(f->esp, &argv, 1);  
    shf_exit(argv[0]); 
    break;
  
  case SYS_EXEC:
    stack_arguments(f->esp, &argv, 1);  
    f->eax = shf_exec((const char*)argv[0]);  //pid_t return
    break;
  
  case SYS_WAIT:
    stack_arguments(f->esp, &argv, 1);  
    f->eax = shf_wait((pid_t)argv[0]); // int return 
    break;
  
  case SYS_CREATE:
    stack_arguments(f->esp, &argv, 2);  
    f -> eax = shf_create ((const char*)argv[0], (unsigned)argv[1]); //bool 
    break;
  
  case SYS_REMOVE:
    stack_arguments(f->esp, &argv, 1);  
    f->eax = shf_remove ((const char*)argv[0]); //bool 
    break;
  
  case SYS_OPEN:
    stack_arguments(f->esp, &argv, 1);  
    f->eax = shf_open ((const char*)argv[0]); //int 
    break;
  
  case SYS_FILESIZE:
    stack_arguments(f->esp, &argv, 1);  
    f->eax = shf_filesize (argv[0]); // int 
    break;
  
  case SYS_READ:
    stack_arguments(f->esp, &argv, 3);  
    f->eax = shf_read (argv[0], (void*)argv[1], (unsigned)argv[2]);  //int 
    break;
  
  case SYS_WRITE:
    stack_arguments(f->esp, &argv, 3);  
    f->eax = shf_write (argv[0], (const void*) argv[1], (unsigned) argv[2]); //int 
    break;
  
  case SYS_SEEK:
    stack_arguments(f->esp, &argv, 2);  
    shf_seek (argv[0], (unsigned)argv[1]); 
    break;
  
  case SYS_TELL:
    stack_arguments(f->esp, &argv, 1);  
    f->eax = shf_tell (argv[0]); // unsigned 
    break;
  
  case SYS_CLOSE:
    stack_arguments(f->esp, &argv, 1);  
    shf_close (argv[0]); 
    break;
  
  default:
    // same as SYS_HALT
    shf_halt();
    break;
  }

}
