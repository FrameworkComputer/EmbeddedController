/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_GPIO_CHIP_H
#define __CROS_EC_GPIO_CHIP_H

struct npcx_wui {
	uint8_t table : 2;
	uint8_t group : 3;
	uint8_t bit   : 3;
};

/* Macros to initialize the MIWU mapping table. */
#define NPCX_WUI_GPIO_PIN(port, index) NPCX_WUI_GPIO_##port##_##index
#define WUI(tbl, grp, idx) ((struct npcx_wui) { .table = tbl, .group = grp, \
						.bit = idx })
#define WUI_INT(tbl, grp)  WUI(tbl, grp, 0)
#define WUI_NONE ((struct npcx_wui) { .table = MIWU_TABLE_COUNT, .group = 0, \
				      .bit = 0 })

/* Macros to initialize the alternative and low voltage mapping table. */
#define NPCX_GPIO_NONE ((struct npcx_gpio) {.port = 0, .bit = 0, .valid = 0})
#define NPCX_GPIO(grp, pin) ((struct npcx_gpio) {.port = GPIO_PORT_##grp, \
							.bit = pin, .valid = 1})

#define NPCX_ALT(grp, pin) ((struct npcx_alt) {.group = ALT_GROUP_##grp, \
			.bit = NPCX_DEVALT##grp##_##pin, .inverted = 0 })
#define NPCX_ALT_INV(grp, pin) ((struct npcx_alt) {.group = ALT_GROUP_##grp, \
			.bit = NPCX_DEVALT##grp##_##pin, .inverted = 1 })
#define ALT(port, index, _alt) { .gpio = NPCX_GPIO(port, index), \
			.alt = (_alt) },

#define NPCX_LVOL_CTRL_ITEMS(ctrl) { NPCX_LVOL_CTRL_##ctrl##_0, \
				     NPCX_LVOL_CTRL_##ctrl##_1, \
				     NPCX_LVOL_CTRL_##ctrl##_2, \
				     NPCX_LVOL_CTRL_##ctrl##_3, \
				     NPCX_LVOL_CTRL_##ctrl##_4, \
				     NPCX_LVOL_CTRL_##ctrl##_5, \
				     NPCX_LVOL_CTRL_##ctrl##_6, \
				     NPCX_LVOL_CTRL_##ctrl##_7, }

/**
 * Switch NPCX UART pins back to normal GPIOs.
 */
void npcx_uart2gpio(void);

/**
 * Switch NPCX UART pins to UART mode (depending on the currently selected
 * pad, see uart.c).
 */
void npcx_gpio2uart(void);

/* Set input buffer of all 1.8v i2c ports. */
void gpio_enable_1p8v_i2c_wake_up_input(int enable);

void gpio_interrupt(struct npcx_wui wui_int);

/*
 * Include the MIWU, alternative and low-Voltage macro functions for GPIOs
 * depends on Nuvoton chip series.
 */
#if defined(CHIP_FAMILY_NPCX5)
#include "gpio_chip-npcx5.h"
#elif defined(CHIP_FAMILY_NPCX7)
#include "gpio_chip-npcx7.h"
#elif defined(CHIP_FAMILY_NPCX9)
#include "gpio_chip-npcx9.h"
#else
#error "Unsupported chip family"
#endif

#endif /* __CROS_EC_GPIO_CHIP_H */
