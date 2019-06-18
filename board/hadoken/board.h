/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Hadoken board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#ifndef __ASSEMBLER__

#undef CONFIG_FLASH /* TODO: implement me */
#undef CONFIG_FLASH_PHYSICAL /* TODO: implement me */
#undef CONFIG_FMAP /* TODO: implement me */
#undef CONFIG_WATCHDOG
#undef CONFIG_LID_SWITCH

/*
 *  nRF51 board specific configuration.
 */
#define NRF51_UART_TX_PIN 24
#define NRF51_UART_RX_PIN 28

#define BATTERY_VOLTAGE_MAX         4425 /* mV */
#define BATTERY_VOLTAGE_NORMAL      3800 /* mV */
#define BATTERY_VOLTAGE_MIN         3000 /* mV */

#define CONFIG_BLUETOOTH_LE
#define CONFIG_BLUETOOTH_LE_STACK
#define CONFIG_BLUETOOTH_LE_RADIO_TEST
#define CONFIG_BLUETOOTH_LL_DEBUG
#define CONFIG_BLUETOOTH_HCI_DEBUG

#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */

