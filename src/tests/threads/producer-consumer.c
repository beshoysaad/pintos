/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

void
producer_consumer (unsigned int num_producer, unsigned int num_consumer);

#define OUT_BUF_SIZE 32

char hello_str[] = "Hello world";
char output_str[OUT_BUF_SIZE]; // this is a circular FIFO buffer
struct lock mutex;
struct condition notfull;
struct condition notempty;

// Producer writes at current index then proceeds, while consumer proceeds first then reads current index
size_t out_buf_read_idx = 0, out_buf_write_idx = 1;

void
test_producer_consumer (void)
{
//  producer_consumer (0, 0);
//  producer_consumer (1, 0);
//  producer_consumer (0, 1);
//  producer_consumer (1, 1);
//  producer_consumer (3, 1);
//  producer_consumer (1, 3);
//  producer_consumer (4, 4);
//  producer_consumer (7, 2);
//  producer_consumer (2, 7);
  producer_consumer (6, 6);
  pass ();
}

static void
producer (UNUSED void *arg)
{
  for (size_t i = 0; i < sizeof(hello_str); i++)
    {
      lock_acquire (&mutex);
      while (out_buf_write_idx == out_buf_read_idx) // output buffer is full
	{
	  cond_wait (&notfull, &mutex);
	}
      output_str[out_buf_write_idx] = hello_str[i]; // write
      out_buf_write_idx = (out_buf_write_idx + 1) % OUT_BUF_SIZE; // proceed
      cond_signal (&notempty, &mutex);
      lock_release (&mutex);
    }
}

static void
consumer (UNUSED void *arg)
{
  while (true)
    {
      lock_acquire (&mutex);
      while ((out_buf_read_idx + 1) % OUT_BUF_SIZE == out_buf_write_idx) // buffer is empty
	{
	  cond_wait (&notempty, &mutex);
	}
      out_buf_read_idx = (out_buf_read_idx + 1) % OUT_BUF_SIZE; // proceed
      printf ("%c", output_str[out_buf_read_idx]); // read
      cond_signal (&notfull, &mutex);
      lock_release (&mutex);
    }
}

void
producer_consumer (unsigned int num_producer, unsigned int num_consumer)
{
  char name[32];
  lock_init (&mutex);
  cond_init (&notfull);
  cond_init (&notempty);
  /*create threads for producer and consumer*/
  for (unsigned int i = 0; i < num_producer; i++)
    {
      snprintf (name, sizeof(name), "producer_%d", i);
      thread_create (name, PRI_DEFAULT, producer, NULL);
    }
  for (unsigned int i = 0; i < num_consumer; i++)
    {
      snprintf (name, sizeof(name), "consumer_%d", i);
      thread_create (name, PRI_DEFAULT, consumer, NULL);
    }
}
