/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#ifndef __CROS_EC_GPIO_H
#define __CROS_EC_GPIO_H

#include "common.h"

/* Flag definitions for gpio_info and gpio_alt_func */
/* The following are valid for both gpio_info and gpio_alt_func: */
#define GPIO_OPEN_DRAIN  (1 << 0)  /* Output type is open-drain */
#define GPIO_PULL_UP     (1 << 1)  /* Enable on-chip pullup */
#define GPIO_PULL_DOWN   (1 << 2)  /* Enable on-chip pulldown */
/* The following are valid for gpio_alt_func only */
#define GPIO_ANALOG      (1 << 3)  /* Set pin to analog-mode */
/* The following are valid for gpio_info only */
#define GPIO_INPUT       0         /* Input */
#define GPIO_OUTPUT      (1 << 4)  /* Output */
#define GPIO_LOW         (1 << 5)  /* If GPIO_OUTPUT, set level low */
#define GPIO_HIGH        (1 << 6)  /* If GPIO_OUTPUT, set level high */
#define GPIO_INT_RISING  (1 << 7)  /* Interrupt on rising edge */
#define GPIO_INT_FALLING (1 << 8)  /* Interrupt on falling edge */
#define GPIO_INT_BOTH    (1 << 9)  /* Interrupt on both edges */
#define GPIO_INT_LOW     (1 << 10) /* Interrupt on low level */
#define GPIO_INT_HIGH    (1 << 11) /* Interrupt on high level */
#define GPIO_DEFAULT     (1 << 12) /* Don't set up on boot */

/* Common flag combinations */
#define GPIO_OUT_LOW     (GPIO_OUTPUT | GPIO_LOW)
#define GPIO_OUT_HIGH    (GPIO_OUTPUT | GPIO_HIGH)
#define GPIO_ODR_HIGH    (GPIO_OUTPUT | GPIO_OPEN_DRAIN | GPIO_HIGH)
#define GPIO_ODR_LOW     (GPIO_OUTPUT | GPIO_OPEN_DRAIN | GPIO_LOW)
#define GPIO_INT_EDGE    (GPIO_INT_RISING | GPIO_INT_FALLING | GPIO_INT_BOTH)
#define GPIO_INT_LEVEL   (GPIO_INT_LOW | GPIO_INT_HIGH)
#define GPIO_INT_ANY     (GPIO_INT_EDGE | GPIO_INT_LEVEL)
/* Note that if no flags are present, the signal is a high-Z input */

/* GPIO signal definition structure, for use by board.c */
struct gpio_info {
	/* Signal name */
	const char *name;

	/* Port base address */
	uint32_t port;

	/* Bitmask on that port (1 << N; 0 = signal not implemented) */
	int mask;

	/* Flags (GPIO_*; see above) */
	uint32_t flags;

	/*
	 * Interrupt handler.  If non-NULL, and the signal's interrupt is
	 * enabled, this will be called in the context of the GPIO interrupt
	 * handler.
	 */
	void (*irq_handler)(enum gpio_signal signal);
};

/* Signal information from board.c.  Must match order from enum gpio_signal. */
extern const struct gpio_info gpio_list[];

/* Macro for signals which don't exist */
#ifdef CHIP_lm4
#define GPIO_SIGNAL_NOT_IMPLEMENTED(name) {name, LM4_GPIO_A, 0, 0, NULL}
#else
#define GPIO_SIGNAL_NOT_IMPLEMENTED(name) {name, GPIO_A, 0, 0, NULL}
#endif

/* GPIO alternate function structure, for use by board.c */
struct gpio_alt_func {
	/* Port base address */
	uint32_t port;

	/* Bitmask on that port (multiple bits allowed) */
	uint32_t mask;

	/* Alternate function number */
	int8_t func;

	/* Module ID (as uint8_t, since enum would be 32-bit) */
	uint8_t module_id;

	/* Flags (GPIO_*; see above). */
	uint16_t flags;
};

extern const struct gpio_alt_func gpio_alt_funcs[];
extern const int gpio_alt_funcs_count;

/**
 * Pre-initialize GPIOs.
 *
 * This occurs before clocks or tasks are set up.
 */
void gpio_pre_init(void);

/**
 * Configure GPIO pin functions for a module.
 *
 * @param id		Module ID to initialize
 * @param enable	Enable alternate functions if 1; high-Z pins if 0.
 */
void gpio_config_module(enum module_id id, int enable);

/**
 * Get the current value of a signal.
 *
 * @param signal	Signal to get
 * @return 0 if low, 1 if high.
 */
int gpio_get_level(enum gpio_signal signal);

/**
 * Get faster access to a GPIO level.
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
 * Return the name of a given GPIO signal.
 *
 * @param signal	Signal to name
 * @returns name of the given signal
 */
const char *gpio_get_name(enum gpio_signal signal);

/**
 * Set the flags for a signal.
 *
 * @param signal	Signal to set flags for
 * @param flags		New flags for the signal
 */
void gpio_set_flags(enum gpio_signal signal, int flags);

/**
 * Set the value of a signal.
 *
 * @param signal	Signal to set
 * @param value		New value for signal (0 = low, != high */
void gpio_set_level(enum gpio_signal signal, int value);

/**
 * Enable interrupts for the signal.
 *
 * The signal must have been defined with
 * an interrupt handler.  Normally called by the module which handles the
 * interrupt, once it's ready to start processing interrupts.
 *
 * @param signal	Signal to enable interrrupts for
 * @return EC_SUCCESS, or non-zero if error.
 */
int gpio_enable_interrupt(enum gpio_signal signal);

/**
 * Set flags for GPIO(s) by port and mask.
 *
 * Use gpio_set_flags() to set flags for an individual GPIO by id.
 *
 * Note that modules should usually declare their GPIO alternate functions in
 * gpio_alt_funcs[] and call gpio_init_module() instead of calling this
 * directly.
 *
 * @param port		GPIO port to set (GPIO_*)
 * @param mask		Bitmask of pins on that port to affect
 * @param flags		Flags (GPIO_*; see above)
 */
void gpio_set_flags_by_mask(uint32_t port, uint32_t mask, uint32_t flags);

/**
 * Set alternate function for GPIO(s).
 *
 * Note that modules should usually declare their GPIO alternate functions in
 * gpio_alt_funcs[] and call gpio_init_module() instead of calling this
 * directly.
 *
 * @param port		GPIO port to set (GPIO_*)
 * @param mask		Bitmask of pins on that port to affect
 * @param func		Alternate function; if <0, configures the specified
 *			GPIOs for normal GPIO operation.
 */
void gpio_set_alternate_function(int port, int mask, int func);

#endif  /* __CROS_EC_GPIO_H */
