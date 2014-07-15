/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hadoken board configuration */

#ifndef __BOARD_H
#define __BOARD_H

#ifndef __ASSEMBLER__

#undef CONFIG_FLASH /* TODO: implement me */
#undef CONFIG_FMAP /* TODO: implement me */
#undef CONFIG_WATCHDOG
#undef CONFIG_LID_SWITCH


/*
 *  nRF51 board specific configuration.
 */
#define NRF51_UART_TX_PIN 25
#define NRF51_UART_RX_PIN 29

#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */

