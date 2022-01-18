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
#define EC_CROS_GPIO_INTERRUPTS                                               \
	GPIO_INT(GPIO_AC_PRESENT, GPIO_INT_EDGE_BOTH, extpower_interrupt)     \
	GPIO_INT(GPIO_LID_OPEN, GPIO_INT_EDGE_BOTH, lid_interrupt)            \
	GPIO_INT(GPIO_WP_L, GPIO_INT_EDGE_BOTH, switch_interrupt)             \
	GPIO_INT(GPIO_POWER_BUTTON_L, GPIO_INT_EDGE_BOTH,                     \
		 power_button_interrupt)                                      \
	GPIO_INT(GPIO_VOLUME_DOWN_L, GPIO_INT_EDGE_BOTH, button_interrupt)    \
	GPIO_INT(GPIO_VOLUME_UP_L, GPIO_INT_EDGE_BOTH, button_interrupt)      \
	GPIO_INT(GPIO_AP_RST_L, GPIO_INT_EDGE_BOTH, chipset_ap_rst_interrupt) \
	GPIO_INT(GPIO_AP_SUSPEND, GPIO_INT_EDGE_BOTH, power_signal_interrupt) \
	GPIO_INT(GPIO_DEPRECATED_AP_RST_REQ, GPIO_INT_EDGE_BOTH,              \
		 power_signal_interrupt)                                      \
	GPIO_INT(GPIO_POWER_GOOD, GPIO_INT_EDGE_BOTH,                         \
		 chipset_power_good_interrupt)                                \
	GPIO_INT(GPIO_PS_HOLD, GPIO_INT_EDGE_BOTH, power_signal_interrupt)    \
	GPIO_INT(GPIO_WARM_RESET_L, GPIO_INT_EDGE_BOTH,                       \
		 chipset_warm_reset_interrupt)                                \
	GPIO_INT(GPIO_USB_C0_PD_INT_ODL, GPIO_INT_EDGE_FALLING,               \
		 tcpc_alert_event)                                            \
	GPIO_INT(GPIO_USB_C1_PD_INT_ODL, GPIO_INT_EDGE_FALLING,               \
		 tcpc_alert_event)                                            \
	GPIO_INT(GPIO_USB_C0_SWCTL_INT_ODL, GPIO_INT_EDGE_FALLING,            \
		 ppc_interrupt)                                               \
	GPIO_INT(GPIO_USB_C1_SWCTL_INT_ODL, GPIO_INT_EDGE_FALLING,            \
		 ppc_interrupt)                                               \
	GPIO_INT(GPIO_USB_C0_BC12_INT_L, GPIO_INT_EDGE_FALLING, usb0_evt)     \
	GPIO_INT(GPIO_USB_C1_BC12_INT_L, GPIO_INT_EDGE_FALLING, usb1_evt)     \
	GPIO_INT(GPIO_USB_A0_OC_ODL, GPIO_INT_EDGE_BOTH, usba_oc_interrupt)   \
	GPIO_INT(GPIO_ACCEL_GYRO_INT_L, GPIO_INT_EDGE_FALLING,                \
		 bmi160_interrupt)                                            \
	GPIO_INT(GPIO_TABLET_MODE_L, GPIO_INT_EDGE_BOTH, gmr_tablet_switch_isr)

#endif /* __ZEPHYR_GPIO_MAP_H */
