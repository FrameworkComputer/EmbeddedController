/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel TGL-U-RVP-ITE board-specific configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* USB MUX */
#define CONFIG_USB_MUX_VIRTUAL

#define CONFIG_USBC_VCONN

/* FAN configs */
#define CONFIG_FANS 1
#define BOARD_FAN_MIN_RPM 3000
#define BOARD_FAN_MAX_RPM 10000

/* Temperature sensor */
#define CONFIG_TEMP_SENSOR

#include "baseboard.h"

#define CONFIG_CHIPSET_TIGERLAKE
#define GPIO_PG_EC_RSMRST_ODL	GPIO_RSMRST_L_PGOOD
#define GPIO_PCH_DSW_PWROK	GPIO_EC_PCH_DSW_PWROK

/* Charger */
#define CONFIG_CHARGER_ISL9241

/* DC Jack charge ports */
#undef  CONFIG_DEDICATED_CHARGE_PORT_COUNT
#define CONFIG_DEDICATED_CHARGE_PORT_COUNT 1

/* USB ports */
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define DEDICATED_CHARGE_PORT 2

/* USB-C port's USB2 & USB3 port numbers */
#ifdef BOARD_TGLRVPU_ITE
	#define TYPE_C_PORT_0_USB2_NUM	6
	#define TYPE_C_PORT_1_USB2_NUM	7

	#define TYPE_C_PORT_0_USB3_NUM	3
	#define TYPE_C_PORT_1_USB3_NUM	4
#else /* BOARD_TGLRVPY_ITE */
	#define TYPE_C_PORT_0_USB2_NUM	6
	#define TYPE_C_PORT_1_USB2_NUM	5

	#define TYPE_C_PORT_0_USB3_NUM	3
	#define TYPE_C_PORT_1_USB3_NUM	2
#endif /* BOARD_TGLRVPU_ITE */


/* Config BB retimer */
#define CONFIG_USB_PD_RETIMER_INTEL_BB

/* Thermal configs */

/* I2C ports */
#define CONFIG_IT83XX_SMCLK2_ON_GPC7

#define I2C_PORT_CHARGER	IT83XX_I2C_CH_B
#define I2C_PORT_BATTERY	IT83XX_I2C_CH_B
#define I2C_PORT_PCA9555_BOARD_ID_GPIO	IT83XX_I2C_CH_B
#define I2C_PORT_PORT80		IT83XX_I2C_CH_B
#define I2C_PORT0_BB_RETIMER	IT83XX_I2C_CH_E
#define I2C_PORT1_BB_RETIMER	IT83XX_I2C_CH_E

#define I2C_ADDR_PCA9555_BOARD_ID_GPIO	0x22
#define PORT80_I2C_ADDR			MAX695X_I2C_ADDR1_FLAGS
#ifdef BOARD_TGLRVPU_ITE
	#define I2C_PORT0_BB_RETIMER_ADDR	0x42
	#define I2C_PORT1_BB_RETIMER_ADDR	0x43

	/* BB retimer nvm is shared between port 0 & 1 */
	#define BB_RETIMER_SHARED_NVM 1
#else /* BOARD_TGLRVPY_ITE */
	#define I2C_PORT0_BB_RETIMER_ADDR	0x42
	#define I2C_PORT1_BB_RETIMER_ADDR	0x41

	/* BB retimers have respective nvm for port 0 & 1 */
	#define BB_RETIMER_SHARED_NVM 0
#endif /* BOARD_TGLRVPU_ITE */
#define USB_PORT0_BB_RETIMER_SHARED_NVM	BB_RETIMER_SHARED_NVM
#define USB_PORT1_BB_RETIMER_SHARED_NVM	BB_RETIMER_SHARED_NVM

/* Enabling SOP* communication */
#define CONFIG_USB_PD_DECODE_SOP

#ifndef __ASSEMBLER__

enum tglrvp_charge_ports {
	TYPE_C_PORT_0,
	TYPE_C_PORT_1,
};

enum tglrvp_i2c_channel {
	I2C_CHAN_FLASH,
	I2C_CHAN_BATT_CHG,
	I2C_CHAN_RETIMER,
	I2C_CHAN_COUNT,
};

/* Define max power */
#define PD_MAX_POWER_MW        60000

int board_get_version(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
