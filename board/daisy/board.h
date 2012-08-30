/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
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

/* use I2C for host communication */
#define CONFIG_I2C

/* Debug features */
#define CONFIG_PANIC_HELP
#define CONFIG_PANIC_NEW_STACK
#define CONFIG_ASSERT_HELP

/* compute RW firmware hash at startup */
#define CONFIG_VBOOT

/* Allow dangerous commands all the time, since we don't have a write protect
 * switch. */
/* TODO: (crosbug.com/p/9986) This is a serious security hole and should be
 * removed in mass production. We add this to allow manual firmware update.
 * Once we complete the vboot and autoupdate, we should remove this. */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

/* By default, enable all console messages except keyboard */
#define CC_DEFAULT	(CC_ALL & ~CC_MASK(CC_KEYSCAN))

#define USB_CHARGE_PORT_COUNT 0

/* EC drives 13 outputs to keyboard matrix */
#define KB_OUTPUTS 13

/* Charging */
#define CONFIG_SMART_BATTERY
#define CONFIG_PMU_TPS65090
#define CONFIG_I2C_HOST_AUTO
#define I2C_PORT_HOST board_i2c_host_port()
#define I2C_PORT_BATTERY I2C_PORT_HOST
#define I2C_PORT_CHARGER I2C_PORT_HOST
#define I2C_PORT_SLAVE 1

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
	/* Number of GPIOs; not an actual GPIO */
	GPIO_COUNT
};

void configure_board(void);

void matrix_interrupt(enum gpio_signal signal);

/* Signal to the AP that keyboard scan data is available */
void board_keyboard_suppress_noise(void);

/* Signal to AP that data is waiting */
void board_interrupt_host(int active);

/* Auto detect EC i2c host port */
int board_i2c_host_port(void);

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
