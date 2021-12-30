/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_GPIO_MAP_H
#define __ZEPHYR_GPIO_MAP_H

#include <devicetree.h>
#include <gpio_signal.h>

/*
 * Set EC_CROS_GPIO_INTERRUPTS to a space-separated list of GPIO_INT items.
 *
 * Each GPIO_INT requires three parameters:
 *   gpio_signal - The enum gpio_signal for the interrupt gpio
 *   interrupt_flags - The interrupt-related flags (e.g. GPIO_INT_EDGE_BOTH)
 *   handler - The platform/ec interrupt handler.
 *
 * Ensure that this files includes all necessary headers to declare all
 * referenced handler functions.
 *
 * For example, one could use the follow definition:
 * #define EC_CROS_GPIO_INTERRUPTS \
 *   GPIO_INT(NAMED_GPIO(h1_ec_pwr_btn_odl), GPIO_INT_EDGE_BOTH, button_print)
 */
/* Helper macros for generating CROS_EC_GPIO_INTERRUPTS */

#ifdef CONFIG_PLATFORM_EC_POWERSEQ
#define POWER_SIGNAL_INT(gpio, edge) \
	GPIO_INT(gpio, edge, power_signal_interrupt)
#define AP_PROCHOT_INT(gpio, edge) \
	GPIO_INT(gpio, edge, throttle_ap_prochot_input_interrupt)
#else
#define POWER_SIGNAL_INT(gpio, edge)
#define AP_PROCHOT_INT(gpio, edge)
#endif

#ifdef CONFIG_PLATFORM_EC_POWER_BUTTON
	#define PWRBTN_INT()        GPIO_INT(GPIO_POWER_BUTTON_L,              \
					     GPIO_INT_EDGE_BOTH,               \
					     power_button_interrupt)
#else
	#define PWRBTN_INT()
#endif

#ifdef CONFIG_PLATFORM_EC_LID_SWITCH
	#define LID_INT()       GPIO_INT(GPIO_LID_OPEN, \
				GPIO_INT_EDGE_BOTH,     \
				lid_interrupt)
#else
	#define LID_INT()
#endif

#ifdef CONFIG_PLATFORM_EC_VOLUME_BUTTONS
	#define VOLBTN_INT(pin)     GPIO_INT(pin,                             \
					     GPIO_INT_EDGE_BOTH,              \
					     button_interrupt)
#else
	#define VOLBTN_INT(pin)
#endif

#ifdef CONFIG_PLATFORM_EC_USBC
	#define USBC_INT(pin, port)	GPIO_INT(pin,			      \
						 GPIO_INT_EDGE_FALLING,	      \
						 usb_c ## port ## _interrupt)
#else
	#define USBC_INT(pin, port)
#endif


#define EC_CROS_GPIO_INTERRUPTS                                         \
	LID_INT()							\
	PWRBTN_INT()							\
	VOLBTN_INT(GPIO_VOLUME_DOWN_L)					\
	VOLBTN_INT(GPIO_VOLUME_UP_L)					\
	POWER_SIGNAL_INT(GPIO_SLP_SUS_L, GPIO_INT_EDGE_BOTH)            \
	POWER_SIGNAL_INT(GPIO_PG_EC_DSW_PWROK, GPIO_INT_EDGE_BOTH)      \
	POWER_SIGNAL_INT(GPIO_PG_EC_RSMRST_ODL, GPIO_INT_EDGE_BOTH)     \
	POWER_SIGNAL_INT(GPIO_PG_EC_ALL_SYS_PWRGD, GPIO_INT_EDGE_BOTH)  \
	AP_PROCHOT_INT(GPIO_EC_PROCHOT_ODL, GPIO_INT_EDGE_BOTH)		\
	USBC_INT(GPIO_USB_C0_PD_INT_ODL, 0)				\
	USBC_INT(GPIO_USB_C1_PD_INT_ODL, 1)
#endif /* __ZEPHYR_GPIO_MAP_H */
