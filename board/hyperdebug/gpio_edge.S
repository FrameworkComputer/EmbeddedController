/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Custom GPIO interrupt handling logic.
 */

#include "config.h"

.text

.syntax unified
.code 16

/*
 * Offsets of fields in struct cyclic_buffer_header_t.
 */
#define BUF_TAIL_TIME_OFF 0
#define BUF_HEAD_TIME_OFF 8
#define BUF_HEAD_OFF 12
#define BUF_TAIL_OFF 16
#define BUF_SIZE_OFF 20
#define BUF_OVERRUN_OFF 24
#define BUF_NUM_SIGNALS_OFF 25
#define BUF_SIGNAL_BITS_OFF 26
#define BUF_DATA_OFF 32

/*
 * Below is a list of data declaration in flash.  The compiler will generate
 * pc-relative instructions to access these fields from the code below.  The
 * idea being that when this code is copied into monitoring_slot_t[i].code for
 * all 16 array elements, then each copy of the code will access the fields from
 * "its own" entry in the array, without any indexing effort.
 */
my_slot:
cyclic_buffer_header:
	.long 117
gpio_base:
	.long 117
gpio_pin_mask:
	.long 117
gpio_signal:
	.long 117
head_level:
	.long 117
tail_level:
	.long 117
signal_no:
	.byte 117

	.byte 117
	.byte 117
	.byte 117
	.byte 117
	.byte 117
	.byte 117
	.byte 117

/*
 * Entry point for edge trigger interrupt handler.
 *
 * The routine keeps track of what was the last level seen on this signal.  It
 * will record one edge with current timestamp in the buffer, and then inspect
 * the current level of the signal, if identical to the last seen, then it must
 * be because (at least) two edges has happened since the last time the handler
 * ran, and it records a second opposite edge with a timestamp identical to the
 * first one.  This ensures that it can reliably detect absense/presence of
 * brief pulses on GPIO signals.
 */
.global edge_int
.thumb_func
edge_int:
	/* Store a few registers on stack (registers r0-r3 already stored by hardware) */
	stmdb   sp!, {r4, r5, r6, lr}
	mov 	r1, #0x40000000 /* STM32_TIM2_BASE */
load_pin_mask_location:
	/* This instruction will be replaced a runtime from load_pin_mask_table */
	mov	r2, #0x0001
