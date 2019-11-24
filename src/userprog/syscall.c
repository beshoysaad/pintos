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

static void
syscall_handler (struct intr_frame*);

void
syscall_init (void)
{
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
  if (pagedir_get_page (pd, upage + sz - 1) == NULL)
    {
      return NULL;
    }
  return pagedir_get_page (pd, upage);
}

static void
syscall_handler (struct intr_frame *f)
{

  uint32_t *pd = thread_current ()->pagedir;
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
      shutdown_power_off ();
      break;
    case SYS_EXIT:
      sp++;
      int status = *(int*) sp;
      terminate_process (f, status);
      break;
    case SYS_EXEC:
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
      f->eax = process_execute (cmd_line);
      break;
    case SYS_WAIT:
      sp++;
      pid_t pid = *(pid_t*)sp;
      f->eax = process_wait(pid);
      break;
//    case SYS_CREATE:
//      break;
//    case SYS_REMOVE:
//      break;
//    case SYS_OPEN:
//      break;
//    case SYS_FILESIZE:
//      break;
//    case SYS_READ:
//      break;
    case SYS_WRITE:
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
      if (fd == 1)
	{
	  putbuf (buffer, size);
	}
      f->eax = size;
      break;
//    case SYS_SEEK:
//      break;
//    case SYS_TELL:
//      break;
//    case SYS_CLOSE:
//      break;
    default:
      break;
    }
}
