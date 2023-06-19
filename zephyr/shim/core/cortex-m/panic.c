/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "panic.h"

#define BASE_EXCEPTION_FRAME_SIZE_BYTES (8 * sizeof(uint32_t))
#define FPU_EXCEPTION_FRAME_SIZE_BYTES (18 * sizeof(uint32_t))

/*
 * Returns non-zero if the exception frame was created on the main stack, or
 * zero if it's on the process stack.
 *
 * See B1.5.8 "Exception return behavior" of ARM DDI 0403D for details.
 */
static int32_t is_frame_in_handler_stack(const uint32_t exc_return)
{
	return (exc_return & 0xf) == 1 || (exc_return & 0xf) == 9;
}

/*
 * Returns the size of the exception frame.
 *
 * See B1.5.7 "Stack alignment on exception entry" of ARM DDI 0403D for details.
 * In short, the exception frame size can be either 0x20, 0x24, 0x68, or 0x6c
 * depending on FPU context and padding for 8-byte alignment.
 */
static uint32_t get_exception_frame_size(const struct panic_data *pdata)
{
	uint32_t frame_size = 0;

	/* base exception frame */
	frame_size += BASE_EXCEPTION_FRAME_SIZE_BYTES;

	/* CPU uses xPSR[9] to indicate whether it padded the stack for
	 * alignment or not.
	 */
	if (pdata->cm.frame[CORTEX_PANIC_FRAME_REGISTER_PSR] & BIT(9))
		frame_size += sizeof(uint32_t);

#ifdef CONFIG_FPU
	/* CPU uses EXC_RETURN[4] to indicate whether it stored extended
	 * frame for FPU or not.
	 */
	if (!(pdata->cm.regs[CORTEX_PANIC_REGISTER_LR] & BIT(4)))
		frame_size += FPU_EXCEPTION_FRAME_SIZE_BYTES;
#endif

	return frame_size;
}

/*
 * Returns the position of the process stack before the exception frame.
 * It computes the size of the exception frame and adds it to psp.
 * If the exception happened in the exception context, it returns psp as is.
 */
uint32_t get_panic_stack_pointer(const struct panic_data *pdata)
{
	uint32_t psp = pdata->cm.regs[CORTEX_PANIC_REGISTER_PSP];

	if (!is_frame_in_handler_stack(
		    pdata->cm.regs[CORTEX_PANIC_REGISTER_LR]))
		psp += get_exception_frame_size(pdata);

	return psp;
}
