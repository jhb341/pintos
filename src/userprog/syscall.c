// userprog
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"

#include <stdio.h>
#include <syscall-nr.h>

// threads
#include "threads/malloc.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

// filesys
#include "filesys/filesys.h"
#include "filesys/file.h"

#include "vm/page.h"

static void syscall_handler(struct intr_frame *);
struct lock FileLock; // make file access ATOMIC

void syscall_init(void)
{
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&FileLock);
}




bool verify_mem_address(void *addr)
{
  return addr >= (void *)0x08048000 && addr < (void *)0xc0000000 ;
}

/*
void exit_mem(const char *file)
{
  if(!verify_mem_address(file)){
    sys_exit(-1);
  }else{
    // nothing to do..
  }
}
*/

void getArgs(void *esp, int *arg, int count)
{
  for (int i = 0; i < count; i++)
  {
    //exit_mem(esp + 4 * i);
  if(!verify_mem_address(esp + 4 * i)){
    sys_exit(-1);
    }


    arg[i] = *(int *)(esp + 4 * i);
  }
}


void sys_halt(void)
{
  shutdown_power_off();
}

/* complete task1 */
void sys_exit(int status)
{
  thread_current()->exitCode = status;
  printf("%s: exit(%d)\n", thread_name(), status);
  thread_exit();
}

pid_t sys_exec(const char *file)
{
//exit_mem(file);

  if(!verify_mem_address(file)){
    sys_exit(-1);
  }


pid_t pid = process_execute(file);
if (pid == -1) return -1;

struct thread *child = getChild(pid);
sema_down(&child->semaExec);

return (child->isLoad) ? pid : -1;
}

int sys_wait(pid_t pid)
{
  return process_wait(pid);
}

bool sys_create(const char *file, unsigned initial_size)
{
  if(!verify_mem_address(file)||file==NULL)
  {
    sys_exit(-1);
  }
  return filesys_create(file, initial_size);
}

bool sys_remove(const char *file)
{
  //exit_mem(file);

    if(!verify_mem_address(file)){
    sys_exit(-1);
  }

  
  return filesys_remove(file);
}

int sys_open(const char *file)
{
//exit_mem(file);
  if(!verify_mem_address(file)){
    sys_exit(-1);
  }

lock_acquire(&FileLock);
struct file *f = filesys_open(file);

if (f == NULL) {
    lock_release(&FileLock);
    return -1;
}

if (!strcmp(thread_current()->name, file)) {
    file_deny_write(f);
}

int fd = thread_current()->fileCnt;
thread_current()->fileTable[thread_current()->fileCnt++] = f;
lock_release(&FileLock);

return fd;
}

int sys_filesize(int fd)
{
  struct file *f = (fd < thread_current()->fileCnt) ? thread_current()->fileTable[fd] : NULL;

  return (f != NULL) ? file_length(f) : -1;
}

int sys_read(int fd, void *buffer, unsigned size)
{
  if (fd < 0 || fd >= thread_current()->fileCnt || !verify_mem_address(buffer)) {
    sys_exit(-1);
  }

  int read_size = 0;
  lock_acquire(&FileLock);

  if (fd == 0) { 
    // Read from standard input (keyboard)
    while (read_size < size && ((char *)buffer)[read_size] != '\0') {
        read_size++;
    }
  } else {
    struct file *f = thread_current()->fileTable[fd];
    
    if (f == NULL) {
        lock_release(&FileLock);
        sys_exit(-1);
    } 
    read_size = file_read(f, buffer, size);
  }

  lock_release(&FileLock);
  return read_size;
}

int sys_write(int fd, const void *buffer, unsigned size)
{
 
 for (int i = 0; i < size; i++) {
    if (!verify_mem_address(buffer + i)) {
        sys_exit(-1);
    }
}

if (fd < 1 || fd >= thread_current()->fileCnt) {
    sys_exit(-1);
}

int write_size = 0;
lock_acquire(&FileLock);

if (fd == 1) {
    // Write to standard output
    putbuf(buffer, size);
    write_size = size;
} else {
    struct file *f = thread_current()->fileTable[fd];

    if (f == NULL) {
        lock_release(&FileLock);
        sys_exit(-1);
    }

    write_size = file_write(f, buffer, size);
}

lock_release(&FileLock);
return write_size;
}

void sys_seek(int fd, unsigned position)
{


 struct file *f = (fd < thread_current()->fileCnt) ? thread_current()->fileTable[fd] : NULL;

  if (f != NULL)  
    file_seek(f, position);
}

unsigned sys_tell(int fd)
{
  struct file *f = (fd < thread_current()->fileCnt) ? thread_current()->fileTable[fd] : NULL;

  return (f != NULL) ? file_tell(f) : 0;
}

