/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* DragonEgg board configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/* I2C Bus Configuration */
#define I2C_PORT_BATTERY	IT83XX_I2C_CH_F	/* Shared bus */
#define I2C_PORT_CHARGER	IT83XX_I2C_CH_F	/* Shared bus */
#define I2C_PORT_SENSOR		IT83XX_I2C_CH_B
#define I2C_PORT_USBC0		IT83XX_I2C_CH_E
#define I2C_PORT_USBC1C2		IT83XX_I2C_CH_C
#define I2C_PORT_EEPROM		IT83XX_I2C_CH_A
#define I2C_ADDR_EEPROM		0xA0

#endif /* __CROS_EC_BASEBOARD_H */
