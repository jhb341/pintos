#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>

void syscall_init (void);

bool verify_thread_memory(void *addr);

static void syscall_handler (struct intr_frame *f);

#endif /* userprog/syscall.h */
