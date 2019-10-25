/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"


void producer_consumer(unsigned int num_producer, unsigned int num_consumer);

#define producer_pority 1
#define comsumer_protity 1
#define NOTFULL 0
#define NOTEMPTY 1

char hello_str[20] = "hello world";
char output_str[20];
bool flag;
struct lock mutex;
struct condition notfull;
struct condition notempty;
int in=0, out=0;
int spaces=11,str=0;


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

void *producer(void *arg)
{
   while(true)
   {
      lock_acquire(&mutex); 
      while(spaces==0)
      {
         cond_wait(&notfull, &mutex);
      }
       
      if(in<11)
      {
        output_str[in] = hello_str[in];
        in++;
        spaces--;
        str++;
        cond_signal(&notempty, &mutex);
        lock_release(&mutex);
     }
     else
     {
       lock_release(&mutex);
       break;
     }
   }

}

void *consumer(void *arg)
{
    while(true)
   {
      lock_acquire(&mutex);
      while(str==0)
      {
         cond_wait(&notempty, &mutex);
      }
      
      if(out<11)
      {
        printf("%c",hello_str[out]);
        out++;
        str--;
        spaces++;
        cond_signal(&notfull, &mutex);
        lock_release(&mutex);
      }
      
      else
      {
        lock_release(&mutex);
        break;
      }
   }
   
}



void producer_consumer(UNUSED unsigned int num_producer, UNUSED unsigned int num_consumer)
{
    //msg("NOT IMPLEMENTED");
    /* FIXME implement */
    char *base_pro = "produce";
    char *base_con = "consume";
    char *name;

    lock_init(&mutex);

    cond_init(&notfull);
    cond_init(&notempty);
    flag = true;//the flag for starting to work 
    /*create threads for producer and consumer*/
    for(int i=0;i<num_producer;i++)
    {
       thread_create("produce_"+(char)i, producer_pority,  producer, NULL); 
    }
    for(int i=0;i<num_consumer;i++)
    {
       thread_create("comsume_"+(char)i, comsumer_protity,  consumer, NULL);
    }
    
    

}


