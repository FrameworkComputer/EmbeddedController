/*
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "registers.h"
#include "uart.h"

#define UART_NCO ((16 * BIT(UART_NCO_WIDTH) *				\
		   (long long)CONFIG_UART_BAUD_RATE) / PCLK_FREQ)

/* 115200N81 uart0, TX on A0, RX on A1 */
void uart_init(void)
{
	/* Pinmux init also turns on all clocks. */
	GREG32(PMU, PERICLKSET0) = 0xffffffff;
	GREG32(PMU, PERICLKSET1) = 0xffffffff;

	/*
	 * hardwire clocks to some value... just to get going
	 * Set source of trim to calibration logic during dynamic trim
	 */
	GWRITE_FIELD(XO, CLK_TIMER_TRIM_CTRL, RC_COARSE_TRIM_SRC, 0);

	/* Set initial coarse trim value (slowest) */
	GREG32(XO, CLK_TIMER_RC_COARSE_ATE_TRIM) = 100;

	/* Set initial trim stabilization period */
	GWRITE_FIELD(XO, CLK_TIMER_TRIM_CTRL, RC_INITIAL_TRIM_PERIOD, 10);

	/* enable trim */
	GWRITE_FIELD(XO, CLK_TIMER_TRIM_CTRL, RC_TRIM_EN, 1);

	/* domain crossing sync */
	GREG32(XO, CLK_TIMER_SYNC_CONTENTS) =  0x1;


	GREG32(PINMUX, DIOA0_SEL) = GC_PINMUX_UART0_TX_SEL;
	GREG32(PINMUX, UART0_RX_SEL) = GC_PINMUX_DIOA1_SEL;
	GREG32(PINMUX, DIOA1_CTL) =
		GC_PINMUX_DIOA1_CTL_DS_MASK  | GC_PINMUX_DIOA1_CTL_IE_MASK;

	GREG32(PMU, PWRDN_SCRATCH3) = 0xbeefcafe;

	GREG32(UART, FIFO) = 3;  /* clear RX,TX FIFO */

	GREG32(UART, NCO) = UART_NCO;  /* 115200N81 */

	GREG32(UART, CTRL) = 3;  /* TX,RX enable */
	uart_write_char('\n');
	uart_write_char('\r');
}

int uart_tx_ready(void)
{
	/*
	 * This makes sure that transmit FIFO is fully flashed, so that TX
	 * FIFO is not used.
	 */
	return GREAD_FIELD(UART, STATE, TXIDLE);
}

void uart_tx_flush(void)
{
}

int uart_init_done(void)
{
	return 1;
}
void uart_tx_start(void)
{
}
void uart_tx_stop(void)
{
}

void uart_write_char(char c)
{
	while (!uart_tx_ready())
		;
	GREG32(UART, WDATA) = c;
}
