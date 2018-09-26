/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

.text

.syntax unified
.code 16

.global ite_sync
.thumb_func

/*
 * The logic shown by the following C code is implemented in assembly below.
 *
 * Two square wave sequences are generated, one oscillating twice as fast as
 * the other. The ite_sync_clock() function implements tick counter. The higher
 * frequency sequence's half period time takes 'half_period_ticks' cycles of
 * the ite_sync_clock() inner loop. So, to generate two sequences, one half
 * the frequency of the other four half cycles are required.
 *
 * Two GPIOs used as who outputs of the two clocks. Luckily both GPIOs can be
 * controlled by the same SOC register, the input values of 'both_zero',
 * 'one_zero', 'zero_one', and 'both_one' are the values to be written
 * into the register to set the two GPIOs to the respecitve values.
 *

static uint32_t tick;               # Free running tick counter, runs from
	                            # 0 to 'total_ticks_required'
static uint32_t next_tick;          # Deadline for the next half period
static uint32_t half_period_ticks;  # Number of ticks per half period.

static void ite_sync_clock(void)
{
	while (tick++ < next_tick)
		;
	next_tick += half_period_ticks ;
}

void ite_sync(volatile uint16_t *gpio_addr, uint16_t both_zero,
	uint16_t one_zero, uint16_t zero_one, uint16_t both_one,
	uint32_t half_period, uint32_t total_ticks_required)
{
	uint32_t tick = 0;
	uint32_t next_tick = half_period_ticks = half_period;

	while (tick < total_ticks_required) {
		ite_sync_clock() ;
		*gpio_addr = both_zero;

		ite_sync_clock() ;
		*gpio_addr = one_zero;

		ite_sync_clock() ;
		*gpio_addr = zero_one;

		ite_sync_clock() ;
		*gpio_addr = both_one;
	}
}
*/

.thumb_func
ite_sync_clock:
	@ ip tick
	@ r7 next_tick
	@ r5 half_period_ticks
	add  ip, ip, #1
	cmp  ip, r7
	bcc  ite_sync_clock
	add  r7, r7, r5
	mov  r8, r8        @ a few NOOPs to fine tune the period.
	mov  r8, r8
	bx   lr

.thumb_func
.global ite_sync
ite_sync:
	@ vvvvvv passed in registers: vvvvv
	@ r0 gpio_addr
	@ r1 both_zero
	@ r2 one_zero
	@ r3 zero_one
	@ vvvvvv passed on the stack, moved to registers: vvvvv
	@ r4 both_one
	@ r5 half_period
	@ r6 total_ticks_required
	@ vvvvvv local variables: vvvvv
	@ r7 next_tick
	@ ip tick

	push	{r4, r5, r6, r7, lr}

	ldr  r4, [sp, #20]   @ both one
	ldr  r5, [sp, #24]   @ half_period_ticks
	ldr  r6, [sp, #28]   @ total_ticks_required

	mov  ip, #0          @ tick counter
	mov  r7, r5          @ next tick

sync_loop:
	bl   ite_sync_clock
	strh r1, [r0]        @ both_zero
	add  ip, ip, 1       @ cycle counter increment

	bl   ite_sync_clock
	strh r2, [r0]        @ one_zero
	mov  r8, r8

	bl   ite_sync_clock
	strh r3, [r0]        @ zero_one
	mov  r8, r8

	bl   ite_sync_clock
	strh r4, [r0]        @ both_one

	cmp  ip, r6
	bcc  sync_loop

	pop  {r4, r5, r6, r7, pc}

