/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include <string.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"


void producer_consumer(unsigned int num_producer, unsigned int num_consumer);


void test_producer_consumer(void)
{
    /*producer_consumer(0, 0);
    producer_consumer(1, 0);
    producer_consumer(0, 1);
    producer_consumer(1, 1);
    producer_consumer(3, 1);
    producer_consumer(1, 3);
    producer_consumer(4, 4);
    producer_consumer(7, 2);
    producer_consumer(2, 7);*/
    producer_consumer(6, 6);
    pass();
}


#define PRODUCER_MESSAGE  ("Hello world")
#define BOUNDED_BUFFER_SIZE  (4)


/* Information about the test. */
struct producer_consumer_test 
{
  char buffer_data [BOUNDED_BUFFER_SIZE];
  int buffer_size;
  struct lock buffer_lock;
  struct condition buffer_not_empty;
  struct condition buffer_not_full;
  int buffer_reading_pos;
  int buffer_writing_pos;

  /* Output. */
  struct lock output_lock;    /* Lock protecting output buffer. */
  char *output_pos;           /* Current position in output buffer. */
};

struct producer_data
{
  struct producer_consumer_test *test;
  int producer_id;
};

struct consumer_data
{
  struct producer_consumer_test *test;
  int consumer_id;
};

static void producer (void *);
static void consumer (void *);


void producer_consumer(unsigned int num_producer, unsigned int num_consumer)
{
  struct producer_consumer_test test;
  struct producer_data *pdata;
  struct consumer_data *cdata;
  char *output;
  unsigned int i;

  msg ("Creating %d producer threads and %d consumer threads.", num_producer, num_consumer);

  /* Allocate memory. */
  const size_t output_len = strlen(PRODUCER_MESSAGE) * num_producer;
  pdata = malloc (sizeof *pdata * num_producer);
  cdata = malloc (sizeof *cdata * num_consumer);
  output = malloc (sizeof *output * (output_len + 1));
  if (pdata == NULL || cdata == NULL || output == NULL)
    PANIC ("couldn't allocate memory for test");

  /* Initialize test. */
  test.buffer_size = 0;
  test.buffer_reading_pos = 0;
  test.buffer_writing_pos = 0;
  lock_init (&test.buffer_lock);
  cond_init (&test.buffer_not_empty);
  cond_init (&test.buffer_not_full);
  lock_init (&test.output_lock);
  test.output_pos = output;

  /* Initialize output. */
  output[output_len] = '\0';

  /* Start threads. */
  for (i = 0; i < num_producer; i++)
    {
      struct producer_data *d = pdata + i;
      char name[16];

      d->test = &test;
      d->producer_id = i;

      snprintf (name, sizeof name, "producer %d", i);
      thread_create (name, PRI_DEFAULT, producer, d);
    }
  for (i = 0; i < num_consumer; i++)
    {
      struct consumer_data *d = cdata + i;
      char name[16];

      d->test = &test;
      d->consumer_id = i;

      snprintf (name, sizeof name, "consumer %d", i);
      thread_create (name, PRI_DEFAULT, consumer, d);
    }

  /* Wait long enough for all the threads to finish. */
  timer_sleep (50 + (num_producer + num_consumer) * 5 + 50);

  /* Acquire the output lock in case some rogue thread is still
     running. */
  lock_acquire (&test.output_lock);

  /* Print output. */
  ASSERT (strlen(output) == output_len);
  printf ("\n%s\n\n", output);

  free (output);
  free (cdata);
  free (pdata);
}


/* producer thread. */
static void
producer (void *d)
{
  struct producer_data *data = d;
  struct producer_consumer_test *test = data->test;
  const char *message = PRODUCER_MESSAGE, *p;

  msg("started Producer ID=%d", data->producer_id);

  for (p = message; *p != 0; ++p)
    {
      lock_acquire (&test->buffer_lock);
      while (test->buffer_size >= BOUNDED_BUFFER_SIZE)
        cond_wait (&test->buffer_not_full, &test->buffer_lock);
      test->buffer_data[test->buffer_writing_pos] = *p;
      test->buffer_size++;
      test->buffer_writing_pos = (test->buffer_writing_pos + 1) % BOUNDED_BUFFER_SIZE;
      cond_signal (&test->buffer_not_empty, &test->buffer_lock);
      lock_release (&test->buffer_lock);
    }
}

/* consumer thread. */
static void
consumer (void *d)
{
  struct consumer_data *data = d;
  struct producer_consumer_test *test = data->test;

  msg("started Consumer ID=%d", data->consumer_id);

  while (true)
    {
      char c;

      lock_acquire (&test->buffer_lock);
      while (test->buffer_size <= 0)
        cond_wait (&test->buffer_not_empty, &test->buffer_lock);
      c = test->buffer_data[test->buffer_reading_pos];
      test->buffer_size--;
      test->buffer_reading_pos = (test->buffer_reading_pos + 1) % BOUNDED_BUFFER_SIZE;
      cond_signal (&test->buffer_not_full, &test->buffer_lock);
      lock_release (&test->buffer_lock);

      lock_acquire (&test->output_lock);
      *(test->output_pos++) = c;
      lock_release (&test->output_lock);
    }
}


