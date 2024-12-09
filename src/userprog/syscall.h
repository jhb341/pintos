#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdbool.h>


typedef int pid_t;
typedef int mapid_t;

void getArgs(void *esp, int *arg, int count);
void syscall_init (void);

/* helper sys_xxxx below */
void sys_halt (void);
void sys_exit (int status);
pid_t sys_exec (const char *cmd_lime);
int sys_wait (pid_t pid);
bool sys_create (const char *file, unsigned initial_size);
bool sys_remove (const char *file);
int sys_open (const char *file);
int sys_filesize (int fd);
int sys_read (int fd, void *buffer, unsigned size);
int sys_write (int fd, const void *buffer, unsigned size);
void sys_seek (int fd, unsigned position);
unsigned sys_tell (int fd);
void sys_close (int fd);

// prjc3 mmf syscall function
int sys_mmap(int fd, void *addr);
int sys_munmap(int mmfCnt);


#endif /* userprog/syscall.h */