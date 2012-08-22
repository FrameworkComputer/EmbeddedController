/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#ifndef __CROS_EC_GPIO_H
#define __CROS_EC_GPIO_H

#include "board.h"  /* For board-dependent enum gpio_signal list */
#include "common.h"


/* Flag definitions for gpio_info. */
#define GPIO_INPUT       0x0000  /* Input */
#define GPIO_OUTPUT      0x0001  /* Output */
#define GPIO_PULL        0x0002  /* Input with on-chip pullup/pulldown */
#define GPIO_HIGH        0x0004  /* If GPIO_OUTPUT, default high; if GPIO_PULL,
				  * pull up (otherwise default low / pull
				  * down) */
#define GPIO_OPEN_DRAIN  0x0008  /* Output type is open-drain */
#define GPIO_INT_RISING  0x0010  /* Interrupt on rising edge */
#define GPIO_INT_FALLING 0x0020  /* Interrupt on falling edge */
#define GPIO_INT_BOTH    0x0040  /* Interrupt on both edges */
#define GPIO_INT_LOW     0x0080  /* Interrupt on low level */
#define GPIO_INT_HIGH    0x0100  /* Interrupt on high level */
#define GPIO_DEFAULT     0x0200  /* Don't set up on boot */

/* Common flag combinations */
#define GPIO_OUT_LOW     GPIO_OUTPUT
#define GPIO_OUT_HIGH    (GPIO_OUTPUT | GPIO_HIGH)
#define GPIO_PULL_DOWN   GPIO_PULL
#define GPIO_PULL_UP     (GPIO_PULL | GPIO_HIGH)
#define GPIO_HI_Z        (GPIO_OUTPUT | GPIO_OPEN_DRAIN | GPIO_HIGH)
#define GPIO_INT_EDGE    (GPIO_INT_RISING | GPIO_INT_FALLING | GPIO_INT_BOTH)
#define GPIO_INT_LEVEL   (GPIO_INT_LOW | GPIO_INT_HIGH)
#define GPIO_INT_ANY     (GPIO_INT_EDGE | GPIO_INT_LEVEL)
/* Note that if no flags are present, the signal is a high-Z input */

/* GPIO signal definition structure, for use by board.c */
struct gpio_info {
	const char *name;
	int port;         /* Port (LM4_GPIO_*) */
	int mask;         /* Bitmask on that port (0x01 - 0x80; 0x00 =
			   * signal not implemented) */
	uint32_t flags;   /* Flags (GPIO_*) */
	/* Interrupt handler.  If non-NULL, and the signal's interrupt is
	 * enabled, this will be called in the context of the GPIO interrupt
	 * handler. */
	void (*irq_handler)(enum gpio_signal signal);
};

/* Signal information from board.c.  Must match order from enum gpio_signal. */
extern const struct gpio_info gpio_list[GPIO_COUNT];

/* Macro for signals which don't exist */
#define GPIO_SIGNAL_NOT_IMPLEMENTED(name) {name, LM4_GPIO_A, 0, 0, NULL}


/* Pre-initializes the module.  This occurs before clocks or tasks are
 * set up. */
int gpio_pre_init(void);

/* Get the current value of a signal (0=low, 1=hi). */
int gpio_get_level(enum gpio_signal signal);

/**
 * Get faster access to a GPIO level
 *
 * Use this function to find out the register address and mask for a GPIO
 * value. Then you can just check that instead of calling gpio_get_level().
 *
 * @param signal	Signal to return details for
 * @param mask		Mask value to use
 * @return pointer to register to read to get GPIO value
 */
uint16_t *gpio_get_level_reg(enum gpio_signal signal, uint32_t *mask);

/**
 * Returns the name of a given GPIO signal.
 *
 * @param signal	Signal to return.
 * @returns name of the given signal
 */
const char *gpio_get_name(enum gpio_signal signal);

/* Set the flags for a signal.  Note that this does not set the signal level
 * based on the presence/absence of GPIO_HIGH; call gpio_set_level() afterwards
 * to do that if needed. */
int gpio_set_flags(enum gpio_signal signal, int flags);

/* Set the current value of a signal. */
int gpio_set_level(enum gpio_signal signal, int value);

/* Enable interrupts for the signal.  The signal must have been defined with
 * an interrupt handler.  Normally called by the module which handles the
 * interrupt, once it's ready to start processing interrupts. */
int gpio_enable_interrupt(enum gpio_signal signal);

/* Set alternate function <func> for GPIO <port> (LM4_GPIO_*) and <mask>.  If
 * func==0, configures the specified GPIOs for normal GPIO operation.
 *
 * This is intended for use by other modules' configure_gpio() functions. */
void gpio_set_alternate_function(int port, int mask, int func);

#endif  /* __CROS_EC_GPIO_H */
