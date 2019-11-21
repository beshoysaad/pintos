#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void
syscall_handler (struct intr_frame*);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{
  printf ("system call!\n");

//  printf ("current thread pd %p\r\n", thread_current ()->pagedir);

  uint32_t *sp = (uint32_t*) pagedir_get_page (thread_current ()->pagedir,
					       f->esp);

//  printf ("old sp %p\r\n", sp);

  uint32_t syscall_nr = *sp;

  printf ("syscall no. %d\r\n", syscall_nr);

  switch (syscall_nr)
    {
//    case SYS_HALT:
//      break;
    case SYS_EXIT:
      sp++;
      int status = *(int *)sp;
      f->eax = status;
      thread_exit();
      break;
//    case SYS_EXEC:
//      break;
    case SYS_WAIT:
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
//      printf ("new sp %p\r\n", sp);
      int fd = *(int*) sp;
      sp++;
      void *buffer = pagedir_get_page (thread_current ()->pagedir,
				       *(void**) sp);
      sp++;
      unsigned size = *(unsigned*) sp;
      if (fd == 1)
	{
	  putbuf (buffer, size);
	}
      f->eax = size;
      break;
    default:
      thread_exit ();
//    case SYS_SEEK:
//      break;
//    case SYS_TELL:
//      break;
//    case SYS_CLOSE:
//      break;
    }
}
