/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Spring board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 16 MHz SYSCLK clock frequency */
#define CPU_CLOCK 16000000


/* Debug features */
#undef  CONFIG_TASK_PROFILING

/* Optional features */
#define CONFIG_ADC
#define CONFIG_BATTERY_SMART
#define CONFIG_BOARD_PRE_INIT
#define CONFIG_CHARGER_TPS65090
#ifdef HAS_TASK_CHIPSET
#define CONFIG_CHIPSET_GAIA
#endif
#define CONFIG_CMD_PMU
#define CONFIG_CONSOLE_RESTRICTED_INPUT
#define CONFIG_EXTPOWER_USB
#define CONFIG_HOST_COMMAND_STATUS
#define CONFIG_I2C
#define CONFIG_I2C_PASSTHROUGH
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_LOW_POWER_IDLE	/* Use STOP mode when we have nothing to do */
#define CONFIG_LED_DRIVER_LP5562
#define CONFIG_PMU_FORCE_FET	/* Always enable 3G modem power rail */
#define CONFIG_PMU_HARD_RESET
#define CONFIG_PMU_TPS65090
#define CONFIG_USB_SWITCH_TSU6721

#ifndef __ASSEMBLER__

/* By default, enable all console messages except keyboard */
#define CC_DEFAULT	(CC_ALL & ~CC_MASK(CC_KEYSCAN))

/* Keyboard output port list */
#define KB_OUT_PORT_LIST GPIO_B, GPIO_C

/* Charging */
#define I2C_PORT_HOST 0
#define I2C_PORT_BATTERY I2C_PORT_HOST
#define I2C_PORT_CHARGER I2C_PORT_HOST
#define I2C_PORT_SLAVE 1
#define I2C_PORTS_USED 1

/* Low battery threshold. In mAh. */
#define BATTERY_AP_OFF_LEVEL 1

/* Timer selection */
#define TIM_CLOCK_MSB 2
#define TIM_CLOCK_LSB 4
#define TIM_WATCHDOG  1

/* ADC signal */
enum adc_channel {
	ADC_CH_USB_VBUS_SNS = 0,
	ADC_CH_USB_DP_SNS,
	ADC_CH_USB_DN_SNS,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/* GPIO signal list */
enum gpio_signal {
	/* Inputs with interrupt handlers are first for efficiency */
	GPIO_KB_PWR_ON_L = 0,  /* Keyboard power button */
	GPIO_PP1800_LDO2,      /* LDO2 is ON (end of PMIC sequence) */
	GPIO_SOC1V8_XPSHOLD,   /* App Processor ON  */
	GPIO_CHARGER_INT,
	GPIO_LID_OPEN,         /* LID switch detection */
	GPIO_SUSPEND_L,        /* AP suspend/resume state */
	GPIO_WP_L,             /* Write protection pin (low active) */
	/* Keyboard inputs */
	GPIO_KB_IN00,
	GPIO_KB_IN01,
	GPIO_KB_IN02,
	GPIO_KB_IN03,
	GPIO_KB_IN04,
	GPIO_KB_IN05,
	GPIO_KB_IN06,
	GPIO_KB_IN07,
	GPIO_USB_CHG_INT,
	/* Other inputs */
	GPIO_BCHGR_VACG,       /* AC good on TPSChrome */
	GPIO_I2C1_SCL,
	GPIO_I2C1_SDA,
	GPIO_I2C2_SCL,
	GPIO_I2C2_SDA,
	/* Outputs */
	GPIO_EN_PP1350,        /* DDR 1.35v rail enable */
	GPIO_EN_PP5000,        /* 5.0v rail enable */
	GPIO_EN_PP3300,        /* 3.3v rail enable */
	GPIO_PMIC_PWRON_L,     /* 5v rail ready */
	GPIO_PMIC_RESET,       /* Force hard reset of the pmic */
	GPIO_ENTERING_RW,      /* EC is R/W mode for the kbc mux */
	GPIO_CHARGER_EN,
	GPIO_EC_INT,
	GPIO_ID_MUX,
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
	GPIO_BOOST_EN,
	GPIO_ILIM,
	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
