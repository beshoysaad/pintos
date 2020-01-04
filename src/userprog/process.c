#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "vm/frame.h"
#include "vm/page.h"

struct proc_inf
{
  char *fn;
  struct process *p;
};

static struct list process_list;
static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
void parser_commands (char *command, int *argc, char *argv[]);

void
parser_commands (char *command, int *argc, char *argv[])
{
  char *save = NULL; //use in strtok_r, save previous command
  argv[*argc] = strtok_r (command, " ", &save); //parser

  while (argv[*argc] != NULL)
    {
      argv[++(*argc)] = strtok_r (NULL, " ", &save);
    }
}

void
process_start (void)
{
  list_init (&process_list);
}

/* Starts a new thread running a user program loaded from
 FILENAME.  The new thread may be scheduled (and may even exit)
 before process_execute() returns.  Returns the new process's
 thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name)
{
  struct proc_inf *p_inf = (struct proc_inf*) malloc (sizeof(struct proc_inf));
  if (p_inf == NULL)
    {
      return TID_ERROR;
    }

  /* Make a copy of FILE_NAME.
   Otherwise there's a race between the caller and load(). */
  p_inf->fn = palloc_get_page (0);
  if (p_inf->fn == NULL)
    {
      free (p_inf);
      return TID_ERROR;
    }
  strlcpy (p_inf->fn, file_name, PGSIZE);

  struct process *p = (struct process*) malloc (sizeof(struct process));
  if (p == NULL)
    {
      palloc_free_page (p_inf->fn);
      free (p_inf);
      return TID_ERROR;
    }
  p->has_wait = false;
  p->terminated = false;
  p->parent_pid = thread_current ()->tid;
  p->fd_counter = 2;
  p->executable = NULL;
  sema_init (&p->sema_start, 0);
  sema_init (&p->sema_terminate, 0);
  lock_init (&p->lock_modify);
  p->list_file_desc = (struct list*) malloc (sizeof(struct list));
  if (p->list_file_desc == NULL)
    {
      free (p);
      palloc_free_page (p_inf->fn);
      free (p_inf);
      return TID_ERROR;
    }
  list_init (p->list_file_desc);
  list_push_front (&process_list, &p->elem);

  // Init page table
  lock_init (&p->page_table_lock);
  if (!page_table_init (&p->pages))
    {
      free (p->list_file_desc);
      free (p);
      palloc_free_page (p_inf->fn);
      free (p_inf);
      return TID_ERROR;
    }

  p_inf->p = p;

  /* Create a new thread to execute FILE_NAME. */
  tid_t tid = p->pid = thread_create (file_name, PRI_DEFAULT, start_process,
				      p_inf);

  if (tid == TID_ERROR)
    {
      palloc_free_page (p_inf->fn);
      list_remove (&p->elem);
      free (p);
    }
  else
    {
      sema_down (&p->sema_start);
      if (!p->load_successful)
	{
	  tid = TID_ERROR;
	}
    }
  free (p_inf);
  return tid;
}

/* A thread function that loads a user process and starts it
 running. */
