/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Daisy board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 16 MHz SYSCLK clock frequency */
#define CPU_CLOCK 16000000

/* Use USART1 as console serial port */
#define CONFIG_CONSOLE_UART 1

/* Debug features */
#define CONFIG_PANIC_HELP
#define CONFIG_ASSERT_HELP

/* Optional features */
#define CONFIG_BATTERY_SMART
#define CONFIG_BOARD_POST_GPIO_INIT
#define CONFIG_CHARGER_TPS65090
#ifdef HAS_TASK_CHIPSET
#define CONFIG_CHIPSET_GAIA
#endif
#define CONFIG_EXTPOWER_SNOW
#define CONFIG_I2C
#define CONFIG_I2C_HOST_AUTO
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_KEYBOARD_SUPPRESS_NOISE
#define CONFIG_LID_SWITCH
#define CONFIG_PMU_TPS65090

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 *
 * TODO: (crosbug.com/p/9986) This is a serious security hole and should be
 * removed in mass production.  Acceptable here only because daisy isn't a
 * production board.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

/* By default, enable all console messages except keyboard */
#define CC_DEFAULT	(CC_ALL & ~CC_MASK(CC_KEYSCAN))

/* Keyboard output port list */
#define KB_OUT_PORT_LIST GPIO_B, GPIO_C

/* Charging */
#define I2C_PORT_HOST board_i2c_host_port()
#define I2C_PORT_BATTERY I2C_PORT_HOST
#define I2C_PORT_CHARGER I2C_PORT_HOST
#define I2C_PORT_SLAVE 1
#define I2C_PORTS_USED 2  /* Since host could be on either 0 or 1 */

/* Timer selection */
#define TIM_CLOCK_MSB 3
#define TIM_CLOCK_LSB 4

/* GPIO signal list */
enum gpio_signal {
	/* Inputs with interrupt handlers are first for efficiency */
	GPIO_KB_PWR_ON_L = 0,    /* Keyboard power button */
	GPIO_PP1800_LDO2,      /* LDO2 is ON (end of PMIC sequence) */
	GPIO_SOC1V8_XPSHOLD,   /* App Processor ON  */
	GPIO_CHARGER_INT,
	GPIO_LID_OPEN,         /* LID switch detection */
	GPIO_SUSPEND_L,        /* AP suspend/resume state */
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
	GPIO_AC_PWRBTN_L,
	GPIO_SPI1_NSS,
	GPIO_I2C1_SCL,
	GPIO_I2C1_SDA,
	GPIO_I2C2_SCL,
	GPIO_I2C2_SDA,
	/* Outputs */
	GPIO_AC_STATUS,
	GPIO_SPI1_MISO,
	GPIO_EN_PP1350,        /* DDR 1.35v rail enable */
	GPIO_EN_PP5000,        /* 5.0v rail enable */
	GPIO_EN_PP3300,        /* 3.3v rail enable */
	GPIO_PMIC_PWRON_L,	/* 5v rail ready */
	GPIO_ENTERING_RW,      /* EC is R/W mode for the kbc mux */
	GPIO_CHARGER_EN,
	GPIO_POWER_LED_L,      /* Power state keyboard LED */
	GPIO_EC_INT,
	GPIO_CODEC_INT,        /* To audio codec (KB noise cancellation) */
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
	/* Unimplemented signals we emulate */
	GPIO_WP_L,
	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

/* Auto detect EC i2c host port */
int board_i2c_host_port(void);

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