void sys_close(int fd)
{
  /*
  struct file *f;
  if(fd < thread_current()->fileCnt)
  {
		f = thread_current()->fileTable[fd];
	}
  else
  {
    f=NULL;
  }

  if (f==NULL)
  {
		return;
	}
  file_close(f);
	thread_current()->fileTable[fd] = NULL;
  */


 /*
  struct file *f = (fd < thread_current()->fileCnt) ? thread_current()->fileTable[fd] : NULL;

  if (f != NULL) {
    file_close(f);
    thread_current()->fileTable[fd] = NULL;
  }
  */
  struct file *file;
  struct thread *t = thread_current();
  int i;

  if(fd >= t->fileCnt || fd < 2){
    sys_exit(-1);
  }

  file = t->fileTable[fd];
  if(file == NULL){
    return;
  }

  file_close(file);
  t->fileTable[fd] = NULL;
  for(i = fd; i < t->fileCnt; i++){
    t->fileTable[i] = t->fileTable[i+1];
  }
  t->fileCnt--;




}

//prjc3, mmf
int 
sys_mmap (int fd, void *addr)
{
  struct thread *t = thread_current ();

  struct file *f = t->fileTable[fd];
  struct file *opened_f;
  struct mmf *mmf;

  if (f == NULL)
    return -1;
  
  if (addr == NULL || (int) addr % PGSIZE != 0)
    return -1;

  lock_acquire (&FileLock);

  opened_f = file_reopen (f);
  if (opened_f == NULL)
  {
    lock_release (&FileLock);
    return -1;
  }

  mmf = init_mmf (t->mapid++, opened_f, addr);
  if (mmf == NULL)
  {
    lock_release (&FileLock);
    return -1;
  }

  lock_release (&FileLock);

  return mmf->id;
}

int 
sys_munmap (int mapid)
{
  struct thread *t = thread_current ();
  struct list_elem *e;
  struct mmf *mmf;
  void *upage;

  if (mapid >= t->mapid)
    return;

  for (e = list_begin (&t->mmf_list); e != list_end (&t->mmf_list); e = list_next (e))
  {
    mmf = list_entry (e, struct mmf, mmf_list_elem);
    if (mmf->id == mapid)
      break;
  }
  if (e == list_end (&t->mmf_list))
    return;

  upage = mmf->upage;

  lock_acquire (&FileLock);
  
  off_t ofs;
  for (ofs = 0; ofs < file_length (mmf->file); ofs += PGSIZE)
  {
    struct spte *entry = get_spte (&t->spt, upage);
    if (pagedir_is_dirty (t->pagedir, upage))
    {
      void *kpage = pagedir_get_page (t->pagedir, upage);
      //file_write_at(entry)
      file_write_at (entry->file, kpage, entry->read_bytes, entry->ofs);
    }
    page_delete (&t->spt, entry);
    upage += PGSIZE;
  }
  list_remove(e);

  lock_release (&FileLock);
}

static void
syscall_handler(struct intr_frame *f)
{
  if(verify_mem_address(f->esp))
  {
  int argv[3];
  switch (*(uint32_t *)(f->esp))
  {
  case SYS_HALT:
    sys_halt();
    break;
  case SYS_EXIT:
    getArgs(f->esp + 4, &argv[0], 1);
    sys_exit((int)argv[0]);
    break;
  case SYS_EXEC:
    getArgs(f->esp + 4, &argv[0], 1);
    f->eax = sys_exec((const char *)argv[0]);
    break;
  case SYS_WAIT:
    getArgs(f->esp + 4, &argv[0], 1);
    f->eax = sys_wait((pid_t)argv[0]);
    break;
  case SYS_CREATE:
    getArgs(f->esp + 4, &argv[0], 2);
    f->eax = sys_create((const char *)argv[0], (unsigned)argv[1]);
    break;
  case SYS_REMOVE:
    getArgs(f->esp + 4, &argv[0], 1);
    f->eax = sys_remove((const char *)argv[0]);
    break;
  case SYS_OPEN:
    getArgs(f->esp + 4, &argv[0], 1);
    f->eax = sys_open((const char *)argv[0]);
    break;
  case SYS_FILESIZE:
    getArgs(f->esp + 4, &argv[0], 1);
    f->eax = sys_filesize(argv[0]);
    break;
  case SYS_READ:
    getArgs(f->esp + 4, &argv[0], 3);
    f->eax = sys_read((int)argv[0], (void *)argv[1], (unsigned)argv[2]);
    break;
  case SYS_WRITE:
    getArgs(f->esp + 4, &argv[0], 3);
    f->eax = sys_write((int)argv[0], (const void *)argv[1], (unsigned)argv[2]);
    break;
  case SYS_SEEK:
    getArgs(f->esp + 4, &argv[0], 2);
    sys_seek(argv[0], (unsigned)argv[1]);
    break;
  case SYS_TELL:
    getArgs(f->esp + 4, &argv[0], 1);
    f->eax = sys_tell(argv[0]);
    break;
  case SYS_CLOSE:
    getArgs(f->esp + 4, &argv[0], 1);
    sys_close(argv[0]);
    break;
  case SYS_MMAP:
    getArgs(f->esp + 4, &argv[0], 2);
    f->eax = sys_mmap((int) argv[0], (void *) argv[1]);
    break;
  case SYS_MUNMAP:
    getArgs(f->esp + 4, &argv[0], 1);
    sys_munmap((int) argv[0]);
    break;
  default:
    sys_exit(-1);
  }
  }else{
    sys_exit(-1);
  }

}

