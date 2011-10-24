/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Basic test for EcOs objects */

#include "ec_os.h"
#include "ec_uart.h"

EcTask t1, t2, t3, t4;
EcSemaphore sem;
EcSwi swi;
EcTimer timer1, timer2;
EcEvent ev1, ev2;


void Thread1(void* arg) {
  int i;

  for (i = 0; i < 5; i++) {
    EcSemaphoreWait(&sem, EC_OS_FOREVER);
    /* Do some work */
    EcTaskSleep(5000);
    EcUartPrintf("Hello from thread1: %s\n", (char*)arg);
    EcSemaphorePost(&sem);

    /* Two rapid posts to SWI, to see that they merge */
    EcSwiPost(&swi, 1 << i);
    EcSwiPost(&swi, 0x100 << i);

    EcTaskSleep(100);
  }

  EcTaskSleep(500000);
  EcUartPrintf("Goodbye from thread1\n");
}


void Thread2(void* arg) {
  int i;

  for (i = 0; i < 5; i++) {
    EcSemaphoreWait(&sem, EC_OS_FOREVER);
    /* Do some work */
    EcTaskSleep(5000);
    EcUartPrintf("Hello from thread2: %s\n", (char*)arg);
    EcSemaphorePost(&sem);

    /* Post events */
    EcEventPost(&ev1, 1 << i);
    EcEventPost(&ev2, 1 << i);

    EcTaskSleep(100);
  }

  EcTaskSleep(50000);
  EcUartPrintf("Goodbye from thread2\n");
}


void Thread3(void* arg) {
  uint32_t got_bits = 0;

  while(got_bits != 0x10) {
    /* Wait for any of the bits to be set */

    EcEventWaitAny(&ev1, 0x1c, &got_bits, EC_OS_FOREVER);
    EcUartPrintf("Event thread 3 got bits: 0x%x\n", got_bits);
  }
  EcUartPrintf("Goodbye from event thread 3\n");
}


void Thread4(void* arg) {
  /* Wait on event bit from creation and a few posted bits. */
  EcEventWaitAll(&ev2, 0x10e, EC_OS_FOREVER);
  EcUartPrintf("Event thread 4 got all bits\n");
  EcUartPrintf("Goodbye from event thread 4\n");
}


void SwiFunc(void* arg, uint32_t bits) {
  EcUartPrintf("Hello from SWI with bits=0x%x\n", bits);
}


void TimerFunc(void* arg) {
  EcUartPrintf("Hello from timer: %s\n", (char*)arg);
  /* Start the one-shot timer. */
  EcTimerStart(&timer2);
}


void OneTimerFunc(void* arg) {
  EcUartPrintf("Hello from one-shot timer: %s\n", (char*)arg);
  /* Stop the periodic timer */
  EcTimerStop(&timer1);
}


int main(void) {
  EcOsInit();
  EcUartInit();

  EcUartPrintf("Hello, world.\n");

  EcTaskCreate(&t1, EC_TASK_PRIORITY_DEFAULT, 0, Thread1, "Foo1");
  EcTaskCreate(&t2, EC_TASK_PRIORITY_DEFAULT, 0, Thread2, "Foo2");
  EcTaskCreate(&t3, EC_TASK_PRIORITY_DEFAULT, 0, Thread3, "EventTask1");
  EcTaskCreate(&t4, EC_TASK_PRIORITY_DEFAULT, 0, Thread4, "EventTask2");

  EcSwiCreate(&swi, EC_SWI_PRIORITY_DEFAULT, SwiFunc, "Swi1");
  EcTimerCreate(&timer1, 100000, EC_TIMER_PRIORITY_DEFAULT,
                EC_TIMER_FLAG_STARTED|EC_TIMER_FLAG_PERIODIC,
                TimerFunc, "Timer1");
  EcTimerCreate(&timer2, 150000, EC_TIMER_PRIORITY_DEFAULT,
                0, OneTimerFunc, "Timer2");
  EcSemaphoreCreate(&sem, 1);
  EcEventCreate(&ev1, 0);
  EcEventCreate(&ev2, 0x100);

  EcUartPrintf("EcOs objects created.\n");

  EcOsStart();

  return 0;
}