load_pin_mask_location_end:
	/* Load current timestamp into r3 */
	ldr	r3, [r1, #0x24]

	ldr	r0, =0x4002f400
	ldr	r1, gpio_base
	/* Clear latched bit of both rising and falling register */
	str	r2, [r0, #0x0c] /* STM32_EXTI_RPR */
	str	r2, [r0, #0x10] /* STM32_EXTI_FPR */
	/* Read current level of the GPIO signal */
	ldr 	lr, [r1, #0x10] /* STM32_GPIO_IDR(gpio_base) */
	ands	lr, lr, r2
	/*
	 * Clear latched bit going "towards" the level just read.  This prevents
	 * recording of extraneous events, in case the level changed between
	 * the above clearing of the latched status, and reading of the level.
	 */
	ite	ne
	strne	r2, [r0, #0x0c] /* STM32_EXTI_RPR */
	streq	r2, [r0, #0x10] /* STM32_EXTI_FPR */

	ldr	r6, cyclic_buffer_header

	/* Compute diff since last timestamp (in r1), and store current timestamp */
	ldr	r0, [r6, #BUF_HEAD_TIME_OFF]
	sub	r1, r3, r0
	str	r3, [r6, #BUF_HEAD_TIME_OFF]
	/* Shift time-diff enough to be able to store signal number in low bits */
signal_bits_location:
	/* This instruction will be replaced a runtime from signal_bits_table */
	lsl	r1, r1, #3
signal_bits_location_end:
signal_no_location:
	/* This instruction will be replaced a runtime from signal_no_table */
	orr	r1, r1, #7
signal_no_location_end:

	/*
	* r1: diff
	* r2: buffer_data[0]
	* r3: buffer_data[head]
	* r4: buffer_data[tail]
	* r5: buffer_data[size]
	* r6: cyclic_buffer_header_t address
	* lr: current logic level
	*/
	add	r2, r6, #BUF_DATA_OFF
	ldr	r3, [r6, #BUF_HEAD_OFF]
	ldr	r4, [r6, #BUF_TAIL_OFF]
	ldr	r5, [r6, #BUF_SIZE_OFF]

	/*
	 * Store the value of diff (us since last event, combined with signal
	 * number in low bits) encoded 7 bits per byte, least significant bits
	 * first, with the high bit indicating whether more bytes are included.
	 *
	 * E.g. if monitoring 3 pins, (requiring two bits to store), and 64
	 * microseconds have elapsed since last event, and we now detected an
	 * edge on signal number 1 (the second out of the three being monitored),
	 * we would have diff = 64 << 2 | 1 = 0x0101.  Since that does not fit in
	 * 7 bits, the first byte of the encoded value would be the low 7 bits of
	 * diff, in a byte with the high bit set, followed by the next higher 7
	 * bits, in a byte with the high bit cleared to indicate end of encoding:
	 * 0x81, 0x02.
	 *
	 * Common case, when events are only tens of us apart, is that each
	 * event uses only a single byte.
	 */
	cmp	r1, #0x80
	bhs	encode_multiple_bytes

encode_last_byte:
	strb	r1, [r3], #1
	cmp	r3, r5
	it	eq
	moveq	r3, r2
	cmp	r3, r4
	beq	report_overrun

	ldr	r1, head_level
	cmp	r1, lr
	adr	r1, head_level
	beq	encode_extra_edge
	str	lr, [r1]
leave:
	str	r3, [r6, #BUF_HEAD_OFF]

	/* Restore registers from stack, and return from interrupt handler */
	ldmia.w sp!, {r4, r5, r6, pc}

/*
 * Uncommon case, more than 7 bits in value of "diff", see above for description
 * of encoding format.  This loop will repeatedly store lowest 7 bits, and shift
 * down the value of "diff" by 7 bits at a time, until the value fits in 7 bits.
 */
encode_multiple_bytes:
	orr	r0, r1, #0x80
	lsr	r1, r1, #7
	strb	r0, [r3], #1
	cmp	r3, r5
	it	eq
	moveq	r3, r2
	cmp	r3, r4
	beq	report_overrun
	cmp	r1, #0x80
	bhs	encode_multiple_bytes
	b	encode_last_byte

/* Uncommon case, two or more edges since last interrupt, record a second edge */
encode_extra_edge:
	ldrb	r0, signal_no
	strb	r0, [r3], #1
	cmp	r3, r5
	it	eq
	moveq	r3, r2
	cmp	r3, r4
	bne	leave

/* Uncommon case, out of buffer space */
report_overrun:
	ldmia.w sp!, {r4, r5, r6, lr}
	ldr	r1, =overrun
	adr	r0, my_slot
	bx	r1

/*
 * Store 32-bit immediate values used in above code here (used in "ldr .., =..").
 * Will be copied along with above code.
 */
.ltorg

.global edge_int_end
edge_int_end:

.data
.global load_pin_mask_replacement
load_pin_mask_replacement:
	.long 16
	.long load_pin_mask_location
	.long load_pin_mask_location_end
	.long load_pin_mask_table
	.long load_pin_mask_table_end

.text
load_pin_mask_table:
	/* One of these instructions will replace the one at load_pin_mask_location at runtime */
	mov	r2, #0x0001
	mov	r2, #0x0002
	mov	r2, #0x0004
	mov	r2, #0x0008
	mov	r2, #0x0010
	mov	r2, #0x0020
	mov	r2, #0x0040
	mov	r2, #0x0080
	mov	r2, #0x0100
	mov	r2, #0x0200
	mov	r2, #0x0400
	mov	r2, #0x0800
	mov	r2, #0x1000
	mov	r2, #0x2000
	mov	r2, #0x4000
	mov	r2, #0x8000
load_pin_mask_table_end:

.data
.global signal_no_replacement
signal_no_replacement:
	.long 16
	.long signal_no_location
	.long signal_no_location_end
	.long signal_no_table
	.long signal_no_table_end

.text
signal_no_table:
	/* One of these instructions will replace the one at signal_no_location at runtime */
	orr	r1, r1, #0
	orr	r1, r1, #1
	orr	r1, r1, #2
	orr	r1, r1, #3
	orr	r1, r1, #4
	orr	r1, r1, #5
	orr	r1, r1, #6
	orr	r1, r1, #7
	orr	r1, r1, #8
	orr	r1, r1, #9
	orr	r1, r1, #10
	orr	r1, r1, #11
	orr	r1, r1, #12
	orr	r1, r1, #13
	orr	r1, r1, #14
	orr	r1, r1, #15
signal_no_table_end:

.data
.global signal_bits_replacement
signal_bits_replacement:
	.long 4
	.long signal_bits_location
	.long signal_bits_location_end
	.long signal_bits_table
	.long signal_bits_table_end

.text
signal_bits_table:
	/* One of these instructions will replace the one at signal_bits_location at runtime */
	lsl	r1, r1, #0
	lsl	r1, r1, #1
	lsl	r1, r1, #2
	lsl	r1, r1, #3
signal_bits_table_end:


