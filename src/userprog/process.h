#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "lib/user/syscall.h"

#define ARGC_MAX 128
#define base_offset 4

struct process {
  pid_t pid;
  pid_t parent_pid;
  struct list_elem elem;
  struct semaphore sema_start;
  struct semaphore sema_terminate;
  struct lock lock_modify;
  bool has_wait;
  int exit_code;
  bool load_successful;
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (int status);
void process_activate (void);
void process_start(void);

#endif /* userprog/process.h */
