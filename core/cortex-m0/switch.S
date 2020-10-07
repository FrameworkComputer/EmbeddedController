/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Context swtching
 */

#include "config.h"

#define CPU_SCB_ICSR		0xe000ed04

.text

.syntax unified
.code 16

/**
 * Task context switching
 *
 * Change the task scheduled after returning from the exception.
 *
 * Save the registers of the current task below the exception context on
 * its task, then restore the live registers of the next task and set the
 * process stack pointer to the new stack.
 *
 * r0: pointer to the task to switch from
 * r1: pointer to the task to switch to
 *
 * must be called from interrupt context
 *
 * the structure of the saved context on the stack is :
 *     r8, r9, r10, r11, r4, r5, r6, r7, r0, r1, r2, r3, r12, lr, pc, psr
 *     additional registers            <|> exception frame
 */
.global __switchto
.thumb_func
__switchto:
  mrs r2, psp            @ get the task stack where the context has been saved
  mov r3, sp
  mov sp, r2
  push {r4-r7}           @ save additional r4-r7 in the task stack
  mov r4, r8
  mov r5, r9
  mov r6, r10
  mov r7, r11
  push {r4-r7}           @ save additional r8-r11 in the task stack
  mov r2, sp             @ prepare to save former task stack pointer
  mov sp, r3             @ restore system stack pointer
  str r2, [r0]           @ save the task stack pointer in its context
  ldr r2, [r1]           @ get the new scheduled task stack pointer
  ldmia r2!, {r4-r7}     @ restore r8-r11 for the next task context
  mov r8, r4
  mov r9, r5
  mov r10, r6
  mov r11, r7
  ldmia r2!, {r4-r7}     @ restore r4-r7 for the next task context
  msr psp, r2            @ set the process stack pointer to exception context
  bx lr                  @ return from exception

/**
 * Start the task scheduling.  r0 is a pointer to task_stack_ready, which is
 * set to 1 after the task stack is set up.
 */
.global __task_start
.thumb_func
__task_start:
  ldr  r2,=scratchpad     @ area used as thread stack for the first switch
  movs r3, #2             @ use : priv. mode / thread stack / no floating point
  adds r2, #17*4          @ put the pointer at the top of the stack
  movs r1, #0             @ __Schedule parameter : re-schedule nothing
  msr  psp, r2            @ setup a thread stack up to the first context switch
  movs r2, #1             @ r2 = TASK_SCHEDULER_INIT
  isb                     @ ensure the write is done
  msr control, r3
  movs r3, r0
  movs r0, #0             @ __Schedule parameter : de-schedule nothing
  isb                     @ ensure the write is done
  str  r2, [r3]           @ Task scheduling is now active
  bl __schedule           @ execute the task with the highest priority
  /* we should never return here */
  movs r0, #1             @ set to EC_ERROR_UNKNOWN
  bx lr

/**
 * SVC exception handler
 */
.global svc_handler
.thumb_func
svc_handler:
  push {r3, lr}              @ save link register and keep stack aligned
  bl __svc_handler           @ call svc handler helper
  ldr r3,=current_task       @ load the current task's address
  ldr r1, [r3]               @ load the current task
  cmp r0, r1                 @ compare with previous task returned by helper
  beq svc_handler_return     @ return if they are the same
  bl __switchto              @ context switch to the next task
svc_handler_return:
  pop {r3, pc}               @ return from exception or return to caller

/**
 * PendSVC exception handler
 */
.global pendsv_handler
.thumb_func
pendsv_handler:
  push {r3, lr}              @ save link register and keep stack aligned
  ldr r0, =#CPU_SCB_ICSR     @ load CPU_SCB_ICSR's address
  movs r1, #1                @ prepare left shift (1 << 27)
  lsls r1, #27               @ shift the bit
  str r1, [r0]               @ clear pending flag
  cpsid i                    @ ensure we have priority 0 during re-scheduling
  movs r1, #0                @ desched nothing
  movs r0, #0                @ resched nothing
  bl svc_handler             @ re-schedule the highest priority task
  cpsie i                    @ leave priority 0
  pop {r3, pc}               @ return from exception
