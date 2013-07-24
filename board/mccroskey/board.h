/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* McCroskey board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* Debug features */
#define CONFIG_I2C_DEBUG	/* FIXME(dhendrix): remove this eventually */
#undef  CONFIG_TASK_PROFILING

/* Optional features */
#define CONFIG_BOARD_PRE_INIT
#define CONFIG_KEYBOARD_PROTOCOL_MKBP

/*
 * TODO(dhendrix): Stop mode causes the UART to drop characters and likely
 * other bad side-effects. Disable for now.
 */
#undef   CONFIG_LOW_POWER_IDLE

#ifndef __ASSEMBLER__

/* By default, enable all console messages except keyboard */
#define CC_DEFAULT	(CC_ALL)

/* Keyboard output ports */
#define KB_OUT_PORT_LIST GPIO_C

/* EC is I2C master */
#define I2C_PORT_HOST 0
#define I2C_PORT_SLAVE 0	/* needed for DMAC macros (ugh) */
#define GPIO_I2C2_SCL 0		/* unused, but must be defined anyway */
#define GPIO_I2C2_SDA 0		/* unused, but must be defined anyway */

/* Timer selection */
#define TIM_CLOCK_MSB 3
#define TIM_CLOCK_LSB 4
#define TIM_WATCHDOG  1

/* GPIO signal list */
enum gpio_signal {
	GPIO_KB_IN00,
	GPIO_KB_IN01,
	GPIO_KB_IN02,
	GPIO_KB_IN03,
	GPIO_KB_IN04,
	GPIO_KB_IN05,
	GPIO_KB_IN06,
	GPIO_KB_IN07,
	GPIO_KBD_PWR_BUTTON,
	GPIO_OMZO_RDY_L,
	GPIO_OZMO_RST_L,
	GPIO_VBUS_UP_DET,
	GPIO_OZMO_REQ_L,
	GPIO_CHARGE_ZERO,
	GPIO_CHARGE_SHUNT,
	GPIO_PMIC_INT_L,
	GPIO_I2C1_SCL,
	GPIO_I2C1_SDA,
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
	GPIO_USB_VBUS_CTRL,
	GPIO_HUB_RESET,
	GPIO_WP_L,

	/* FIXME: this will be an alt. function GPIO, so remove it from here */
	GPIO_BL_PWM,

	/* Unimplemented GPIOs */
	GPIO_EC_INT,
	GPIO_ENTERING_RW,

#if 0
	GPIO_STM_USBDM,
	GPIO_STM_USBDP,
	GPIO_JTMS_SWDIO,
	GPIO_JTCK_SWCLK,
	GPIO_JTDI,
	GPIO_JTDO,
	GPIO_JNTRST,
	GPIO_OSC32_OUT,
#endif
	GPIO_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
