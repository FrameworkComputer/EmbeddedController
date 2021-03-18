/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_GPIO_MAP_H
#define __ZEPHYR_GPIO_MAP_H

#include <devicetree.h>
#include <gpio_signal.h>

#define GPIO_AC_PRESENT			NAMED_GPIO(acok_od)
#define GPIO_BOARD_VERSION1		NAMED_GPIO(brd_id0)
#define GPIO_BOARD_VERSION2		NAMED_GPIO(brd_id1)
#define GPIO_BOARD_VERSION3		NAMED_GPIO(brd_id2)
#define GPIO_DA9313_GPIO0		NAMED_GPIO(da9313_gpio0)
#define GPIO_ENTERING_RW		NAMED_GPIO(ec_entering_rw)
#define GPIO_LID_OPEN			NAMED_GPIO(lid_open_ec)
#define GPIO_POWER_BUTTON_L		NAMED_GPIO(ec_pwr_btn_odl)
#define GPIO_SKU_ID0			NAMED_GPIO(sku_id0)
#define GPIO_SKU_ID1			NAMED_GPIO(sku_id1)
#define GPIO_SKU_ID2			NAMED_GPIO(sku_id2)
#define GPIO_SWITCHCAP_ON		NAMED_GPIO(switchcap_on)
#define GPIO_SWITCHCAP_ON_L		NAMED_GPIO(switchcap_on)
#define GPIO_SWITCHCAP_PG_INT_L		NAMED_GPIO(da9313_gpio0)
#define GPIO_WP_L			NAMED_GPIO(ec_wp_odl)

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
#define EC_CROS_GPIO_INTERRUPTS                                           \
	GPIO_INT(GPIO_AC_PRESENT, GPIO_INT_EDGE_BOTH, extpower_interrupt) \
	GPIO_INT(GPIO_LID_OPEN, GPIO_INT_EDGE_BOTH, lid_interrupt)        \
	GPIO_INT(GPIO_POWER_BUTTON_L, GPIO_INT_EDGE_BOTH,                 \
		 power_button_interrupt)                                  \
	GPIO_INT(GPIO_SWITCHCAP_PG_INT_L, GPIO_INT_FALLING, ln9310_interrupt)

#endif /* __ZEPHYR_GPIO_MAP_H */
