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

#define CONFIG_I2C
#define CONFIG_I2C_DEBUG

/*
 *  nRF51 board specific configuration.
 */
#define NRF51_UART_TX_PIN 25
#define NRF51_UART_RX_PIN 29

#define NRF51_TWI_PORT 0
#define NRF51_TWI0_SCL_PIN 23
#define NRF51_TWI0_SDA_PIN 22

#define NRF51_TWI0_SCL_GPIO MCU_SCL
#define NRF51_TWI0_SDA_GPIO MCU_SDA

#define NRF51_TWI_SCL_PIN(port)   NRF51_TWI0_SCL_PIN
#define NRF51_TWI_SDA_PIN(port)   NRF51_TWI0_SDA_PIN
#define NRF51_TWI_FREQ(port)      NRF51_TWI_100KBPS
#define NRF51_TWI_PPI_CHAN(port)  0


#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */

