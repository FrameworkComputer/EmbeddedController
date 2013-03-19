/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* McCroskey board configuration */

#ifndef __BOARD_H
#define __BOARD_H

/* 48 MHz SYSCLK clock frequency */
/*
 * FIXME(dhendrix): We'll eventually switch to the HSE instead of HSI.
 * Also, I2C is limited to 2-36MHz, so for now let's just use 16MHz until
 * we're ready to switch to the HSE. FREQ in I2C1 CR2 also must be set
 * appropriately.
 */
#define CPU_CLOCK 48000000	/* should be 48000000 */

/* Use USART1 as console serial port */
#define CONFIG_CONSOLE_UART 1

/* Debug features */
#define CONFIG_PANIC_HELP
#define CONFIG_ASSERT_HELP
#define CONFIG_CONSOLE_CMDHELP

#undef  CONFIG_TASK_PROFILING
#define CONFIG_WATCHDOG_HELP

/* use STOP mode when we have nothing to do */
/*
 * FIXME(dhendrix): This causes the UART to drop characters and likely
 * other bad side-effects. Disable for now.
 */
#if 0
#define CONFIG_LOW_POWER_IDLE
#endif

#ifndef __ASSEMBLER__

/* By default, enable all console messages except keyboard */
#define CC_DEFAULT	(CC_ALL)

#define USB_CHARGE_PORT_COUNT 0

/* EC drives 13 outputs to the keyboard matrix and reads 8 inputs/interrupts */
#define KB_INPUTS 8
#define KB_OUTPUTS 13
#define KB_OUT_PORT_LIST GPIO_C

/* EC is I2C master */
#define CONFIG_I2C
#define I2C_PORT_HOST 0
#define I2C_PORT_SLAVE 0	/* needed for DMAC macros (ugh) */
#define CONFIG_DEBUG_I2C	/* FIXME(dhendrix): remove this eventually */
#define GPIO_I2C2_SCL 0		/* unused, but must be defined anyway */
#define GPIO_I2C2_SDA 0		/* unused, but must be defined anyway */

/* Timer selection */
#define TIM_CLOCK_MSB 3
#define TIM_CLOCK_LSB 4

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
	GPIO_WRITE_PROTECTn,

	/* FIXME: this will be an alt. function GPIO, so remove it from here */
	GPIO_BL_PWM,
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

void configure_board(void);

/* FIXME: this should not be needed on mccroskey. */
void board_interrupt_host(int active);

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
