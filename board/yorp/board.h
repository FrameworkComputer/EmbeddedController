/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Yorp board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands while in dev. */

/* NPCX7 config */
#define NPCX_UART_MODULE2 1  /* GPIO64/65 are used as UART pins. */
#define NPCX_TACH_SEL2    0  /* [0:GPIO40/73, 1:GPIO93/A6] as TACH */
#define NPCX7_PWM1_SEL    0  /* GPIO C2 is not used as PWM1. */

/* Internal SPI flash on NPCX7 */
/* Flash is 1MB but reserve half for future use. */
#define CONFIG_FLASH_SIZE (512 * 1024)

#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q128 /* Internal SPI flash type. */

/* SoC / PCH */
/* GEMINILAKE reuses apollo lake power seq */
#define CONFIG_CHIPSET_APOLLOLAKE
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_ESPI
/* TODO(b/74123961): Enable Virtual Wires after bringup */
#define CONFIG_LPC
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_EXTPOWER_GPIO
/* TODO(b/73811887), increase CONFIG_EXTPOWER_DEBOUNCE_MS from 30 to 1000? */


#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* TODO(b/73811887): Fill out correctly */
enum adc_channel {
	ADC_CH_COUNT
};

enum power_signal {
#ifdef CONFIG_POWER_S0IX
	X86_SLP_S0_N,		/* PCH  -> SLP_S0_L */
#endif
	X86_SLP_S3_N,		/* PCH  -> SLP_S3_L */
	X86_SLP_S4_N,		/* PCH  -> SLP_S4_L */
	X86_SUSPWRDNACK,	/* PCH  -> SUSPWRDNACK */

	X86_ALL_SYS_PG,		/* PMIC -> PMIC_EC_PWROK_OD */
	X86_RSMRST_N,		/* PMIC -> PMIC_EC_RSMRST_ODL */
	X86_PGOOD_PP3300,	/* PMIC -> PP3300_PG_OD */
	X86_PGOOD_PP5000,	/* PMIC -> PP5000_PG_OD */

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
