/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#ifndef __CROS_EC_GPIO_H
#define __CROS_EC_GPIO_H

#include "common.h"

/*
 * GPIO_CONFIG_ALL_PORTS signifies a "don't care" for the GPIO port.  This is
 * used in gpio_config_pins().  When the port parameter is set to this, the
 * pin_mask parameter is ignored.
 */
#define GPIO_CONFIG_ALL_PORTS 0xFFFFFFFF

/* Flag definitions for gpio_info and gpio_alt_func */
/* The following are valid for both gpio_info and gpio_alt_func: */
#define GPIO_OPEN_DRAIN    (1 << 0)  /* Output type is open-drain */
#define GPIO_PULL_UP       (1 << 1)  /* Enable on-chip pullup */
#define GPIO_PULL_DOWN     (1 << 2)  /* Enable on-chip pulldown */
/* The following are valid for gpio_alt_func only */
#define GPIO_ANALOG        (1 << 3)  /* Set pin to analog-mode */
/* The following are valid for gpio_info only */
#define GPIO_INPUT         (1 << 4)  /* Input */
#define GPIO_OUTPUT        (1 << 5)  /* Output */
#define GPIO_LOW           (1 << 6)  /* If GPIO_OUTPUT, set level low */
#define GPIO_HIGH          (1 << 7)  /* If GPIO_OUTPUT, set level high */
#define GPIO_INT_F_RISING  (1 << 8)  /* Interrupt on rising edge */
#define GPIO_INT_F_FALLING (1 << 9)  /* Interrupt on falling edge */
#define GPIO_INT_F_LOW     (1 << 11) /* Interrupt on low level */
#define GPIO_INT_F_HIGH    (1 << 12) /* Interrupt on high level */
#define GPIO_DEFAULT       (1 << 13) /* Don't set up on boot */
#define GPIO_INT_DSLEEP    (1 << 14) /* Interrupt in deep sleep */
#define GPIO_INT_SHARED    (1 << 15) /* Shared among multiple pins */
#define GPIO_SEL_1P8V      (1 << 16) /* Support 1.8v */

/* Common flag combinations */
#define GPIO_OUT_LOW        (GPIO_OUTPUT | GPIO_LOW)
#define GPIO_OUT_HIGH       (GPIO_OUTPUT | GPIO_HIGH)
#define GPIO_ODR_HIGH       (GPIO_OUTPUT | GPIO_OPEN_DRAIN | GPIO_HIGH)
#define GPIO_ODR_LOW        (GPIO_OUTPUT | GPIO_OPEN_DRAIN | GPIO_LOW)
#define GPIO_INT_RISING     (GPIO_INPUT | GPIO_INT_F_RISING)
#define GPIO_INT_FALLING    (GPIO_INPUT | GPIO_INT_F_FALLING)
/* TODO(crosbug.com/p/24204): "EDGE" would have been clearer than "BOTH". */
#define GPIO_INT_BOTH       (GPIO_INT_RISING | GPIO_INT_FALLING)
#define GPIO_INT_LOW        (GPIO_INPUT | GPIO_INT_F_LOW)
#define GPIO_INT_HIGH       (GPIO_INPUT | GPIO_INT_F_HIGH)
#define GPIO_INT_LEVEL      (GPIO_INT_LOW | GPIO_INT_HIGH)
#define GPIO_INT_ANY        (GPIO_INT_BOTH | GPIO_INT_LEVEL)
#define GPIO_INT_BOTH_DSLEEP (GPIO_INT_BOTH | GPIO_INT_DSLEEP)

/* Convert GPIO mask to GPIO number / index. */
#define GPIO_MASK_TO_NUM(mask) (__fls(mask))

/* Convert a GPIO to a port + mask pair */
#define GPIO_TO_PORT_MASK_PAIR(gpio) \
	{ gpio_list[(gpio)].port, \
	  GPIO_MASK_TO_NUM(gpio_list[(gpio)].mask) }

/* NOTE: This is normally included from board.h, thru config.h and common.h But,
 * some boards and unit tests don't have a gpio_signal enum defined, so we
 * define an emtpy one here.*/
#ifndef __CROS_EC_GPIO_SIGNAL_H
enum gpio_signal {
	GPIO_COUNT
};
#endif /* __CROS_EC_GPIO_SIGNAL_H */

/* GPIO signal definition structure, for use by board.c */
struct gpio_info {
	/* Signal name */
	const char *name;

	/* Port base address */
	uint32_t port;

	/* Bitmask on that port (1 << N; 0 = signal not implemented) */
	uint32_t mask;

	/* Flags (GPIO_*; see above) */
	uint32_t flags;
};

/* Signal information from board.c.  Must match order from enum gpio_signal. */
extern const struct gpio_info gpio_list[];

/* Interrupt handler table for those GPIOs which have IRQ handlers.
 *
 * If the signal's interrupt is enabled, this will be called in the
 * context of the GPIO interrupt handler.
 */
extern void (* const gpio_irq_handlers[])(enum gpio_signal signal);
extern const int gpio_ih_count;
#define GPIO_IH_COUNT gpio_ih_count

/* GPIO alternate function structure, for use by board.c */
struct gpio_alt_func {
	/* Port base address */
	uint32_t port;

	/* Bitmask on that port (multiple bits allowed) */
	uint32_t mask;

	/* Alternate function number */
	uint8_t func;

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
 * Enable/disable alternate function for pins
 *
 * Note, you can also configure an entire module by setting the port parameter
 * equal to GPIO_CONFIG_ALL_PORTS.
 *
 * @param id		module ID of pins
 * @param port		Port of pins
 * @param pin_mask	Bit mask of pins
 * @param enable	Enable alternate functions if 1; high-Z pins if 0
 * @return EC_SUCCESS, or non-zero if pins are not found.
 * */
int gpio_config_pins(enum module_id id,
		     uint32_t port, uint32_t pin_mask, int enable);

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
 * Reset the GPIO flags and alternate function state
 *
 * This returns the GPIO to it's default state of being a GPIO (not
 * configured as an alternate function) with its default flags (those
 * specified in gpio.inc when it was defined).
 *
 * @param signal	Signal to reset
 */
void gpio_reset(enum gpio_signal signal);

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
 * Disable interrupts for the signal.
 *
 * The signal must have been defined with
 * an interrupt handler.  Normally called by the module which handles the
 * interrupt, if it doesn't want to process interrupts.
 *
 * @param signal	Signal to disable interrupts for
 * @return EC_SUCCESS, or non-zero if error.
 */
int gpio_disable_interrupt(enum gpio_signal signal);

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
void gpio_set_alternate_function(uint32_t port, uint32_t mask, int func);

/**
 * Enable GPIO peripheral clocks.
 *
 * This function is used by the GPIO implementation and should not be called
 * outside of that context.
 */
void gpio_enable_clocks(void);

#endif  /* __CROS_EC_GPIO_H */
