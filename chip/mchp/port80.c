/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Port 80 Timer Interrupt for MCHP MEC family */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "lpc.h"
#include "port80.h"
#include "registers.h"
#include "task.h"
#include "tfdp_chip.h"


/*
 * Interrupt fires when number of bytes written
 * to eSPI/LPC I/O 80h-81h exceeds Por80_0 FIFO level
 * Issues:
 * 1. eSPI will not break 16-bit I/O into two 8-bit writes
 *    as LPC does. This means Port80 hardware will capture
 *    only bits[7:0] of data.
 * 2. If Host performs write of 16-bit code as consecutive
 *    byte writes the Port80 hardware will capture both but
 *    we do not know the order it was written.
 * 3. If Host sometimes writes one byte code to I/O 80h and
 *    sometimes two byte code to I/O 80h/81h how do we determine
 *    what to do?
 *
 * An alternative is to document Host must write 16-bit codes
 * to I/O 80h and 90h.  LSB to 0x80 and MSB to 0x90.
 *
 */
void port_80_interrupt(void)
{
	int d;

	while (MCHP_P80_STS(0) & MCHP_P80_STS_NOT_EMPTY) {
		/* this masks off time stamp d = port_80_read(); */
		d = MCHP_P80_CAP(0);	/* b[7:0] = data, b[31:8] = timestamp */
		trace1(0, P80, 0, "Port80h = 0x%02x", (d & 0xff));
		port_80_write(d & 0xff);
	}

	MCHP_INT_SOURCE(MCHP_P80_GIRQ) = MCHP_P80_GIRQ_BIT(0);
}
DECLARE_IRQ(MCHP_IRQ_PORT80DBG0, port_80_interrupt, 3);


