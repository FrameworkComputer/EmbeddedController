/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* DragonEgg board configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

#define CONFIG_CHIPSET_ICELAKE
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
/* TODO(b/111155507): Don't enable SOiX for now */
/* #define CONFIG_POWER_S0IX */
/* #define CONFIG_POWER_TRACK_HOST_SLEEP_STATE */

/* I2C Bus Configuration */
#define I2C_PORT_BATTERY	IT83XX_I2C_CH_F	/* Shared bus */
#define I2C_PORT_CHARGER	IT83XX_I2C_CH_F	/* Shared bus */
#define I2C_PORT_SENSOR		IT83XX_I2C_CH_B
#define I2C_PORT_USBC0		IT83XX_I2C_CH_E
#define I2C_PORT_USBC1C2		IT83XX_I2C_CH_C
#define I2C_PORT_EEPROM		IT83XX_I2C_CH_A
#define I2C_ADDR_EEPROM		0xA0

#ifndef __ASSEMBLER__

enum power_signal {
	X86_SLP_S0_DEASSERTED,
	X86_SLP_S3_DEASSERTED,
	X86_SLP_S4_DEASSERTED,
	X86_SLP_SUS_DEASSERTED,
	X86_RSMRST_L_PGOOD,
	X86_DSW_DPWROK,
	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};
#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
