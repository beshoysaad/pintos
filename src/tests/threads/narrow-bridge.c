/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"


void narrow_bridge(unsigned int num_vehicles_left, unsigned int num_vehicles_right,
        unsigned int num_emergency_left, unsigned int num_emergency_right);


void test_narrow_bridge(void)
{
    /*narrow_bridge(0, 0, 0, 0);
    narrow_bridge(1, 0, 0, 0);
    narrow_bridge(0, 0, 0, 1);
    narrow_bridge(0, 4, 0, 0);
    narrow_bridge(0, 0, 4, 0);
    narrow_bridge(3, 3, 3, 3);
    narrow_bridge(4, 3, 4 ,3);
    narrow_bridge(7, 23, 17, 1);
    narrow_bridge(40, 30, 0, 0);
    narrow_bridge(30, 40, 0, 0);
    narrow_bridge(23, 23, 1, 11);
    narrow_bridge(22, 22, 10, 10);
    narrow_bridge(0, 0, 11, 12);
    narrow_bridge(0, 10, 0, 10);*/
    narrow_bridge(0, 10, 10, 0);
    pass();
}


#define DIRECTION_LEFT      (0)
#define DIRECTION_RIGHT     (1)
#define PRIORITY_NORMAL     (0)
#define PRIORITY_EMERGENCY  (1)

#define BRIDGE_CAPACITY     (3)


/* Information about the test. */
struct narrow_bridge_test 
{
  struct semaphore bridge_vehicles;
  struct semaphore bridge_priority_left;
  struct semaphore bridge_priority_right;
  struct semaphore bridge_lock;
  int current_direction;
};

struct vehicle_data
{
  struct narrow_bridge_test *test;
  int vehicle_id;
  int direction;
  int priority;
};

static void vehicle (void *);


void narrow_bridge(unsigned int num_vehicles_left, unsigned int num_vehicles_right,
        unsigned int num_emergency_left, unsigned int num_emergency_right)
{
  struct narrow_bridge_test test;
  struct vehicle_data *vdata;
  unsigned int i, k = 0;

  msg ("Creating %d left-vehicle threads "
            "and %d right-vehicle threads "
            "and %d left-emergency threads "
            "and %d right-emergency threads.",
      num_vehicles_left, num_vehicles_right,
      num_emergency_left, num_emergency_left);

  /* Allocate memory. */
  int vehicles_total = num_vehicles_left
                     + num_vehicles_right
                     + num_emergency_left
                     + num_emergency_right;
  vdata = malloc (sizeof *vdata * vehicles_total);
  if (vdata == NULL)
    PANIC ("couldn't allocate memory for test");

  /* Initialize test. */
  sema_init (&test.bridge_vehicles, BRIDGE_CAPACITY);
  sema_init (&test.bridge_priority_left, 0);
  sema_init (&test.bridge_priority_right, 0);
  sema_init (&test.bridge_lock, 1);
  test.current_direction = DIRECTION_LEFT;

    /* Start threads. */
  for (i = 0; i < num_vehicles_left; i++, k++)
    {
      struct vehicle_data *d = vdata + k;
      char name[16];

      d->test = &test;
      d->vehicle_id = k;
      d->direction = DIRECTION_LEFT;
      d->priority = PRIORITY_NORMAL;

      snprintf (name, sizeof name, "vecl-l %d", i);
      thread_create (name, PRI_DEFAULT, vehicle, d);
    }
  for (i = 0; i < num_vehicles_right; i++, k++)
    {
      struct vehicle_data *d = vdata + k;
      char name[16];

      d->test = &test;
      d->vehicle_id = k;
      d->direction = DIRECTION_RIGHT;
      d->priority = PRIORITY_NORMAL;

      snprintf (name, sizeof name, "vecl-r %d", i);
      thread_create (name, PRI_DEFAULT, vehicle, d);
    }
  for (i = 0; i < num_emergency_left; i++, k++)
    {
      struct vehicle_data *d = vdata + k;
      char name[16];

      d->test = &test;
      d->vehicle_id = k;
      d->direction = DIRECTION_LEFT;
      d->priority = PRIORITY_EMERGENCY;

      snprintf (name, sizeof name, "emrg-l %d", i);
      thread_create (name, PRI_MAX, vehicle, d);
    }
  for (i = 0; i < num_emergency_right; i++, k++)
    {
      struct vehicle_data *d = vdata + k;
      char name[16];

      d->test = &test;
      d->vehicle_id = k;
      d->direction = DIRECTION_RIGHT;
      d->priority = PRIORITY_EMERGENCY;

      snprintf (name, sizeof name, "emrg-r %d", i);
      thread_create (name, PRI_MAX, vehicle, d);
    }

  /* Wait long enough for all the threads to finish. */
  timer_sleep (200 + vehicles_total * 20 + 200);

  free (vdata);
}


/* vehicle arrive method.
   Corresponds to `ArriveBridge' in the assignment. */
static void
vehicle_arrive (struct vehicle_data *data)
{
  struct narrow_bridge_test *test = data->test;
  struct semaphore *this_priority_direction;
  struct semaphore *other_priority_direction;

  switch (data->direction)
    {
    case DIRECTION_LEFT:
      this_priority_direction = &test->bridge_priority_left;
      other_priority_direction = &test->bridge_priority_right;
      break;
    case DIRECTION_RIGHT:
      this_priority_direction = &test->bridge_priority_right;
      other_priority_direction = &test->bridge_priority_left;
      break;
    default:
      PANIC ("unkown direction value");
    }

  if (data->priority == PRIORITY_EMERGENCY)
    sema_up (this_priority_direction);

  while (true)
    {
      sema_down (&test->bridge_lock);

      unsigned int current_vehicle_count = BRIDGE_CAPACITY
          - test->bridge_vehicles.value;
      unsigned int priority_waiters = this_priority_direction->value
          + other_priority_direction->value;

      bool gives_precedence = data->priority == PRIORITY_NORMAL
          && priority_waiters > 0;
      bool can_go = data->direction == test->current_direction
          || current_vehicle_count == 0;

      // we can move on only when current direction on the bridge
      // is ours, or the when the bridge is empty. And we should
      // not move on when there are priority cars waiting.
      if (can_go && !gives_precedence)
        {
          // overwrite the current bridge direction
          test->current_direction = data->direction;
          sema_down (&test->bridge_vehicles);
          if (data->priority == PRIORITY_EMERGENCY)
            sema_down (this_priority_direction);
          sema_up (&test->bridge_lock);
          break;
        }

      sema_up (&test->bridge_lock);
    }
}

/* vehicle cross method.
   Corresponds to `CrossBridge' in the assignment. */
static void
vehicle_cross (struct vehicle_data *data)
{
  msg("Vehicle ID=%d: entering bridge [direction=%d, priority=%d]",
      data->vehicle_id, data->direction, data->priority);
  
  timer_sleep (40);

  msg("Vehicle ID=%d: leaving bridge [direction=%d, priority=%d]",
      data->vehicle_id, data->direction, data->priority);
}

/* vehicle leave method.
   Corresponds to `ExitBridge' in the assignment. */
static void
vehicle_leave (struct vehicle_data *data)
{
  struct narrow_bridge_test *test = data->test;

  sema_up (&test->bridge_vehicles);
}

/* vehicle thread.
   Corresponds to `OneVehicle' in the assignment. */
static void
vehicle (void *d)
{
  struct vehicle_data *data = d;

  msg("started Vehicle ID=%d", data->vehicle_id);

  vehicle_arrive (data);
  vehicle_cross (data);
  vehicle_leave (data);
}