static void
start_process (void *p_inf_)
{
  ASSERT(p_inf_ != NULL);

  struct proc_inf *p_inf = (struct proc_inf *)p_inf_;

  char *file_name = p_inf->fn;
  ASSERT(file_name != NULL);
  struct intr_frame if_;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  char *argv[ARGC_MAX];
  int argc = 0;
  parser_commands (file_name, &argc, argv);

  thread_current()->p = p_inf->p;

  bool success = load (argv[0], &if_.eip, &if_.esp);

  /* If load failed, quit. */
  if (!success)
    {
      palloc_free_page (file_name);
      p_inf->p->load_successful = false;
      sema_up(&p_inf->p->sema_start);
      thread_exit (-1);
    }

  // Fix thread's name
  strlcpy(thread_current()->name, argv[0], sizeof(thread_current()->name));

  /*push the address of parameters*/
  char *ptr_address[argc];
  for (int i = argc - 1; i >= 0; i--)
    {
      size_t str_size = strlen (argv[i]) + 1;
      if_.esp = if_.esp - str_size; //move the stack pointer
      memcpy (if_.esp, argv[i], str_size);
      ptr_address[i] = (char*) if_.esp;
    }

  /*word-align*/
  while ((int) if_.esp % base_offset != 0)
    {
      if_.esp--;
    }

  /*push argv address extend*/
  if_.esp = if_.esp - base_offset;
  (*(int*) if_.esp) = 0;

  /*push the address of address of parameters*/
  for (int i = argc - 1; i >= 0; i--)
    {
      if_.esp = if_.esp - base_offset;
      (*(char**) if_.esp) = ptr_address[i];
    }

  /*push the address of argv*/
  if_.esp = if_.esp - base_offset;
  (*(char**) if_.esp) = if_.esp + base_offset;

  /*push argc*/
  if_.esp = if_.esp - base_offset;
  (*(int*) if_.esp) = argc;

  /*push 0*/
  if_.esp = if_.esp - base_offset;
  (*(int*) if_.esp) = 0;

  palloc_free_page (file_name);

  p_inf->p->load_successful = true;
  sema_up(&p_inf->p->sema_start);

  /* Start the user process by simulating a return from an
   interrupt, implemented by intr_exit (in
   threads/intr-stubs.S).  Because intr_exit takes all of its
   arguments on the stack in the form of a `struct intr_frame',
   we just point the stack pointer (%esp) to our stack frame
   and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");

  NOT_REACHED();
}

/* Waits for thread TID to die and returns its exit status.  If
 it was terminated by the kernel (i.e. killed due to an
 exception), returns -1.  If TID is invalid or if it was not a
 child of the calling process, or if process_wait() has already
 been successfully called for the given TID, returns -1
 immediately, without waiting.

 This function will be implemented in problem 2-2.  For now, it
 does nothing. */
int
process_wait (tid_t child_tid)
{
  pid_t own_pid = thread_current ()->tid;

  struct process *proc = NULL;

  struct list_elem *e;

  for (e = list_begin (&process_list); e != list_end (&process_list); e =
      list_next (e))
    {
      struct process *p = list_entry(e, struct process, elem);
      if ((p->pid == child_tid) && (p->parent_pid == own_pid))
	{
	  proc = p;
	  break;
	}
    }

  if (proc == NULL)
    {
      return -1;
    }

  lock_acquire (&proc->lock_modify);

  if (proc->has_wait)
    {
      lock_release (&proc->lock_modify);
      return -1;
    }
  else
    {
      proc->has_wait = true;
      lock_release (&proc->lock_modify);
      sema_down (&proc->sema_terminate);
      return proc->exit_code;
    }
  return -1;
}

/* Free the current process's resources. */
void
process_exit (int status)
{
  struct thread *cur_thread = thread_current ();
  struct process *cur_process = cur_thread->p;
  uint32_t *pd;

  ASSERT(cur_process != NULL);
  ASSERT(cur_process->list_file_desc != NULL);

  while (!list_empty (cur_process->list_file_desc))
    {
      struct list_elem *e = list_pop_front (cur_process->list_file_desc);
      free (list_entry(e, struct file_desc, elem));
    }

  free (cur_process->list_file_desc);
  cur_process->terminated = true;
  cur_process->exit_code = status;
  if (cur_process->executable != NULL)
    {
      file_close (cur_process->executable);
    }

  /* Free and remove from list all terminated children.
   * If parent has been terminated, free and remove this as well */

  bool free_this_process = false;
  bool parent_found = false;

  struct list_elem *e = list_begin (&process_list);
  while (e != list_end (&process_list) && !list_empty (&process_list))
    {
      struct process *p = list_entry(e, struct process, elem);
      if (p->pid == cur_process->parent_pid)
	{
	  parent_found = true;
	  if (p->terminated)
	    {
	      free_this_process = true;
	    }
	}
      if ((p->parent_pid == cur_process->pid) && (p->terminated))
	{
	  e = list_remove (e);
	  free (p);
	  continue;
	}
      e = list_next(e);

    }

  // Free this process's page table
  page_table_destroy();

  printf ("%s: exit(%d)\n", thread_current ()->name, status);

  sema_up (&cur_process->sema_terminate);

  if (free_this_process || !parent_found)
    {
      list_remove (&cur_process->elem);
      free (cur_process);
    }

  /* Destroy the current process's page directory and switch back
   to the kernel-only page directory. */
  pd = cur_thread->pagedir;

  if (pd != NULL)
    {
      /* Correct ordering here is crucial.  We must set
       cur->pagedir to NULL before switching page directories,
       so that a timer interrupt can't switch back to the
       process page directory.  We must activate the base page
       directory before destroying the process's page
       directory, or our active page directory will be one
       that's been freed (and cleared). */
      cur_thread->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  t->p->executable = file;
  file_deny_write(file);

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  if (!success)
    {
      file_close (file);
    }
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = page_add(upage, PAGE_TYPE_FILE);
      if (kpage == NULL)
        return false;

      /* Load this page.
       * TODO: delay until page fault */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false;
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
	  page_remove(upage);
          return false; 
        }

      // TODO set page as not present
//      pagedir_clear_page(thread_current()->pagedir, upage);

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
 user virtual memory. */
static bool
setup_stack (void **esp)
{
  uint8_t *kpage, *upage;
  bool success = false;

  upage = ((uint8_t*) PHYS_BASE) - PGSIZE;

  kpage = page_add (upage, PAGE_TYPE_ZERO);

  if (kpage != NULL)
    {
      success = install_page (upage, kpage, true);
      if (success)
	*esp = PHYS_BASE;
      else
	page_remove(upage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}


