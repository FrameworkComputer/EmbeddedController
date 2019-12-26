/* -*- mode:c -*-
 *
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Unimplemented signals which we need to emulate for now */
UNIMPLEMENTED(ENTERING_RW)
UNIMPLEMENTED(WP_L)

/*
 * GPIOn = port * 32 + bit
 *
 * EINT ALT function:
 *   GPIO[0-3]: Alt3
 *   GPIO[4-7]: Alt6
 * UART0 ALT function:
 *   GPIO[110]: Alt3 TP_URXD1_AO
 *   GPIO[112]: Alt3 TP_UTXD1_AO
 */
ALTERNATE(PIN_MASK(0, 0x00000070), 6, MODULE_GPIO, 0) /* GPIO 5,6,7 as SCP EINT */
#if CONFIG_UART_CONSOLE == 0
#ifdef UART0_PINMUX_110_112
/* Use SCP debug UART. */
ALTERNATE(PIN_MASK(3, 0x00014000), 3, MODULE_UART, 0) /* GPIO 110,112 as UART0 */
#endif /* UART0_PINMUX_110_112 */
#ifdef UART0_PINMUX_11_12
/* Use H1(AP->H1) rework UART. */
ALTERNATE(PIN_MASK(0, 0x00001800), 1, MODULE_UART, 0) /* GPIO 11,12 as UART0 */
#endif /* UART0_PINMUX_11_12 */
#endif /* CONFIG_UART_CONSOLE == 0 */
