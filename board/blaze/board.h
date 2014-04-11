/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Blaze board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* Optional features */
#define CONFIG_AP_HANG_DETECT
#define CONFIG_BATTERY_SMART
#define CONFIG_CHARGER
#define CONFIG_CHARGER_V2
#define CONFIG_CHARGER_BQ24725
#define CONFIG_CHIPSET_TEGRA
#define CONFIG_POWER_COMMON
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_HOST_COMMAND_STATUS
#define CONFIG_I2C
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_SPI
#define CONFIG_PWM
#define CONFIG_POWER_BUTTON
#define CONFIG_VBOOT_HASH
#define CONFIG_LED_COMMON

#ifndef __ASSEMBLER__

/* Keyboard output port list */
#define KB_OUT_PORT_LIST GPIO_A, GPIO_B, GPIO_C

/* Single I2C port, where the EC is the master. */
#define I2C_PORT_MASTER 0
#define I2C_PORT_BATTERY I2C_PORT_MASTER
#define I2C_PORT_CHARGER I2C_PORT_MASTER

/* Timer selection */
#define TIM_CLOCK_MSB 3
#define TIM_CLOCK_LSB 9
#define TIM_POWER_LED 2
#define TIM_WATCHDOG  4

/* GPIO signal list */
enum gpio_signal {
	/* Inputs with interrupt handlers are first for efficiency */
	GPIO_POWER_BUTTON_L = 0,
	GPIO_SOC1V8_XPSHOLD,
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
	GPIO_ENTERING_RW,
	GPIO_I2C1_SCL,
	GPIO_I2C1_SDA,
	GPIO_LED_POWER_L,  /* alias to GPIO_PWR_LED1 */
	GPIO_PMIC_PWRON_L,
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
	GPIO_AC_LED,
	GPIO_CHG_LED,
	GPIO_BAT_LED1,
	GPIO_CHARGING,
	GPIO_EC_BL_OVERRIDE,
	GPIO_PMIC_THERM_L,
	GPIO_PMIC_WARM_RESET_L,
	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

enum power_signal {
	TEGRA_XPSHOLD = 0,
	TEGRA_SUSPEND_ASSERTED,

	/* Number of power signals */
	POWER_SIGNAL_COUNT
};

enum pwm_channel {
	PWM_CH_POWER_LED = 0,
	/* Number of PWM channels */
	PWM_CH_COUNT
};

/* Charger module */
#define CONFIG_CHARGER_SENSE_RESISTOR 10 /* Charge sense resistor, mOhm */
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20 /* Input sensor resistor, mOhm */
#define CONFIG_CHARGER_INPUT_CURRENT 4032 /* mA, based on Link HW design */
#define CONFIG_CHARGER_CURRENT_LIMIT 3000 /* PL102 inductor 3.0A(3.8A) */

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
