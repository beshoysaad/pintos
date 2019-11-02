/**
 * Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 *
 * Specifications:
 * 1. Traffic may only cross the bridge in one direction at a time
 * 2. If there are ever more than 3 vehicles on the bridge at one time, it will collapse under their weight
 * 3. When an emergency vehicle wants to cross the bridge it should be allowed access as soon as possible
 * 4. Only semaphores are allowed
 */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"
#include "random.h"

#define DIRECTION_LEFT     	0
#define DIRECTION_RIGHT    	1

#define LOW_PRIORITY       	0
#define HIGH_PRIORITY	   	1

struct semaphore sema_cars;
struct semaphore sema_modify;

struct bridge_status
{
  int direction;
  unsigned num_vehicles;
  unsigned num_emergency_waiting;
};

struct bridge_status b;

struct car_params
{
  int direction;
  int priority;
};

void
narrow_bridge (unsigned int num_vehicles_left, unsigned int num_vehicles_right,
	       unsigned int num_emergency_left,
	       unsigned int num_emergency_right);

void
test_narrow_bridge (void)
{
//  narrow_bridge (0, 0, 0, 0);
//  narrow_bridge (1, 0, 0, 0);
//  narrow_bridge (0, 0, 0, 1);
//  narrow_bridge (0, 4, 0, 0);
//  narrow_bridge (0, 0, 4, 0);
//  narrow_bridge (3, 3, 3, 3);
//  narrow_bridge (4, 3, 4, 3);
//  narrow_bridge (7, 23, 17, 1);
//  narrow_bridge (40, 30, 0, 0);
//  narrow_bridge (30, 40, 0, 0);
//  narrow_bridge (23, 23, 1, 11);
//  narrow_bridge (22, 22, 10, 10);
//  narrow_bridge (0, 0, 11, 12);
//  narrow_bridge (0, 10, 0, 10);
  narrow_bridge (0, 10, 10, 0);
  pass ();
}

static void
ArriveBridgeEmergency (int direc)
{
  sema_down (&sema_modify);
  b.num_emergency_waiting++;
  sema_up (&sema_modify);
  while (true)
    {
      sema_down (&sema_cars);
      sema_down (&sema_modify);
      if ((b.num_vehicles > 0) && (b.direction != direc))
	{
	  sema_up (&sema_modify);
	  sema_up (&sema_cars);
	  continue;
	}
      else
	{
	  b.direction = direc;
	  b.num_vehicles++;
	  b.num_emergency_waiting--;
	  sema_up (&sema_modify);
	  return;
	}
    }
}

static void
ArriveBridgeRegular (int direc)
{
  while (true)//this should just print out a debug message upon entrance, sleep the thread for a random amount of time, and print
    {
      sema_down (&sema_cars);
      sema_down (&sema_modify);
      if ((b.num_emergency_waiting > 0)
	  || ((b.num_vehicles > 0) && (b.direction != direc)))
	{
	  sema_up (&sema_modify);
	  sema_up (&sema_cars);
	  continue;
	}
      else
	{
	  b.direction = direc;
	  b.num_vehicles++;
	  sema_up (&sema_modify);
	  return;
	}
    }
}

/**
 * block the thread until it is safe for the car to cross the bridge in the given direction
 */
static void
ArriveBridge (int direc, int prio)
{
  if (prio == HIGH_PRIORITY)
    {
      ArriveBridgeEmergency (direc);
    }
  else
    {
      ArriveBridgeRegular (direc);
    }
}

/**
 * print out a debug message upon entrance, sleep the thread for a random amount of time,
 * and print another debug message upon exit
 */
static void
CrossBridge (int direc, int prio)
{
  char *priority = (prio == HIGH_PRIORITY ? "Emergency" : "Regular");
  char *direction = (direc == DIRECTION_LEFT ? "left" : "right");
  printf ("%s vehicle entered bridge in %s direction.\r\n", priority,
	  direction);
  timer_sleep ((random_ulong () % 400) + 100);
  printf ("%s vehicle exited bridge in %s direction.\r\n", priority, direction);
}

static void
ExitBridge (int direc UNUSED, int prio UNUSED)
{
  sema_down (&sema_modify);
  b.num_vehicles--;
  sema_up (&sema_modify);
  sema_up (&sema_cars);
}

static void
OneVehicle (int direc, int prio)
{
  ArriveBridge (direc, prio);
  CrossBridge (direc, prio);
  ExitBridge (direc, prio);
}

static void
car_thread (void *arg)
{
  struct car_params *p = (struct car_params*) arg;
  OneVehicle (p->direction, p->priority);
  free (p);
}

void
narrow_bridge (unsigned int num_vehicles_left, unsigned int num_vehicles_right,
	       unsigned int num_emergency_left,
	       unsigned int num_emergency_right)
{
  sema_init (&sema_cars, 3);
  sema_init (&sema_modify, 1);

  if (num_emergency_left > num_emergency_right)
    {
      for (unsigned int i = 0; i < num_emergency_left; i++)
	{
	  struct car_params *p = (struct car_params*) malloc (
	      sizeof(struct car_params));
	  p->direction = DIRECTION_LEFT;
	  p->priority = HIGH_PRIORITY;
	  thread_create ("", PRI_DEFAULT, car_thread, (void*) p);
	}
      for (unsigned int i = 0; i < num_emergency_right; i++)
	{
	  struct car_params *p = (struct car_params*) malloc (
	      sizeof(struct car_params));
	  p->direction = DIRECTION_RIGHT;
	  p->priority = HIGH_PRIORITY;
	  thread_create ("", PRI_DEFAULT, car_thread, (void*) p);
	}

    }
  else
    {
      for (unsigned int i = 0; i < num_emergency_right; i++)
	{
	  struct car_params *p = (struct car_params*) malloc (
	      sizeof(struct car_params));
	  p->direction = DIRECTION_RIGHT;
	  p->priority = HIGH_PRIORITY;
	  thread_create ("", PRI_DEFAULT, car_thread, (void*) p);
	}
      for (unsigned int i = 0; i < num_emergency_left; i++)
	{
	  struct car_params *p = (struct car_params*) malloc (
	      sizeof(struct car_params));
	  p->direction = DIRECTION_LEFT;
	  p->priority = HIGH_PRIORITY;
	  thread_create ("", PRI_DEFAULT, car_thread, (void*) p);
	}
    }

  for (unsigned int i = 0; i < num_vehicles_left; i++)
    {
      struct car_params *p = (struct car_params*) malloc (
	  sizeof(struct car_params));
      p->direction = DIRECTION_LEFT;
      p->priority = LOW_PRIORITY;
      thread_create ("", PRI_DEFAULT, car_thread, (void*) p);
    }
  for (unsigned int i = 0; i < num_vehicles_right; i++)
    {
      struct car_params *p = (struct car_params*) malloc (
	  sizeof(struct car_params));
      p->direction = DIRECTION_RIGHT;
      p->priority = LOW_PRIORITY;
      thread_create ("", PRI_DEFAULT, car_thread, (void*) p);
    }
}

