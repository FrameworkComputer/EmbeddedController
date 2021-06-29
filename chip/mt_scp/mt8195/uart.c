/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* SCP UART module for MT8195 specific */

#include "uart_regs.h"

/*
 * UARTN == 0, SCP UART0
 * UARTN == 1, SCP UART1
 * UARTN == 2, AP UART1
 */
#define UARTN CONFIG_UART_CONSOLE

void uart_init_pinmux(void)
{
}
