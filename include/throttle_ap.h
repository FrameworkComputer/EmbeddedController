/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common interface to throttle the AP */

#ifndef __CROS_EC_THROTTLE_AP_H
#define __CROS_EC_THROTTLE_AP_H

/**
 * Level of throttling desired.
 */
enum throttle_level {
	THROTTLE_OFF = 0,
	THROTTLE_ON,
};

/**
 * Types of throttling desired. These are independent.
 */
enum throttle_type {
	THROTTLE_SOFT = 0,			/* for example, host events */
	THROTTLE_HARD,				/* for example, PROCHOT */
	NUM_THROTTLE_TYPES
};

/**
 * Possible sources for CPU throttling requests.
 */
enum throttle_sources {
	THROTTLE_SRC_THERMAL = 0,
	THROTTLE_SRC_BAT_DISCHG_CURRENT,
	THROTTLE_SRC_BAT_VOLTAGE,
};

/**
 * PROCHOT detection GPIOs.  PROCHOT in assumed to be active high unless
 * CONFIG_CPU_PROCHOT_ACTIVE_LOW is enabled.
 * C10 input polarity is explicitly specified in the struct below.
 */
struct prochot_cfg {
	enum gpio_signal gpio_prochot_in;
#ifdef CONFIG_CPU_PROCHOT_GATE_ON_C10
	enum gpio_signal gpio_c10_in;
	bool c10_active_high;
#endif
};

/**
 * Enable/disable CPU throttling.
 *
 * This is a virtual "OR" operation. Any caller can enable CPU throttling of
 * any type, but all callers must agree in order to disable that type.
 *
 * @param level         Level of throttling desired
 * @param type          Type of throttling desired
 * @param source        Which task is requesting throttling
 */
#if defined(CONFIG_THROTTLE_AP) || \
	defined(CONFIG_THROTTLE_AP_ON_BAT_DISCHG_CURRENT) || \
	defined(CONFIG_THROTTLE_AP_ON_BAT_VOLTAGE)

void throttle_ap(enum throttle_level level,
		 enum throttle_type type,
		 enum throttle_sources source);

/**
 * Configure the GPIOs used to monitor the PROCHOT signal.
 *
 * @param cfg	GPIO configuration for the PROCHOT and optional C10
 *		signals.
 */
void throttle_ap_config_prochot(const struct prochot_cfg *cfg);

/**
 * Interrupt handler to monitor PROCHOT input to the EC. The PROCHOT signal
 * can be asserted by the AP or by other devices on the board, such as chargers
 * and voltage regulators.
 *
 * The board initialization is responsible for enabling the interrupt.
 *
 * @param signal    GPIO signal connected to PROCHOT input. The polarity of this
 *                  signal is active high unless CONFIG_CPU_PROCHOT_ACTIVE_LOW
 *                  is defined.
 */
void throttle_ap_prochot_input_interrupt(enum gpio_signal signal);

/**
 * Interrupt handler to monitor the C10 input to the EC. The C10 signal
 * can be asserted by the AP when entering an idle state. This interrupt
 * is configured for the edge indicating C10 is de-asserting (GPIO_INT_RISING
 * if the signal is active low, GPIO_INT_FALLING for an active high signal).
 *
 * The board initialization is responsible for enabling the interrupt.
 *
 * @param signal    GPIO signal connected to C10 input.
 */
void throttle_ap_c10_input_interrupt(enum gpio_signal signal);

#else
static inline void throttle_ap(enum throttle_level level,
			       enum throttle_type type,
			       enum throttle_sources source)
{}
#endif

#endif	/* __CROS_EC_THROTTLE_AP_H */
