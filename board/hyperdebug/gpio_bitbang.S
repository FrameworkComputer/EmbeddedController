/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Custom GPIO bitbang interrupt handling logic.
 */

#include "config.h"

/*
 * Control whether to pulse pin PG6 from timer interrupt, useful for
 * measuring jitter.
 */
#undef DEBUG_BITBANG_INTERRUPT

.text

.syntax unified
.code 16

#define BITBANG_BUFFER_SIZE 16384

/*
 * Offsets of fields in struct bitbang_state_t, measured from the END
 * of the waveform data buffer.
 */
#define BITBANG_TAIL_OFF 0
#define BITBANG_IRQ_TAIL_OFF 4
#define BITBANG_IRQ_OFF 8
#define BITBANG_HEAD_OFF 12
#define BITBANG_COUNTDOWN_OFF 16
#define BITBANG_MASK_OFF 20
#define BITBANG_PATTERN_OFF 21
#define BITBANG_DATA_OFF 24

/*
 * First part of bitbang timer interrupt handler.
 *
 * The actual handler is copied into SRAM at runtime, and composed by
 * concatenating various "snippets" of machine code, creating a handler that
 * hard-codes the GPIO registers to inspect and manipulate.
*/

	.align	4
.global bitbang_int_begin
bitbang_int_begin:

/* Constant pool */
stm32_gpioa_base:
	.long	0x42020000
stm32_timer6_base:
	.long	0x40001000
bitbang_addr:
	.long	bitbang + BITBANG_BUFFER_SIZE

