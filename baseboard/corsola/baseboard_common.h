/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Corsola baseboard-specific onfiguration common to ECOS and Zephyr */

#ifndef __CROS_EC_BASEBOARD_COMMON_H
#define __CROS_EC_BASEBOARD_COMMON_H

/* GPIO name remapping */
#define GPIO_EN_HDMI_PWR        GPIO_EC_X_GPIO1
#define GPIO_USB_C1_FRS_EN      GPIO_EC_X_GPIO1
#define GPIO_USB_C1_PPC_INT_ODL GPIO_X_EC_GPIO2
#define GPIO_PS185_EC_DP_HPD    GPIO_X_EC_GPIO2
#define GPIO_USB_C1_DP_IN_HPD   GPIO_EC_X_GPIO3
#define GPIO_PS185_PWRDN_ODL    GPIO_EC_X_GPIO3

#ifndef __ASSEMBLER__

#include "gpio_signal.h"

enum board_sub_board {
	SUB_BOARD_NONE = -1,
	SUB_BOARD_TYPEC,
	SUB_BOARD_HDMI,
	SUB_BOARD_COUNT,
};

/**
 * board_get_version() - Get the board version
 *
 * Read the ADC to obtain the board version
 *
 * @return board version in the range 0 to 14 inclusive
 */
int board_get_version(void);

void ppc_interrupt(enum gpio_signal signal);
void bc12_interrupt(enum gpio_signal signal);
void x_ec_interrupt(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BASEBOARD_COMMON_H */
