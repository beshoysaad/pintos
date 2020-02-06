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
#include "vm/mapping.h"
#include "filesys/directory.h"

#define MIN(A, B)	(((A) < (B)) ? (A) : (B))

struct lock lock_file_sys;

static void
syscall_handler (struct intr_frame*);

void
syscall_init (void)
{
  lock_init (&lock_file_sys);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static inline void*
next_page (void *addr)
{
  return pg_round_down (addr) + PGSIZE;
}

static void
terminate_process (struct intr_frame *f, int status)
{
  f->eax = status;
  thread_exit (status);
}

static void*
get_kernel_address (struct intr_frame *f, uint32_t *pd, void *user_address,
bool writable)
{
  if ((f == NULL) || (pd == NULL) || (user_address == NULL)
      || !is_user_vaddr (user_address))
    {
      goto error;
    }

  void *upage = pg_round_down (user_address);
  void *ret_addr = NULL;

  struct process *proc = thread_current ()->p;

  page_check_out (proc, upage, false);

  ret_addr = pagedir_get_page (pd, user_address);

  if (ret_addr == NULL)
    {
      page_check_in (proc, upage);
      if (retrieve_page (user_address, true))
	{
	  ret_addr = pagedir_get_page (pd, user_address);
	}
      else if (grow_stack (user_address, f->esp, true))
	{
	  ret_addr = pagedir_get_page (pd, user_address);
	}
      else
	{
	  goto error;
	}
    }

  if (writable && !page_is_writable (proc, upage))
    {
      page_check_in (proc, upage);
      goto error;
    }

  pagedir_set_accessed (pd, user_address, true);
  if (writable)
    {
      pagedir_set_dirty (pd, user_address, true);
    }

  return ret_addr;
error: terminate_process (f, -1);
  return NULL;
}

static void
syscall_handler (struct intr_frame *f)
{

  struct thread *cur_thread = thread_current ();
  uint32_t *pd = cur_thread->pagedir;
  struct process *cur_proc = cur_thread->p;

  uint32_t *user_sp = f->esp;
  uint32_t *sp = get_kernel_address (f, pd, user_sp, false);

  uint32_t syscall_nr = *sp;

  page_check_in (cur_proc, pg_round_down (user_sp));

  switch (syscall_nr)
    {
    case SYS_HALT:
      {
	shutdown_power_off ();
	break;
      }
    case SYS_EXIT:
      {
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	int status = *(int*) sp;
	page_check_in (cur_proc, pg_round_down (user_sp));
	terminate_process (f, status);
	break;
      }
    case SYS_EXEC:
      {
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	page_check_in (cur_proc, pg_round_down (user_sp));
	char *cmd_line = get_kernel_address (f, pd, *(char**) sp, false);
	lock_acquire (&lock_file_sys);
	f->eax = process_execute (cmd_line);
	lock_release (&lock_file_sys);
	page_check_in (cur_proc, pg_round_down (*(char**) sp));
	break;
      }
    case SYS_WAIT:
      {
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	pid_t pid = *(pid_t*) sp;
	f->eax = process_wait (pid);
	page_check_in (cur_proc, pg_round_down (user_sp));
	break;
      }
    case SYS_CREATE:
      {
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	page_check_in (cur_proc, pg_round_down (user_sp));
	char *file = get_kernel_address (f, pd, *(char**) sp, false);
	if (file == NULL)
	  {
	    f->eax = false;
	    return;
	  }
	if (strcmp (file, "") == 0)
	  {
	    f->eax = false;
	    return;
	  }
	user_sp++;
	page_check_in (cur_proc, pg_round_down (*(char**) sp));
	sp = get_kernel_address (f, pd, user_sp, false);
	unsigned size = *(unsigned*) sp;
	lock_acquire (&lock_file_sys);
	f->eax = filesys_create (file, size);
	lock_release (&lock_file_sys);
	page_check_in (cur_proc, pg_round_down (user_sp));
	break;
      }
    case SYS_REMOVE:
      {
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	char *file = get_kernel_address (f, pd, *(char**) sp, false);
	lock_acquire (&lock_file_sys);
	f->eax = filesys_remove (file);
	lock_release (&lock_file_sys);
	page_check_in (cur_proc, pg_round_down (user_sp));
	page_check_in (cur_proc, pg_round_down (*(char**) sp));
	break;
      }
    case SYS_OPEN:
      {
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	page_check_in (cur_proc, pg_round_down (user_sp));
	char *file = get_kernel_address (f, pd, *(char**) sp, false);
	lock_acquire (&lock_file_sys);
	struct file *fl = filesys_open (file);
	lock_release (&lock_file_sys);
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
		page_check_in (cur_proc, pg_round_down (*(char**) sp));
		return;
	      }
	    fd->f = fl;
	    fd->pos = 0;
	    fd->fd = cur_proc->fd_counter++;
	    list_push_front (cur_proc->list_file_desc, &fd->elem);
	    f->eax = fd->fd;
	  }
	page_check_in (cur_proc, pg_round_down (*(char**) sp));
	break;
      }
    case SYS_FILESIZE:
      {
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	int fd = *(int*) sp;

	struct list_elem *e;
	for (e = list_begin (cur_proc->list_file_desc);
	    e != list_end (cur_proc->list_file_desc); e = list_next (e))
	  {
	    struct file_desc *fl = list_entry(e, struct file_desc, elem);
	    if (fl->fd == fd)
	      {
		lock_acquire (&lock_file_sys);
		f->eax = file_length (fl->f);
		lock_release (&lock_file_sys);
		page_check_in (cur_proc, pg_round_down (user_sp));
		return;
	      }
	  }
	f->eax = -1;
	page_check_in (cur_proc, pg_round_down (user_sp));
	break;
      }
    case SYS_READ:
      {
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	int fd = *(int*) sp;
	page_check_in (cur_proc, pg_round_down (user_sp));
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	uint8_t *user_buffer = *(uint8_t**) sp;
	if (user_buffer == NULL)
	  {
	    page_check_in (cur_proc, pg_round_down (user_sp));
	    terminate_process (f, -1);
	    return;
	  }
	page_check_in (cur_proc, pg_round_down (user_sp));
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	unsigned size = *(unsigned*) sp;
	page_check_in (cur_proc, pg_round_down (user_sp));
	if (fd == 0)
	  {
	    int32_t rem_size = size;
	    unsigned chunk_idx = 0;
	    do
	      {
		uint8_t *kernel_buffer = get_kernel_address (
		    f, pd, user_buffer + chunk_idx, true);

		unsigned chunk_sz = MIN(
		    rem_size,
		    (uint8_t* )next_page (kernel_buffer) - kernel_buffer);

		for (unsigned i = 0; i < chunk_sz; i++)
		  {
		    kernel_buffer[i] = input_getc ();
		  }
		page_check_in (cur_proc,
			       pg_round_down (user_buffer + chunk_idx));
		rem_size -= chunk_sz;
		chunk_idx += chunk_sz;
	      }
	    while (rem_size > 0);
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

		    int32_t rem_size = size;
		    unsigned chunk_idx = 0;
		    f->eax = 0;
		    do
		      {
			uint8_t *kernel_buffer = get_kernel_address (
			    f, pd, user_buffer + chunk_idx, true);
			unsigned chunk_sz = MIN(
			    rem_size,
			    (uint8_t* )next_page (kernel_buffer)
				- kernel_buffer);
			off_t sz = file_read_at (fl->f, kernel_buffer, chunk_sz,
						 fl->pos);
			f->eax += sz;
			fl->pos += sz;
			page_check_in (cur_proc,
				       pg_round_down (user_buffer + chunk_idx));
			rem_size -= chunk_sz;
			chunk_idx += chunk_sz;
		      }
		    while (rem_size > 0);

		    lock_release (&lock_file_sys);

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
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	int fd = *(int*) sp;
	page_check_in (cur_proc, pg_round_down (user_sp));
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	void *user_buffer = *(void**) sp;
	if (user_buffer == NULL)
	  {
	    page_check_in (cur_proc, pg_round_down (user_sp));
	    terminate_process (f, -1);
	    return;
	  }
	page_check_in (cur_proc, pg_round_down (user_sp));
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	unsigned size = *(unsigned*) sp;
	page_check_in (cur_proc, pg_round_down (user_sp));
	if (fd == 0)
	  {
	    f->eax = -1;
	    return;
	  }
	else if (fd == 1)
	  {
	    int32_t rem_size = size;
	    unsigned chunk_idx = 0;
	    do
	      {
		char *kernel_buffer = get_kernel_address (
		    f, pd, user_buffer + chunk_idx, false);
		unsigned chunk_sz = MIN(
		    rem_size,
		    (char* )next_page (kernel_buffer) - kernel_buffer);
		putbuf (kernel_buffer, chunk_sz);
		page_check_in (cur_proc,
			       pg_round_down (user_buffer + chunk_idx));
		rem_size -= chunk_sz;
		chunk_idx += chunk_sz;
	      }
	    while (rem_size > 0);

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

		    int32_t rem_size = size;
		    unsigned chunk_idx = 0;
		    f->eax = 0;
		    do
		      {
			uint8_t *kernel_buffer = get_kernel_address (
			    f, pd, user_buffer + chunk_idx, false);
			unsigned chunk_sz = MIN(
			    rem_size,
			    (uint8_t* )next_page (kernel_buffer)
				- kernel_buffer);
			off_t sz = file_write_at (fl->f, kernel_buffer,
						  chunk_sz, fl->pos);
			f->eax += sz;
			fl->pos += sz;
			page_check_in (cur_proc,
				       pg_round_down (user_buffer + chunk_idx));
			rem_size -= chunk_sz;
			chunk_idx += chunk_sz;
		      }
		    while (rem_size > 0);
		    lock_release (&lock_file_sys);

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
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	int fd = *(int*) sp;
	page_check_in (cur_proc, pg_round_down (user_sp));
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	unsigned position = *(unsigned*) sp;
	if ((fd == 0) || (fd == 1))
	  {
	    page_check_in (cur_proc, pg_round_down (user_sp));
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
		    page_check_in (cur_proc, pg_round_down (user_sp));
		    return;
		  }
	      }
	  }
	break;
      }
    case SYS_TELL:
      {
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	int fd = *(int*) sp;
	if ((fd == 0) || (fd == 1))
	  {
	    page_check_in (cur_proc, pg_round_down (user_sp));
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
		    page_check_in (cur_proc, pg_round_down (user_sp));
		    return;
		  }
	      }
	    page_check_in (cur_proc, pg_round_down (user_sp));
	    terminate_process (f, -1);
	    return;
	  }
	break;
      }
    case SYS_CLOSE:
      {
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	int fd = *(int*) sp;
	if ((fd == 0) || (fd == 1))
	  {
	    page_check_in (cur_proc, pg_round_down (user_sp));
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
		    page_check_in (cur_proc, pg_round_down (user_sp));
		    return;
		  }
	      }
	    page_check_in (cur_proc, pg_round_down (user_sp));
	    terminate_process (f, -1);
	    return;
	  }
	break;
      }
    case SYS_MMAP:
      {
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	int fd = *(int*) sp;
	if ((fd == 0) || (fd == 1))
	  {
	    f->eax = -1;
	    page_check_in (cur_proc, pg_round_down (user_sp));
	    return;
	  }
	page_check_in (cur_proc, pg_round_down (user_sp));
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	void *addr = *(void**) sp;
	if (addr == NULL)
	  {
	    f->eax = -1;
	    page_check_in (cur_proc, pg_round_down (user_sp));
	    return;
	  }
	struct list_elem *e;
	for (e = list_begin (cur_proc->list_file_desc);
	    e != list_end (cur_proc->list_file_desc); e = list_next (e))
	  {
	    struct file_desc *fl = list_entry(e, struct file_desc, elem);
	    if (fl->fd == fd)
	      {
		lock_acquire (&lock_file_sys);
		struct file *new_f = file_reopen (fl->f);
		lock_release (&lock_file_sys);
		struct mapping *mp = mapping_alloc (cur_proc, addr, new_f);
		if (mp == NULL)
		  {
		    f->eax = -1;
		  }
		else
		  {
		    f->eax = mp->map_id;
		  }
		page_check_in (cur_proc, pg_round_down (user_sp));
		return;
	      }
	  }
	page_check_in (cur_proc, pg_round_down (user_sp));
	terminate_process (f, -1);
	break;
      }
    case SYS_MUNMAP:
      {
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	mapid_t mapping = *(mapid_t*) sp;
	mapping_free (cur_proc, mapping);
	page_check_in (cur_proc, pg_round_down (user_sp));
	break;
      }
    case SYS_CHDIR:
      {
	user_sp++;
	sp = get_kernel_address (f, pd, user_sp, false);
	page_check_in (cur_proc, pg_round_down (user_sp));
	char *dir_name = get_kernel_address (f, pd, *(char**) sp, false);
	page_check_in(cur_proc, pg_round_down(*(char**) sp));
	if (dir_name == NULL)
	  {
	    f->eax = false;
	    return;
	  }
	else if (strcmp (dir_name, ""))
	  {
	    f->eax = false;
	    return;
	  }
	else
	  {
	    struct inode *inode;
	    if (dir_name[0] == '/') // absolute
	      {
		f->eax = dir_lookup (dir_open_root (), dir_name, &inode,
				     ENTRY_TYPE_DIR);
	      }
	    else // relative
	      {
		f->eax = dir_lookup (cur_proc->cur_dir, dir_name, &inode,
				     ENTRY_TYPE_DIR);
	      }
	    if (f->eax == true)
	      {
		cur_proc->cur_dir = dir_open (inode);
	      }
	  }
	break;
      }
    case SYS_MKDIR:
      {
	break;
      }
    case SYS_READDIR:
      {
	break;
      }
    case SYS_ISDIR:
      {
	break;
      }
    case SYS_INUMBER:
      {
	break;
      }
    default:
      break;
    }
}