done:
	mov	r1, #0
	str	r1, [r0, #0x00] /* STM32_TIM_CR1 */

#ifdef DEBUG_BITBANG_INTERRUPT
	add	r0, lr, #0x1800 /* STM32_GPIOG_BASE */
	mov	r1, #0x00000040
	str	r1, [r0, #0x18] /* STM32_GPIO_BSRR */
#endif
	/* Restore registers from stack, and return from interrupt handler */
	ldmia.w	sp!, {r7, pc}

countdown:
	add	r1, r1, #-1
	str	r1, [r3, #BITBANG_COUNTDOWN_OFF]

#ifdef DEBUG_BITBANG_INTERRUPT
	add	r0, lr, #0x1800 /* STM32_GPIOG_BASE */
	mov	r1, #0x00000040
	str	r1, [r0, #0x18] /* STM32_GPIO_BSRR */
#endif
	/* Restore registers from stack, and return from interrupt handler */
	ldmia.w	sp!, {r7, pc}

/* Entry point for bitbang interrupt handler. */
.global bitbang_int
.thumb_func
bitbang_int:
	/*
	 * Store a few registers on stack (registers r0-r3 already stored by
	 * hardware)
	 */
	stmdb	sp!, {r7, lr}
	ldr	lr, stm32_gpioa_base

#ifdef DEBUG_BITBANG_INTERRUPT
	add	r0, lr, #0x1800 /* STM32_GPIOG_BASE */
	mov	r1, #0x00400000
	str	r1, [r0, #0x18] /* STM32_GPIO_BSRR */
#endif

	/* r3: address of bitbang struct */
	ldr	r3, bitbang_addr

	/* Acknowledge timer interrupt */
	mov	r1, #0
	ldr	r0, stm32_timer6_base
	str	r1, [r0, #0x10] /* STM32_TIM_SR */

	/* Check if waveform data is exhausted */
	ldr	r2, [r3, #BITBANG_IRQ_OFF]
	ldr	r1, [r3, #BITBANG_IRQ_TAIL_OFF]
	cmp	r1, r2
	beq	done

	/* Check if we are pausing for a number of ticks */
	ldr	r1, [r3, #BITBANG_COUNTDOWN_OFF]
	ands	r1, r1, r1
	bne	countdown

	mov	r7, #0

	/* lr: base address of GPIOA peripheral */
	/* r2: bitbang_irq_off */
	/* r3: address of bitbang struct */
	/* r7: input_data (sampled from bit-banging pins) */

.global bitbang_int_end
bitbang_int_end:

/*
 * Load the current value of the 16 pins of a particular GPIO port into r0.
 */
	.align	4
.global read_gpio_snippet
read_gpio_snippet:
	.long	8
	.long	read_gpio_table
	.long	read_gpio_table_end

read_gpio_table:
	add	r1, lr, #0x0000 /* STM32_GPIOA_BASE */
	ldr	r0, [r1, #0x10] /* STM32_GPIO_IDR */

	add	r1, lr, #0x0400 /* STM32_GPIOB_BASE */
	ldr	r0, [r1, #0x10] /* STM32_GPIO_IDR */

	add	r1, lr, #0x0800 /* STM32_GPIOC_BASE */
	ldr	r0, [r1, #0x10] /* STM32_GPIO_IDR */

	add	r1, lr, #0x0C00 /* STM32_GPIOD_BASE */
	ldr	r0, [r1, #0x10] /* STM32_GPIO_IDR */

	add	r1, lr, #0x1000 /* STM32_GPIOE_BASE */
	ldr	r0, [r1, #0x10] /* STM32_GPIO_IDR */

	add	r1, lr, #0x1400 /* STM32_GPIOF_BASE */
	ldr	r0, [r1, #0x10] /* STM32_GPIO_IDR */

	add	r1, lr, #0x1800 /* STM32_GPIOG_BASE */
	ldr	r0, [r1, #0x10] /* STM32_GPIO_IDR */

	add	r1, lr, #0x1C00 /* STM32_GPIOH_BASE */
	ldr	r0, [r1, #0x10] /* STM32_GPIO_IDR */
read_gpio_table_end:

/*
 * Pick a particular bit out of r0, then "rotate" that bit value
 * into the highest bit of r7, shifting any bits already in r7
 * towards the low end.
 */
	.align	4
.global get_bit_snippet
get_bit_snippet:
	.long	16
	.long	get_bit_table
	.long	get_bit_table_end

get_bit_table:
	lsrs	r1, r0, #1
	rrx	r7, r7

	lsrs	r1, r0, #2
	rrx	r7, r7

	lsrs	r1, r0, #3
	rrx	r7, r7

	lsrs	r1, r0, #4
	rrx	r7, r7

	lsrs	r1, r0, #5
	rrx	r7, r7

	lsrs	r1, r0, #6
	rrx	r7, r7

	lsrs	r1, r0, #7
	rrx	r7, r7

	lsrs	r1, r0, #8
	rrx	r7, r7

	lsrs	r1, r0, #9
	rrx	r7, r7

	lsrs	r1, r0, #10
	rrx	r7, r7

	lsrs	r1, r0, #11
	rrx	r7, r7

	lsrs	r1, r0, #12
	rrx	r7, r7

	lsrs	r1, r0, #13
	rrx	r7, r7

	lsrs	r1, r0, #14
	rrx	r7, r7

	lsrs	r1, r0, #15
	rrx	r7, r7

	lsrs	r1, r0, #16
	rrx	r7, r7
get_bit_table_end:

/*
 * After one or more bits have been shifted into r7 from the "high end"
 * one of the instructions from this snipped is used to move the sequence
 * of bits to the right, so that they end in the "low end".
 */
	.align	4
.global align_bits_snippet
align_bits_snippet:
	.long	7
	.long	align_bits_table
	.long	align_bits_table_end

align_bits_table:
	lsr	r7, r7, #31
	lsr	r7, r7, #30
	lsr	r7, r7, #29
	lsr	r7, r7, #28
	lsr	r7, r7, #27
	lsr	r7, r7, #26
	lsr	r7, r7, #25
align_bits_table_end:

/*
 * The main part of the interrupt handler logic.
 *
 * Will consume one byte from the waveform data, and depending on its high bit
 * either proceed with ordinary output of a sample, or populate countdown or
 * mask/pattern with instructions to wait for a number of cycles or until
 * certain input is seen.
 */
	.align	4
.global midway_snippet
midway_snippet:
	.long	1
	.long	midway_table
	.long	midway_table_end

midway_table:
	/* lr: base address of GPIOA peripheral */
	/* r2: bitbang_irq_off */
	/* r3: address of bitbang struct */
	/* r7: input data sampled from bit-banging pins (in up to 7 low bits) */

	/*
	 * If bitbang.mask is nonzero, we must wait until seeing a certain
	 * pattern, before resuming waveform output.
	 */
	ldrb	r1, [r3, #BITBANG_MASK_OFF]
	cmp	r1, #0
	beq	inspect_byte

	ldrb	r0, [r3, #BITBANG_PATTERN_OFF]
	eor	r0, r0, r7
	tst	r0, r1
	beq	done_waiting

#ifdef DEBUG_BITBANG_INTERRUPT
	add	r0, lr, #0x1800 /* STM32_GPIOG_BASE */
	mov	r1, #0x00000040
	str	r1, [r0, #0x18] /* STM32_GPIO_BSRR */
#endif
	/* Restore registers from stack, and return from interrupt handler */
	ldmia.w	sp!, {r7, pc}

done_waiting:
	mov	r0, #0
	strb	r0, [r3, #BITBANG_MASK_OFF]

inspect_byte:
	/*
	 * When control reaches this point, it means that we should continue
	 * by interpreting the next entry of the the waveform.
	 */
	mov	r1, #BITBANG_BUFFER_SIZE - 1
	orn	r0, r2, r1 /* r0 = r2 % BITBANG_BUFFER_SIZE - BITBANG_BUFFER_SIZE */
	ldrb	r1, [r3, r0]
	tst	r1, #0x80
	beq	continue

	/* Highest bit set means special delay */

	/* lr: base address of GPIOA peripheral */
	/* r1: first byte of special sequence */
	/* r2: bitbang_irq_off */
	/* r3: address of bitbang struct */
	/* r7: input_data (sampled from bit-banging pins) */

	stmdb	sp!, {r5, r6}

	mov	r6, #0
	mov	r5, #0

	/*
	 * Decode for as long as bytes continue having their high bit set.
	 */
decode_loop:
	add	r2, r2, #1
	and	r1, r1, #0x7F
	lsl	r1, r1, r5
	add	r6, r6, r1
	add	r5, r5, #7

	/* Load next data byte */
	mov	r1, #BITBANG_BUFFER_SIZE - 1
	orn	r0, r2, r1 /* r0 = r2 % BITBANG_BUFFER_SIZE - BITBANG_BUFFER_SIZE */
	ldrb	r1, [r3, r0]
	tst	r1, #0x80
	bne	decode_loop

	/* Decoded to end of special sequence */
	cmp	r6, #0
	beq	zero_delay

	/* Delay of some number of ticks requested */
	add	r6, r6, #-1
	str	r2, [r3, #BITBANG_IRQ_OFF]
	str	r6, [r3, #BITBANG_COUNTDOWN_OFF]

#ifdef DEBUG_BITBANG_INTERRUPT
	add	r0, lr, #0x1800 /* STM32_GPIOG_BASE */
	mov	r1, #0x00000040
	str	r1, [r0, #0x18] /* STM32_GPIO_BSRR */
#endif
	/* Restore registers from stack, and return from interrupt handler */
	ldmia.w	sp!, {r5, r6, r7, pc}

zero_delay:
	/* Zero-delay is used to encode "await()" */
	mov	r5, #BITBANG_BUFFER_SIZE - 1
	orn	r1, r2, r5 /* r1 = r2 % BITBANG_BUFFER_SIZE - BITBANG_BUFFER_SIZE */
	add	r2, r2, #1
	ldrb	r0, [r3, r1]

	orn	r1, r2, r5 /* r1 = r2 % BITBANG_BUFFER_SIZE - BITBANG_BUFFER_SIZE */
	add	r2, r2, #1
	ldrb	r5, [r3, r1]

	eor	r1, r5, r7
	tst	r1, r0
	bne	pattern_not_immediately_satisfied

	/* Requested pattern is immediately satisfied, proceed to output next byte */
	ldmia.w	sp!, {r5, r6}
	b	inspect_byte

pattern_not_immediately_satisfied:
	strb	r0, [r3, #BITBANG_MASK_OFF]
	strb	r5, [r3, #BITBANG_PATTERN_OFF]
	str	r2, [r3, #BITBANG_IRQ_OFF]

#ifdef DEBUG_BITBANG_INTERRUPT
	add	r0, lr, #0x1800 /* STM32_GPIOG_BASE */
	mov	r1, #0x00000040
	str	r1, [r0, #0x18] /* STM32_GPIO_BSRR */
#endif
	/* Restore registers from stack, and return from interrupt handler */
	ldmia.w	sp!, {r5, r6, r7, pc}

continue:
	add	r2, r2, #1
	strb	r7, [r3, r0]

	/*
	 * The following register assignments apply through the remaning snippets
	 */

	/* lr: base address of GPIOA peripheral */
	/* r1: data_byte */
	/* r2: bitbang_irq_off (will be eventually written back to memory) */
	/* r3: address of bitbang struct */
midway_table_end:

/*
 * Consume lowest bit of r1 (shifting the remaining ones to the right),
 * Load a value into r7 with a single bit set among the lower 16 bits
 * or higher sixteen bits, depending on value of the bit of r1.
 */
	.align	4
.global set_bit_snippet
set_bit_snippet:
	.long	16
	.long	set_bit_table
	.long	set_bit_table_end

set_bit_table:
	lsrs	r1, r1, #1
	ite	cs
	movcs.w	r7, #0x00000001
	movcc.w	r7, #0x00010000

	lsrs	r1, r1, #1
	ite	cs
	movcs.w	r7, #0x00000002
	movcc.w	r7, #0x00020000

	lsrs	r1, r1, #1
	ite	cs
	movcs.w	r7, #0x00000004
	movcc.w	r7, #0x00040000

	lsrs	r1, r1, #1
	ite	cs
	movcs.w	r7, #0x00000008
	movcc.w	r7, #0x00080000

	lsrs	r1, r1, #1
	ite	cs
	movcs.w	r7, #0x00000010
	movcc.w	r7, #0x00100000

	lsrs	r1, r1, #1
	ite	cs
	movcs.w	r7, #0x00000020
	movcc.w	r7, #0x00200000

	lsrs	r1, r1, #1
	ite	cs
	movcs.w	r7, #0x00000040
	movcc.w	r7, #0x00400000

	lsrs	r1, r1, #1
	ite	cs
	movcs.w	r7, #0x00000080
	movcc.w	r7, #0x00800000

	lsrs	r1, r1, #1
	ite	cs
	movcs.w	r7, #0x00000100
	movcc.w	r7, #0x01000000

	lsrs	r1, r1, #1
	ite	cs
	movcs.w	r7, #0x00000200
	movcc.w	r7, #0x02000000

	lsrs	r1, r1, #1
	ite	cs
	movcs.w	r7, #0x00000400
	movcc.w	r7, #0x04000000

	lsrs	r1, r1, #1
	ite	cs
	movcs.w	r7, #0x00000800
	movcc.w	r7, #0x08000000

	lsrs	r1, r1, #1
	ite	cs
	movcs.w	r7, #0x00001000
	movcc.w	r7, #0x10000000

	lsrs	r1, r1, #1
	ite	cs
	movcs.w	r7, #0x00002000
	movcc.w	r7, #0x20000000

	lsrs	r1, r1, #1
	ite	cs
	movcs.w	r7, #0x00004000
	movcc.w	r7, #0x40000000

	lsrs	r1, r1, #1
	ite	cs
	movcs.w	r7, #0x00008000
	movcc.w	r7, #0x80000000
set_bit_table_end:

/*
 * Consume lowest bit of r1 (shifting the remaining ones to the right),
 * Set one additional bit of r7 among the lower 16 bits
 * or higher sixteen bits, depending on value of the bit of r1.
 */
	.align	4
.global set_additional_bit_snippet
set_additional_bit_snippet:
	.long	16
	.long	set_additional_bit_table
	.long	set_additional_bit_table_end

set_additional_bit_table:
	lsrs	r1, r1, #1
	ite	cs
	orrcs.w	r7, r7, #0x00000001
	orrcc.w	r7, r7, #0x00010000

	lsrs	r1, r1, #1
	ite	cs
	orrcs.w	r7, r7, #0x00000002
	orrcc.w	r7, r7, #0x00020000

	lsrs	r1, r1, #1
	ite	cs
	orrcs.w	r7, r7, #0x00000004
	orrcc.w	r7, r7, #0x00040000

	lsrs	r1, r1, #1
	ite	cs
	orrcs.w	r7, r7, #0x00000008
	orrcc.w	r7, r7, #0x00080000

	lsrs	r1, r1, #1
	ite	cs
	orrcs.w	r7, r7, #0x00000010
	orrcc.w	r7, r7, #0x00100000

	lsrs	r1, r1, #1
	ite	cs
	orrcs.w	r7, r7, #0x00000020
	orrcc.w	r7, r7, #0x00200000

	lsrs	r1, r1, #1
	ite	cs
	orrcs.w	r7, r7, #0x00000040
	orrcc.w	r7, r7, #0x00400000

	lsrs	r1, r1, #1
	ite	cs
	orrcs.w	r7, r7, #0x00000080
	orrcc.w	r7, r7, #0x00800000

	lsrs	r1, r1, #1
	ite	cs
	orrcs.w	r7, r7, #0x00000100
	orrcc.w	r7, r7, #0x01000000

	lsrs	r1, r1, #1
	ite	cs
	orrcs.w	r7, r7, #0x00000200
	orrcc.w	r7, r7, #0x02000000

	lsrs	r1, r1, #1
	ite	cs
	orrcs.w	r7, r7, #0x00000400
	orrcc.w	r7, r7, #0x04000000

	lsrs	r1, r1, #1
	ite	cs
	orrcs.w	r7, r7, #0x00000800
	orrcc.w	r7, r7, #0x08000000

	lsrs	r1, r1, #1
	ite	cs
	orrcs.w	r7, r7, #0x00001000
	orrcc.w	r7, r7, #0x10000000

	lsrs	r1, r1, #1
	ite	cs
	orrcs.w	r7, r7, #0x00002000
	orrcc.w	r7, r7, #0x20000000

	lsrs	r1, r1, #1
	ite	cs
	orrcs.w	r7, r7, #0x00004000
	orrcc.w	r7, r7, #0x40000000

	lsrs	r1, r1, #1
	ite	cs
	orrcs.w	r7, r7, #0x00008000
	orrcc.w	r7, r7, #0x80000000
set_additional_bit_table_end:

/*
 * Apply value of r7 to the "bit set/reset register" of a particular GPIO bank.
 */
	.align	4
.global apply_gpio_snippet
apply_gpio_snippet:
	.long	8
	.long	apply_gpio_table
	.long	apply_gpio_table_end

apply_gpio_table:
	add	r0, lr, #0x0000 /* STM32_GPIOA_BASE */
	str	r7, [r0, #0x18] /* STM32_GPIO_BSRR */

	add	r0, lr, #0x0400 /* STM32_GPIOB_BASE */
	str	r7, [r0, #0x18] /* STM32_GPIO_BSRR */

	add	r0, lr, #0x0800 /* STM32_GPIOC_BASE */
	str	r7, [r0, #0x18] /* STM32_GPIO_BSRR */

	add	r0, lr, #0x0C00 /* STM32_GPIOD_BASE */
	str	r7, [r0, #0x18] /* STM32_GPIO_BSRR */

	add	r0, lr, #0x1000 /* STM32_GPIOE_BASE */
	str	r7, [r0, #0x18] /* STM32_GPIO_BSRR */

	add	r0, lr, #0x1400 /* STM32_GPIOF_BASE */
	str	r7, [r0, #0x18] /* STM32_GPIO_BSRR */

	add	r0, lr, #0x1800 /* STM32_GPIOG_BASE */
	str	r7, [r0, #0x18] /* STM32_GPIO_BSRR */

	add	r0, lr, #0x1C00 /* STM32_GPIOH_BASE */
	str	r7, [r0, #0x18] /* STM32_GPIO_BSRR */
apply_gpio_table_end:

/*
 * Retrieve 12-bit value by comsuming 1 bytes of waveform data, appending
 * 4 bits from the value in r1 (the register holding 7 bits used for GPIO
 * bitbanging.)
 */
	.align	4
.global fetch_dac_value_snippet
fetch_dac_value_snippet:
	.long	1
	.long	fetch_dac_value_table
	.long	fetch_dac_value_table_end

fetch_dac_value_table:
	/* r1: high 4 bits of DAC value (in low 4 bits) */
	/* r2: bitbang_irq_off (will be eventually written back to memory) */
	/* r3: address of bitbang struct */

	/* Load one more waveform byte, incrementing index */
	mov	r0, #BITBANG_BUFFER_SIZE - 1
	orn	r7, r2, r0 /* r7 = r2 % BITBANG_BUFFER_SIZE - BITBANG_BUFFER_SIZE */
	add	r2, r2, #1
	ldrb	r0, [r3, r7]

	/* Concatenate 4 bits from r0 with the loaded byte */
	and	r1, r1, #0x0F
	add	r0, r0, r1, lsl #8
fetch_dac_value_table_end:

/*
 * Retrieve 12-bit value by consuming 2 bytes of waveform data.
 */
	.align	4
.global fetch_dac_value2_snippet
fetch_dac_value2_snippet:
	.long	1
	.long	fetch_dac_value2_table
	.long	fetch_dac_value2_table_end

fetch_dac_value2_table:
	/* r2: bitbang_irq_off (will be eventually written back to memory) */
	/* r3: address of bitbang struct */

	/* Load two more waveform bytes, incrementing index */
	mov	r0, #BITBANG_BUFFER_SIZE - 1
	orn	r7, r2, r0 /* r7 = r2 % BITBANG_BUFFER_SIZE - BITBANG_BUFFER_SIZE */
	add	r2, r2, #1
	ldrb	r1, [r3, r7]

	orn	r7, r2, r0 /* r7 = r2 % BITBANG_BUFFER_SIZE - BITBANG_BUFFER_SIZE */
	add	r2, r2, #1
	ldrb	r0, [r3, r7]

	/* Concatenate 4 bits from the first with the second byte */
	and	r1, r1, #0x0F
	add	r0, r0, r1, lsl #8
fetch_dac_value2_table_end:

/*
 * Apply value in lower 12 bits of r0 to one of the DAC channels.
 */
	.align	4
.global apply_dac_snippet
apply_dac_snippet:
	.long	2
	.long	apply_dac_table
	.long	apply_dac_table_end

apply_dac_table:
	mov	r1, #0x40000000
	add	r1, r1, #0x7400 /* STM32_DAC_BASE */
	str	r0, [r1, #0x08] /* STM32_DAC_DHR12R1 */

	mov	r1, #0x40000000
	add	r1, r1, #0x7400 /* STM32_DAC_BASE */
	str	r0, [r1, #0x14] /* STM32_DAC_DHR12R2 */
apply_dac_table_end:

/*
 * Write current index into bitbang.irq_off, then return from interrupt.
 */
	.align	4
.global finish_snippet
finish_snippet:
	.long	1
	.long	finish_table
	.long	finish_table_end

finish_table:
	str	r2, [r3, #BITBANG_IRQ_OFF]

#ifdef DEBUG_BITBANG_INTERRUPT
	add	r0, lr, #0x1800 /* STM32_GPIOG_BASE */
	mov	r1, #0x00000040
	str	r1, [r0, #0x18] /* STM32_GPIO_BSRR */
#endif
	/* Restore registers from stack, and return from interrupt handler */
	ldmia.w	sp!, {r7, pc}

finish_table_end:
