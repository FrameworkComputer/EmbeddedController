Chromium OS Embedded Controller runtime
=======================================

Design principles
-----------------

  1. Never do at runtime what you can do at compile time
The goal is saving flash space and computations.
Compile-time configuration until you really need to switch at runtime.

  2. Real-time: guarantee low latency (eg < 20 us)
no interrupt disabling ...
bounded code in interrupt handlers.

  3. Keep it simple: design for the subset of microcontroller we use
targeted at 32-bit single core CPU
for small systems : 4kB to 64kB data RAM, possibly execute-in-place from flash.

Execution contexts
------------------

This is a pre-emptible runtime with static tasks.
It has only 2 possible execution contexts:

- the regular [tasks](#tasks)
- the [interrupt handlers](#interrupts)

The initial startup is an exception as described in the
[dedicated paragraph](#Startup).

### tasks

The tasks are statically defined at compile-time.
They are described for each *board* in the
[board/$board/ec.tasklist](../board/host/ec.tasklist) file.

They also have a static fixed priority implicitly defined at compile-time by
their order in the [ec.tasklist](../board/host/ec.tasklist) file (the top-most
one being the lowest priority aka *task* *1*).
As a consequence, two different tasks cannot have the same priority.

In order to store its context, each task has its own stack whose (*small*) size
is defined at compile-time in the [ec.tasklist](../board/host/ec.tasklist) file.

A task can normally be preempted at any time by either interrupts or higher
priority tasks, see the [preemption section](#scheduling-and-preemption) for
details and the [locking section](#locking-and-atomicity) for the few cases
where you need to avoid it.

### interrupts

The hardware interrupt requests are connected to the interruption handling
*C* routines declared by the `DECLARE_IRQ` macros, through some chip/core
specific mechanisms (e.g. depending whether we have a vectored interrupt
controller, slave interrupt controllers...)

The interrupts can be nested (ie interrupted by a higher priority interrupt).
All the interrupt vectors are assigned a priority as defined in their
`DECLARE_IRQ` macro. The number of available priority level is
architecture-specific (e.g. 4 on Cortex-M0, 8 on Cortex-M3/M4) and several
interrupt handlers can have the same priority. An interrupt handler can only be
interrupted by an handler having a priority **strictly** **greater** than
its own.

In most cases, the exceptions (e.g data/prefetch aborts, software interrupt) can
be seen as interrupts with a priority strictly greater than all IRQ vectors.
So they can interrupt any IRQ handler using the same nesting mechanism.
All fatal exceptions should ultimately lead to a reboot.

### Events

Each task has a *pending* events bitmap[1] implemented as a 32-bit word.
Several events are pre-defined for all tasks, the most significant bits on the
32-bit bitmap are reserved for them : the timer pending event on bit 31
([see the corresponding section](#Timers)), the requested task wake (bit 29),
the event to kick the waiters on a mutex (bit 30), along with a few hardware
specific events.
The 19 least significant bits are available for task-specific meanings.

Those event bits are used in inter-task communication and scheduling mechanism,
other tasks **and** interrupt handlers can atomically set them to request
specific actions from the task. Therefore, the presence of pending events in a
task bitmap has an impact on its scheduling as described in the [scheduling
section](#scheduling-and-preemption).
These requests are done using the `task_set_event()` and `task_wake()`
primitives.

The two typical use-cases are:

- a task sends a message to another task (simply use some common memory
  structures [see explanation](#single-address-space) and want it to process
  it now.
- an hardware IRQ occurred and we need to do some long processing to respond to
  it (e.g. an I2C transaction). The associated interrupt handler cannot do it
  (for latency reason), so it will raise an event to ask a task to do it.

The task code chooses to consume them (or a subset of them) when it's running
through the `task_wait_event()` and `task_wait_event_mask()` primitives.

### Scheduling and preemption

The system has a global bitmap[1] called `tasks_ready` containing one bit
per task and indicating whether or not it is *ready* *to* *run*
(ie want/need to be scheduled).
The task ready bit can only be cleared when it's calling itself one of the
functions explicitly triggering a re-scheduling (e.g. `task_wait_event()`
or `task_set_event()`) **and** it has no pending event.
The task ready bit is set by any task or interrupt handler setting an event
bit for the task (ie `task_set_event()`).

The scheduling is based on (and *only* on) the `tasks_ready` bitmap
(which is derived from all the events bitmap of the tasks as explained above).

Then, the scheduling policy to find which task should run is just finding the
most significant bit set in the tasks_ready bitmap and schedule the corresponding task.

Important note: the re-scheduling happens **only** when we are exiting the interrupt context.
It is done in a non-preemptible context (likely with the highest priority).
Indeed, a re-scheduling is actually needed only when the highest priority task ready has changed.
There are 3 distinct cases where this can happen:

- an interrupt handler sets a new event for a task.
  In this case, `task_set_event` will detect that it is executed in interrupt
  context and record in the `need_resched_or_profiling` variable that it might
  need to re-schedule at interrupt return. When the current interrupt is going
  to return, it will see this bit and decide to take the slow path making a new
  scheduling decision and eventually a context switch instead of the fast path
  returning to the interrupt task.
- a task sets an event on another task.
  The runtime will trigger a software interrupt to force a re-scheduling at its
  exit.
- the running task voluntarily relinguish its current execution rights by
  calling `task_wait_event()` or a similar function.
  This will call the software interrupt similarly to the previous case.

On the re-scheduling path, if the highest-priority ready task is not matching
the currently running one, it will perform a context-switch by saving all the
processor registers on the current task stack, switch the stack pointer to the
newly scheduled task, and restore the registers from the previously saved
context from there.

### hooks and deferred function

The lowest priority task (ie Task 1, aka TASK_ID_HOOKS) is reserved to execute
repetitive actions and future actions deferred in time without blocking the
current task or creating a dedicated task (whose stack memory allocation would
be wasting precious RAM).

The HOOKS task has a list of deferred functions and their next deadline.
Every time it is waken up, it runs through the list and calls the ones whose
deadline is expired. Before going back to sleep, it arms a timer to the closest
deadline.
The deferred functions can be created using the `DECLARED_DEFERRED()` macro.
Similarly the HOOK_SECOND and HOOK_TICK hooks are called periodically by the
HOOKS task loop (the *tick* duration is platform-defined and shorter than
the second).

Note: be specially careful about priority inversions when accessing resources
protected by a mutex (e.g. a shared I2C controller) in a deferred function.
Indeed being the lowest priority task, it might be de-scheduled for long time
and starve higher priority tasks trying to access the resource given there is
no priority boosting implemented for this case.
Also be careful about long delays (> x 100us) in hook or deferred function
handlers, since those will starve other hooks of execution time. It is better
to implement a state machine where you set up a subsequent call to a deferred
function than have a long delay in your handler.

### watchdog

The system is always protected against misbehaving tasks and interrupt handlers
by a hardware watchdog rebooting the CPU when it is not attended.

The watchdog is petted in the HOOKS task, typically by declaring a HOOK_TICK
doing it as regular intervals. Given this is the lowest priority task,
this guarantees that all tasks are getting some run time during the watchdog
period.

Note: that's also why one should not sprinkle its code with `watchdog_reload()`
to paper over long-running routine issues.

To help debugging bad sequences triggering watchdog reboots, most platforms
implement a warning mechanism defined under `CONFIG_WATCHDOG_HELP`.
It's a timer firing at the middle of the watchdog period if it hasn't been
petted by then, and dumping on the console the current state of the execution
mainly to help finding a stuck task or handler. The normal execution is resumed
though after this alert.

### Startup

The startup sequence goes through the following steps:

- the assembly entry routine clears the .bss (uninitialized data),
  copies the initialized data (and optionally the code if we are not executing
  from flash), sets a stack pointer.
- we can jump to the `main()` C routine at this point.
- then we go through the hardware pre-init (before we have all the clocks to
 run the peripherals normal) and init routines, in this rough order:
   memory protection if any, gpios in their default state,
   prepare the interrupt controller, set the clocks, then timers,
   enable interrupts, init the debug UART and the watchdog.
- finally start tasks.

For the tasks startup, initially only the HOOKS task is marked as ready,
so it is the first to start and can call all the HOOK_INIT handlers performing
initializations before actually executing any real task code.
Then all tasks are marked as ready, and the highest priority one is given
the control.

During all the startup sequence until the control is given the first task,
we are using a speciak stack called 'system stack' which will be later re-used
as the interrupts and exception stack.

To prepare the first context switch, the code in `task_pre_init()` is stuffing
all the tasks stacks with a *fake* saved context whose program counter is
containing the task start address and the stack pointer is pointing to its
reserved stack space.

### locking and atomicity

The two main concurrency primitives are lightweight atomic variables and
heavier mutexes.

The atomic variables are 32-bit integers (which can usually be loaded/stored
atomically on the architecture we are supporting). The `atomic.h` headers
include primitives to do atomically various bit and arithmetic operations
using either load-linked/load-exclusive, store-conditional/store-exclusive
or simple depending what is available.

The mutexes are actually statically allocated binary semaphores.
In case of contention, they will make the waiting task sleep
(removing its ready bit) and use the [event mechanism](#Events) to wake-up
the other waiters on unlocking.

Note: the mutexes are NOT triggering any priority boosting to avoid the
priority inversion phenomenon.

Given the runtime is running on single core CPU, spinlocks would be equivalent
to masking interrupts with `interrupt_disable()` spinlocks, but it's
strongly discouraged to avoid harming the real-time characterics of the runtime.

Time
----

### time keeping

In the runtime, the time is accounted everywhere using a
**64-bit** **microsecond** count since the microcontroller **cold** **boot**.

Note: The runtime has no notion of wall-time/date, even though a few platform have
an RTC inside the microcontroller.

These microsecond timestamps are implemented in the code using the `timestamp_t`
type and the current timestamp is returned by the `get_time()` function.

The time-keeping is preferably implemented using a 32-bit hardware
free running counter at 1Mhz plus a 32-bit word in memory keeping track of
the high word of the 64-bit absolute time. This word is incremented by the
32-bit timer rollback interrupt.

Note: as a consequence of this implementation, when the 64-bit timestamp is read
in interrupt context in an handler having a higher priority than the timer IRQ
(which is somewhat rare), the high 32-bit word might be incoherent (off by one).

### timer event

The runtime offers *one* (and only one) timer per task.
All the task timers are multiplexed on a single hardware timer.
(can be just a *match* *interrupt* on the free running counter mentioned in the
[previous paragraph](#time-keeping))
Every time a timer is armed or expired, the runtime finds the task timer having
the closest deadline and programs it in the hardware to get an interrupt.
At the same time, it sets the TASK_EVENT_TIMER event in all tasks whose timer
deadline has expired.
The next deadline is computed in interrupt context.

Note: given each task has a **single** timer which is also used to wake-up the
task when `task_wait_event()` is called with a timeout, one needs to be careful
when using directly the `timer_arm()` function because there is an eventuality
that this timer is still running on the next `task_wait_event()` call, the call
will fail due to the lack of available timer.

Memory
------

### Single address space

There is no memory isolation between tasks (ie they all live in the same address
space). Some architectures implement memory protection mechanism albeit only to
differentiate executable area (eg `.code`) from writable area (eg `.bss` or
`.data`) as there is a **single** **privilege** level for all execution contexts.

As all the memory is implicitely shared between the task, the inter-task
communication can be done by simply writing the data structures in memory
and using events to wake the other task (given we properly thought the concurrent
accesses on thoses structures).

### heap

The data structure should be statically allocated at compile time.

Note: there is no dynamic allocator available (e.g. `malloc()`), not due to
impossibility to create one but to avoid the negative side effects of
having one: ie poor/unpredictable real-time behavior and possible leaks
leading to a long-tail of failures.

- TODO: talk about shared memory
- TODO: where/how we store *panic* *memory* and *sysjump* *parameters*.

### stacks

Each task has its own stack, in addition there is a system stack used for
startup and interrupts/exceptions.

Note 1: Each task stack is relatively small (e.g. 512 bytes), so one needs to
be careful about stack usage when implementing features.

Note 2: At the same time, the total size of RAM used by stacks is a big chunk
of the total RAM consumption, so their sizes need to be carefully tuned.
(please refer to the [debugging paragraph](#debugging) for additional input on
this topic.

## Firmware code organization and multiple copies

- TODO: Details the classical RO / RW partitions and how we sysjump.

power management
----------------

- TODO: talk about the idle task + WFI (note: interrupts are disabled!)
- TODO: more about low power idle and the sleep-disable bitmap
- TODO: adjusting the microsecond timer at wake-up

debugging
---------

- TODO: our main tool: serial console ...
(but non-blocking / discard overflow, cflush DO/DONT)
- TODO: else JTAG stop and go: careful with watchdog and timer
- TODO: panics and software panics
- TODO: stack size tuning and canarying


- TODO: Address the rest of the comments from https://crrev.com/c/445941

[1]: bitmap: array of bits.
