#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "pagedir.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "string.h"
#include "process.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "devices/input.h"

struct lock lock_file_sys;

static void
syscall_handler (struct intr_frame*);

void
syscall_init (void)
{
  lock_init (&lock_file_sys);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
terminate_process (struct intr_frame *f, int status)
{
  f->eax = status;
  thread_exit (status);
}

static void*
read_user_mem (uint32_t *pd, const void *upage, size_t sz)
{
  if (pd == NULL)
    {
      return NULL;
    }
  if (!is_user_vaddr (upage) || !is_user_vaddr (upage + sz - 1))
    {
      return NULL;
    }
  if ((sz > 1) && (pg_no (upage) != pg_no (upage + sz - 1)))
    {
      if (pagedir_get_page (pd, upage + sz - 1) == NULL)
	{
	  return NULL;
	}
    }
  return pagedir_get_page (pd, upage);
}

static void
syscall_handler (struct intr_frame *f)
{

  uint32_t *pd = thread_current ()->pagedir;
  struct process *cur_proc = thread_current ()->p;
  uint32_t *sp = (uint32_t*) read_user_mem (pd, f->esp, 4 * 3);

  if (sp == NULL)
    {
      terminate_process (f, -1);
      return;
    }

  uint32_t syscall_nr = *sp;

  switch (syscall_nr)
    {
    case SYS_HALT:
      {
	shutdown_power_off ();
	break;
      }
    case SYS_EXIT:
      {
	sp++;
	int status = *(int*) sp;
	terminate_process (f, status);
	break;
      }
    case SYS_EXEC:
      {
	sp++;
	const char *cmd_line = (char*) read_user_mem (pd, *(char**) sp, 1);
	if (cmd_line == NULL)
	  {
	    terminate_process (f, -1);
	    return;
	  }
	// Have to check again because string maybe going into invalid memory
	cmd_line = (char*) read_user_mem (pd, *(char**) sp, strlen (cmd_line));
	if (cmd_line == NULL)
	  {
	    terminate_process (f, -1);
	    return;
	  }
	lock_acquire (&lock_file_sys);
	f->eax = process_execute (cmd_line);
	lock_release (&lock_file_sys);
	break;
      }
    case SYS_WAIT:
      {
	sp++;
	pid_t pid = *(pid_t*) sp;
	f->eax = process_wait (pid);
	break;
      }
    case SYS_CREATE:
      {
	sp++;
	const char *file = (const char*) read_user_mem (pd, *(char**) sp, 1);
	if (file == NULL)
	  {
	    terminate_process (f, -1);
	    return;
	  }
	// Have to check again
	file = (const char*) read_user_mem (pd, *(char**) sp, strlen (file));
	if (file == NULL)
	  {
	    terminate_process (f, -1);
	    return;
	  }
	sp++;
	unsigned size = *(unsigned*) sp;
	lock_acquire (&lock_file_sys);
	f->eax = filesys_create (file, size);
	lock_release (&lock_file_sys);
	break;
      }
    case SYS_REMOVE:
      {
	sp++;
	const char *file = (const char*) read_user_mem (pd, *(char**) sp, 1);
	if (file == NULL)
	  {
	    terminate_process (f, -1);
	    return;
	  }
	// Have to check again
	file = (const char*) read_user_mem (pd, *(char**) sp, strlen (file));
	if (file == NULL)
	  {
	    terminate_process (f, -1);
	    return;
	  }
	lock_acquire (&lock_file_sys);
	f->eax = filesys_remove (file);
	lock_release (&lock_file_sys);
	break;
      }
    case SYS_OPEN:
      {
	sp++;
	const char *file = (const char*) read_user_mem (pd, *(char**) sp, 1);
	if (file == NULL)
	  {
	    terminate_process (f, -1);
	    return;
	  }
	// Have to check again
	file = (const char*) read_user_mem (pd, *(char**) sp, strlen (file));
	if (file == NULL)
	  {
	    terminate_process (f, -1);
	    return;
	  }
	struct file *fl = filesys_open (file);
	if (fl == NULL)
	  {
	    f->eax = -1;
	  }
	else
	  {
	    struct file_desc *fd = (struct file_desc*) malloc (
		sizeof(struct file_desc));
	    if (fd == NULL)
	      {
		f->eax = -1;
		return;
	      }
	    fd->f = fl;
	    fd->pos = 0;
	    fd->fd = cur_proc->fd_counter++;
	    list_push_front (cur_proc->list_file_desc, &fd->elem);
	    f->eax = fd->fd;
	  }
	break;
      }
    case SYS_FILESIZE:
      {
	sp++;
	int fd = *(int*) sp;
	struct list_elem *e;

	for (e = list_begin (cur_proc->list_file_desc);
	    e != list_end (cur_proc->list_file_desc); e = list_next (e))
	  {
	    struct file_desc *fl = list_entry(e, struct file_desc, elem);
	    if (fl->fd == fd)
	      {
		f->eax = file_length (fl->f);
		return;
	      }
	  }
	f->eax = -1;
	break;
      }
    case SYS_READ:
      {
	sp++;
	int fd = *(int*) sp;
	sp++;
	uint8_t *buffer = (uint8_t*) sp;
	sp++;
	unsigned size = *(unsigned*) sp;
	buffer = (uint8_t*) read_user_mem (pd, *(uint8_t**) buffer, size);
	if (buffer == NULL)
	  {
	    terminate_process (f, -1);
	    return;
	  }
	if (fd == 0)
	  {
	    for (unsigned i = 0; i < size; i++)
	      {
		buffer[i] = input_getc ();
	      }
	    f->eax = size;
	    return;
	  }
	else if (fd == 1)
	  {
	    f->eax = -1;
	    return;
	  }
	else
	  {
	    struct list_elem *e;
	    for (e = list_begin (cur_proc->list_file_desc);
		e != list_end (cur_proc->list_file_desc); e = list_next (e))
	      {
		struct file_desc *fl = list_entry(e, struct file_desc, elem);
		if (fl->fd == fd)
		  {
		    lock_acquire (&lock_file_sys);
		    f->eax = file_read_at (fl->f, buffer, size, fl->pos);
		    lock_release (&lock_file_sys);
		    fl->pos += f->eax;
		    return;
		  }
	      }
	    f->eax = -1;
	    return;
	  }
	break;
      }
    case SYS_WRITE:
      {
	sp++;
	int fd = *(int*) sp;
	sp++;
	void *buffer = sp;
	sp++;
	unsigned size = *(unsigned*) sp;
	buffer = read_user_mem (pd, *(void**) buffer, size);
	if (buffer == NULL)
	  {
	    terminate_process (f, -1);
	    return;
	  }
	if (fd == 0)
	  {
	    f->eax = -1;
	    return;
	  }
	else if (fd == 1)
	  {
	    putbuf (buffer, size);
	    f->eax = size;
	    return;
	  }
	else
	  {
	    struct list_elem *e;
	    for (e = list_begin (cur_proc->list_file_desc);
		e != list_end (cur_proc->list_file_desc); e = list_next (e))
	      {
		struct file_desc *fl = list_entry(e, struct file_desc, elem);
		if (fl->fd == fd)
		  {
		    lock_acquire (&lock_file_sys);
		    f->eax = file_write_at (fl->f, buffer, size, fl->pos);
		    lock_release (&lock_file_sys);
		    fl->pos += f->eax;
		    return;
		  }
	      }
	    f->eax = -1;
	    return;
	  }
	break;
      }
    case SYS_SEEK:
      {
	sp++;
	int fd = *(int*) sp;
	sp++;
	unsigned position = *(unsigned*) sp;
	if ((fd == 0) || (fd == 1))
	  {
	    return;
	  }
	else
	  {
	    struct list_elem *e;
	    for (e = list_begin (cur_proc->list_file_desc);
		e != list_end (cur_proc->list_file_desc); e = list_next (e))
	      {
		struct file_desc *fl = list_entry(e, struct file_desc, elem);
		if (fl->fd == fd)
		  {
		    fl->pos = position;
		    return;
		  }
	      }
	  }
	break;
      }
    case SYS_TELL:
      {
	sp++;
	int fd = *(int*) sp;
	if ((fd == 0) || (fd == 1))
	  {
	    terminate_process (f, -1);
	    return;
	  }
	else
	  {
	    struct list_elem *e;
	    for (e = list_begin (cur_proc->list_file_desc);
		e != list_end (cur_proc->list_file_desc); e = list_next (e))
	      {
		struct file_desc *fl = list_entry(e, struct file_desc, elem);
		if (fl->fd == fd)
		  {
		    f->eax = fl->pos;
		    return;
		  }
	      }
	    terminate_process (f, -1);
	    return;
	  }
	break;
      }
    case SYS_CLOSE:
      {
	sp++;
	int fd = *(int*) sp;
	if ((fd == 0) || (fd == 1))
	  {
	    terminate_process (f, -1);
	  }
	else
	  {
	    struct list_elem *e;
	    for (e = list_begin (cur_proc->list_file_desc);
		e != list_end (cur_proc->list_file_desc); e = list_next (e))
	      {
		struct file_desc *fl = list_entry(e, struct file_desc, elem);
		if (fl->fd == fd)
		  {
		    list_remove (&fl->elem);
		    free (fl);
		    return;
		  }
	      }
	    terminate_process (f, -1);
	    return;
	  }
	break;
      }
    default:
      break;
    }
}
