/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Settings to enable JTAG debugging */

#include "jtag.h"
#include "registers.h"

int jtag_pre_init(void)
{
	/* stop TIM2, TIM3 and watchdogs when the JTAG stops the CPU */
	STM32_DBGMCU_CR |= 0x00001b00;

	return EC_SUCCESS;
}
