/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SCP UART module for MT8192 specific */

#include "uart_regs.h"

/*
 * UARTN == 0, SCP UART0
 * UARTN == 1, SCP UART1
 * UARTN == 2, AP UART1
 */
#define UARTN CONFIG_UART_CONSOLE

void uart_init_pinmux(void)
{
#if UARTN == 0
	SCP_UART_CK_SEL |= UART0_CK_SEL_VAL(UART_CK_SEL_ULPOSC);
	SCP_SET_CLK_CG |= CG_UART0_MCLK | CG_UART0_BCLK | CG_UART0_RST;

	/* set AP GPIO164 and GPIO165 to alt func 3 */
	AP_GPIO_MODE20_CLR = 0x00770000;
	AP_GPIO_MODE20_SET = 0x00330000;
#elif UARTN == 1
	SCP_UART_CK_SEL |= UART1_CK_SEL_VAL(UART_CK_SEL_ULPOSC);
	SCP_SET_CLK_CG |= CG_UART1_MCLK | CG_UART1_BCLK | CG_UART1_RST;
#endif
}
