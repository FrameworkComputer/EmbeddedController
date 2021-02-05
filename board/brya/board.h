/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Brya board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Baseboard features */
#include "baseboard.h"

/*
 * Disable features enabled by default.
 */
#undef CONFIG_ADC
#undef CONFIG_LID_SWITCH
#undef CONFIG_HIBERNATE
#undef CONFIG_SPI_FLASH
#undef CONFIG_SWITCH

/* USB Type C and USB PD defines */
#define CONFIG_IO_EXPANDER_PORT_COUNT		2

#define GPIO_ENTERING_RW		GPIO_EC_ENTERING_RW
#define GPIO_WP_L			GPIO_EC_WP_ODL


#ifndef __ASSEMBLER__

#include "gpio_signal.h"	/* needed by registers.h */
#include "registers.h"

enum ioex_port {
	IOEX_C0_NCT38XX = 0,
	IOEX_C2_NCT38XX,
	IOEX_PORT_COUNT
};

enum battery_type {
	BATTERY_POWER_TECH,
	BATTERY_LGC011,
	BATTERY_TYPE_COUNT
};

/*
 * remove when we enable CONFIG_POWER_BUTTON
 */

void power_button_interrupt(enum gpio_signal signal);

/*
 * remove when we enable CONFIG_THROTTLE_AP
 */

void throttle_ap_prochot_input_interrupt(enum gpio_signal signal);

/*
 * remove when we enable CONFIG_EXTPOWER_GPIO
 */

void extpower_interrupt(enum gpio_signal signal);

/*
 * remove when we enable CONFIG_VOLUME_BUTTONS
 */

void button_interrupt(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
