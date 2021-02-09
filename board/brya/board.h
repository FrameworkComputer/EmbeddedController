/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Brya board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Baseboard features */
#include "baseboard.h"

/*
 * Disable features enabled by default.
 */
#undef CONFIG_ADC
#undef CONFIG_HIBERNATE
#undef CONFIG_SPI_FLASH
#undef CONFIG_SWITCH

/* USB Type C and USB PD defines */
#define CONFIG_IO_EXPANDER_PORT_COUNT		2

#define GPIO_AC_PRESENT			GPIO_ACOK_EC_OD
#define GPIO_CPU_PROCHOT		GPIO_EC_PROCHOT_ODL
#define GPIO_ENTERING_RW		GPIO_EC_ENTERING_RW
#define GPIO_LID_OPEN			GPIO_LID_OPEN_OD
#define GPIO_PCH_RSMRST_L		GPIO_EC_PCH_RSMRST_L
#define GPIO_PCH_SLP_S0_L		GPIO_SYS_SLP_S0IX_L
#define GPIO_PCH_SLP_S3_L		GPIO_SLP_S3_L
/*
 * GPIO_EC_PCH_INT_ODL is used for MKBP events as well as a PCH wakeup
 * signal.
 */
#define GPIO_PCH_WAKE_L			GPIO_EC_PCH_INT_ODL
#define GPIO_PG_EC_ALL_SYS_PWRGD	GPIO_SEQ_EC_ALL_SYS_PG
#define GPIO_PG_EC_DSW_PWROK		GPIO_SEQ_EC_DSW_PWROK
#define GPIO_PG_EC_RSMRST_ODL		GPIO_SEQ_EC_RSMRST_ODL
#define GPIO_RSMRST_L_PGOOD		GPIO_SEQ_EC_RSMRST_ODL
#define GPIO_SYS_RESET_L		GPIO_SYS_RST_ODL
#define GPIO_WP_L			GPIO_EC_WP_ODL

#define CONFIG_FANS			FAN_CH_COUNT

/* System has back-lit keyboard */
#define CONFIG_PWM_KBLIGHT

/* I2C Bus Configuration */

#define I2C_PORT_SENSOR		NPCX_I2C_PORT0_0

#define I2C_PORT_TCPC0_2	NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C0_TCPC	NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C1_TCPC	NPCX_I2C_PORT4_1
#define I2C_PORT_USB_C2_TCPC	NPCX_I2C_PORT1_0	/* dual TCPC with C0 */

#define I2C_PORT_USB_C0_PPC	NPCX_I2C_PORT2_0
#define I2C_PORT_USB_C1_PPC	NPCX_I2C_PORT6_0
#define I2C_PORT_USB_C2_PPC	NPCX_I2C_PORT2_0

#define I2C_PORT_USB_C0_BC12	NPCX_I2C_PORT2_0
#define I2C_PORT_USB_C1_BC12	NPCX_I2C_PORT6_0
#define I2C_PORT_USB_C2_BC12	NPCX_I2C_PORT2_0

#define I2C_PORT_USB_C0_MUX	NPCX_I2C_PORT3_0
#define I2C_PORT_USB_C1_MUX	NPCX_I2C_PORT6_0
#define I2C_PORT_USB_C2_MUX	NPCX_I2C_PORT3_0

#define I2C_PORT_BATTERY	NPCX_I2C_PORT5_0
#define I2C_PORT_CHARGER	NPCX_I2C_PORT7_0
#define I2C_PORT_EEPROM		NPCX_I2C_PORT7_0

#ifndef __ASSEMBLER__

#include "gpio_signal.h"	/* needed by registers.h */
#include "registers.h"

enum ioex_port {
	IOEX_C0_NCT38XX = 0,
	IOEX_C2_NCT38XX,
	IOEX_PORT_COUNT
};

enum battery_type {
	BATTERY_POWER_TECH,
	BATTERY_LGC011,
	BATTERY_TYPE_COUNT
};

enum pwm_channel {
	PWM_CH_LED2 = 0,		/* PWM0 (white charger) */
	PWM_CH_LED3,			/* PWM1 */
	PWM_CH_LED1,			/* PWM2 (orange charger) */
	PWM_CH_KBLIGHT,			/* PWM3 */
	PWM_CH_FAN,			/* PWM5 */
	PWM_CH_LED4,			/* PWM7 */
	PWM_CH_COUNT
};

enum fan_channel {
	FAN_CH_0 = 0,
	FAN_CH_COUNT
};

enum mft_channel {
	MFT_CH_0 = 0,
	MFT_CH_COUNT
};

/*
 * remove when we enable CONFIG_POWER_BUTTON
 */

void power_button_interrupt(enum gpio_signal signal);

/*
 * remove when we enable CONFIG_THROTTLE_AP
 */

void throttle_ap_prochot_input_interrupt(enum gpio_signal signal);

/*
 * remove when we enable CONFIG_VOLUME_BUTTONS
 */

void button_interrupt(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
