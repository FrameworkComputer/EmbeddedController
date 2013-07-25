/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Pit board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* Optional features */
#define CONFIG_BATTERY_BQ20Z453
#define CONFIG_BATTERY_SMART
#define CONFIG_BOARD_POST_GPIO_INIT
#ifdef HAS_TASK_CHARGER
#define CONFIG_CHARGER_TPS65090
#endif
#ifdef HAS_TASK_CHIPSET
#define CONFIG_CHIPSET_GAIA
#endif
#define CONFIG_CMD_PMU
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_HOST_COMMAND_STATUS
#define CONFIG_I2C
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_PMU_HARD_RESET
#define CONFIG_PMU_POWERINFO
#define CONFIG_PMU_TPS65090
#define CONFIG_SPI

#ifndef __ASSEMBLER__

/* By default, enable all console messages except keyboard */
#define CC_DEFAULT	(CC_ALL & ~CC_MASK(CC_KEYSCAN))

/* Keyboard output port list */
#define KB_OUT_PORT_LIST GPIO_A, GPIO_B, GPIO_C

/*
 * Charging.
 *
 * "HOST" means the port where the EC is the master, which has the battery,
 * charger and PMU.
 *
 * "SLAVE" means the port where the EC is the slave, which has the AP (host
 * processor).
 *
 * TODO: In this context, "host" is badly overloaded and confusing.
 */
#define I2C_PORT_HOST 0
#define I2C_PORT_BATTERY I2C_PORT_HOST
#define I2C_PORT_CHARGER I2C_PORT_HOST
#define I2C_PORT_SLAVE 1
#define I2C_PORTS_USED 1

/* Charger sense resistors */
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 12
#define CONFIG_CHARGER_SENSE_RESISTOR 16

/* Timer selection */
#define TIM_CLOCK_MSB 3
#define TIM_CLOCK_LSB 9
#define TIM_POWER_LED 2
#define TIM_WATCHDOG  4

/* GPIO signal list */
enum gpio_signal {
	/* Inputs with interrupt handlers are first for efficiency */
	GPIO_KB_PWR_ON_L = 0,
	GPIO_PP1800_LDO2,
	GPIO_SOC1V8_XPSHOLD,
	GPIO_CHARGER_INT,
	GPIO_LID_OPEN,
	GPIO_SUSPEND_L,
	GPIO_SPI1_NSS,
	GPIO_AC_PRESENT,
	/* Keyboard inputs */
	GPIO_KB_IN00,
	GPIO_KB_IN01,
	GPIO_KB_IN02,
	GPIO_KB_IN03,
	GPIO_KB_IN04,
	GPIO_KB_IN05,
	GPIO_KB_IN06,
	GPIO_KB_IN07,
	/* Other inputs */
	GPIO_WP_L,
	/* Outputs */
	GPIO_AP_RESET_L,
	GPIO_CHARGER_EN,
	GPIO_EC_INT,
	GPIO_EN_PP1350,
	GPIO_EN_PP3300,
	GPIO_EN_PP5000,
	GPIO_ENTERING_RW,
	GPIO_I2C1_SCL,
	GPIO_I2C1_SDA,
	GPIO_I2C2_SCL,
	GPIO_I2C2_SDA,
	GPIO_LED_POWER_L,
	GPIO_PMIC_PWRON,
	GPIO_PMIC_RESET,
	GPIO_KB_OUT00,
	GPIO_KB_OUT01,
	GPIO_KB_OUT02,
	GPIO_KB_OUT03,
	GPIO_KB_OUT04,
	GPIO_KB_OUT05,
	GPIO_KB_OUT06,
	GPIO_KB_OUT07,
	GPIO_KB_OUT08,
	GPIO_KB_OUT09,
	GPIO_KB_OUT10,
	GPIO_KB_OUT11,
	GPIO_KB_OUT12,
	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
