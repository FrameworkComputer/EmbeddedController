/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guybrush baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/* NPCX9 config */
#define NPCX9_PWM1_SEL    1  /* GPIO C2 is used as PWM1. */
#define NPCX_UART_MODULE2 1  /* GPIO64/65 are used as UART pins. */

#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

/* EC Defines */

/* Host communication */

/* Chipset config */

/* Common Keyboard Defines */

/* Sensors */

/* Common charger defines */

/* Common battery defines */

/* USB Type C and USB PD defines */

/* BC 1.2 */

/* I2C Bus Configuration */


#ifndef __ASSEMBLER__

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
