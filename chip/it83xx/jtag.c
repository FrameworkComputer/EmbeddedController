/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "gpio.h"
#include "jtag.h"
#include "registers.h"
#include "system.h"

void jtag_pre_init(void)
{
	/* bit4, enable debug mode through SMBus */
	IT83XX_SMB_SLVISELR &= ~(1 << 4);
}
