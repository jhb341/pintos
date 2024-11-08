#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

int parse_input_cmd(char *file_name, char **argv);  /* 구현 완료 */

tid_t process_execute (const char *file_name);  /* 구현 완료 */
int process_wait (tid_t);                       /* 구현 완료 */
void process_exit (void);                       /* 구현 완료 */          
void process_activate (void);                   /* 구현 완료 */

#endif /* userprog/process.h */
