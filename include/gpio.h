/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#ifndef __CROS_EC_GPIO_H
#define __CROS_EC_GPIO_H

#include "common.h"

/* Flag definitions for gpio_info and gpio_alt_func */
#define GPIO_FLAG_NONE     0       /* No flag needed, default setting */
/* The following are valid for both gpio_info and gpio_alt_func: */
#define GPIO_OPEN_DRAIN    BIT(0)  /* Output type is open-drain */
#define GPIO_PULL_UP       BIT(1)  /* Enable on-chip pullup */
#define GPIO_PULL_DOWN     BIT(2)  /* Enable on-chip pulldown */
/* The following are valid for gpio_alt_func only */
#define GPIO_ANALOG        BIT(3)  /* Set pin to analog-mode */
/* The following are valid for gpio_info only */
#define GPIO_INPUT         BIT(4)  /* Input */
#define GPIO_OUTPUT        BIT(5)  /* Output */
#define GPIO_LOW           BIT(6)  /* If GPIO_OUTPUT, set level low */
#define GPIO_HIGH          BIT(7)  /* If GPIO_OUTPUT, set level high */
#define GPIO_INT_F_RISING  BIT(8)  /* Interrupt on rising edge */
#define GPIO_INT_F_FALLING BIT(9)  /* Interrupt on falling edge */
#define GPIO_INT_F_LOW     BIT(11) /* Interrupt on low level */
#define GPIO_INT_F_HIGH    BIT(12) /* Interrupt on high level */
#define GPIO_DEFAULT       BIT(13) /* Don't set up on boot */
#define GPIO_INT_DSLEEP    BIT(14) /* Interrupt in deep sleep */
#define GPIO_INT_SHARED    BIT(15) /* Shared among multiple pins */
#define GPIO_SEL_1P8V      BIT(16) /* Support 1.8v */
#define GPIO_ALTERNATE     BIT(17) /* GPIO used for alternate function. */
#define GPIO_LOCKED        BIT(18) /* Lock GPIO output and configuration */
#define GPIO_HIB_WAKE_HIGH BIT(19) /* Hibernate wake on high level */
#ifdef CONFIG_GPIO_POWER_DOWN
#define GPIO_POWER_DOWN    BIT(20) /* Pin and pad is powered off */
#endif

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

/* NOTE: This is normally included from board.h, thru config.h and common.h But,
 * some boards and unit tests don't have a gpio_signal enum defined, so we
 * define an emtpy one here.*/
#ifndef __CROS_EC_GPIO_SIGNAL_H
enum gpio_signal {
	GPIO_COUNT
};
#endif /* __CROS_EC_GPIO_SIGNAL_H */

/* Alternate functions for GPIOs */
enum gpio_alternate_func {
	GPIO_ALT_FUNC_NONE = -1,
	GPIO_ALT_FUNC_DEFAULT,
	GPIO_ALT_FUNC_1,
	GPIO_ALT_FUNC_2,
	GPIO_ALT_FUNC_3,
	GPIO_ALT_FUNC_4,
	GPIO_ALT_FUNC_5,
	GPIO_ALT_FUNC_6,
	GPIO_ALT_FUNC_7,

	GPIO_ALT_FUNC_MAX = 63,
};

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
 * @return EC_SUCCESS, or non-zero if module_id is not found.
 */
int gpio_config_module(enum module_id id, int enable);

/**
 * Enable/disable alternate function for single pin
 *
 * @param id		module ID of pins
 * @param signal	Signal to configure
 * @param enable	Enable alternate function if 1; GPIO if 0
 * @return EC_SUCCESS, or non-zero if pin is not found.
 * */
int gpio_config_pin(enum module_id id, enum gpio_signal signal, int enable);

/**
 * Get the current value of a signal.
 *
 * @param signal	Signal to get
 * @return 0 if low, 1 if high.
 */
int gpio_get_level(enum gpio_signal signal);

/**
 * Read a ternary GPIO input, activating internal pull-down, then pull-up,
 * to check if the GPIO is high, low, or Hi-Z. Useful for board strappings.
 *
 * @param signal	Signal to get
 * @return 0 if low, 1 if high, 2 if Hi-Z.
 */
int gpio_get_ternary(enum gpio_signal signal);

/**
 * Return the name of a given GPIO signal.
 *
 * @param signal	Signal to name
 * @returns name of the given signal
 */
const char *gpio_get_name(enum gpio_signal signal);

/**
 * Determine if a GPIO is implemented.
 *
 * Some well known GPIO signal names may not be implemented on a particular
 * board.  This function can be used to determine if a GPIO is implemented.
 *
 * @param signal	Signal to query
 * @returns 0 if the GPIO is not implemented, 1 if it is.
 */
int gpio_is_implemented(enum gpio_signal signal);

/**
 * Set the flags for a signal.
 *
 * @param signal	Signal to set flags for
 * @param flags		New flags for the signal
 */
void gpio_set_flags(enum gpio_signal signal, int flags);

#if defined(CONFIG_CMD_GPIO_EXTENDED) && !defined(CONFIG_GPIO_GET_EXTENDED)
#define CONFIG_GPIO_GET_EXTENDED
#endif

#ifdef CONFIG_GPIO_GET_EXTENDED
/**
 * Get the current flags for a signal.
 *
 * @param signal	Signal to get flags for
 * @returns The flags that are currently defined for this signal
 */
int gpio_get_flags(enum gpio_signal signal);

/**
 * Get flags for GPIO by port and mask.
 *
 * @param port		GPIO port to set (GPIO_*)
 * @param mask		Bitmask of pins on that port to check: one only.
 */
int gpio_get_flags_by_mask(uint32_t port, uint32_t mask);
#endif

/**
 * Get the default flags for a signal.
 *
 * @param signal	Signal to set flags for
 * @returns The flags that were originally defined for this signal
 */
int gpio_get_default_flags(enum gpio_signal signal);

/**
 * Set the value of a signal.
 *
 * @param signal	Signal to set
 * @param value		New value for signal (0 = low, 1 = high)
 */
void gpio_set_level(enum gpio_signal signal, int value);

/**
 * Set the value of a signal that could be either a local GPIO or an IO
 * expander GPIO.
 *
 * @param signal	GPIO_* or IOEX_* signal to set
 * @param value		New value for signal (0 = low, 1 = high)
 */
void gpio_or_ioex_set_level(int signal, int value);

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
 * Clear pending interrupts for the signal.
 *
 * The signal must have been defined with an interrupt handler.
 *
 * @param signal	Signal to clear interrupts for
 * @return EC_SUCCESS, or non-zero on error.
 */
int gpio_clear_pending_interrupt(enum gpio_signal signal);

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
 * @param func		Alternate function; if GPIO_ALT_FUNC_NONE, configures
 *                      the specified GPIOs for normal GPIO operation.
 */
void gpio_set_alternate_function(uint32_t port, uint32_t mask,
				enum gpio_alternate_func func);

#ifdef CONFIG_GPIO_POWER_DOWN
/**
 * Power down all GPIO pins in a module.
 *
 * @param id		Module ID to initialize
 * @return EC_SUCCESS, or non-zero if module_id is not found.
 */
int gpio_power_down_module(enum module_id id);
#endif

/*
 * Check if signal is a valid GPIO signal, and not IO expander (enum
 * ioex_signal) or eSPI virtual wire (enum espi_vw_signal).
 *
 * @param signal	GPIO or IOEX or VW signal
 * @return		1 if signal is GPIO else return 0
 */
int signal_is_gpio(int signal);

#endif  /* __CROS_EC_GPIO_H */
