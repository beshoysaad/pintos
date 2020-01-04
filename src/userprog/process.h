#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "lib/user/syscall.h"
#include "filesys/off_t.h"
#include "filesys/file.h"

#define ARGC_MAX 128
#define base_offset 4

struct file_desc {
  int fd;
  struct file *f;
  off_t pos;
  struct list_elem elem;
};

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
  struct list *list_file_desc;
  int fd_counter;
  struct file* executable;
  bool terminated;
  struct hash *pages;
  struct lock page_table_lock;
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (int status);
void process_activate (void);
void process_start(void);

#endif /* userprog/process.h */
