#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "userprog/syscall.h"


struct thread *getChild (pid_t pid);                       /* 자녀 스레드 포인터 반환 */
int make_argv(char **argv, char *file_name);               /* argc 반환, argv 리스트에 내용 채움*/
void cmd_stack_build(char **argv, int argc, void **esp);   /* 만든 argv 바탕으로 stack에 관련 정보 저장*/

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);



#endif /* userprog/process.h */