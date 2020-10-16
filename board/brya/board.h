/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Brya board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Baseboard features */
#include "baseboard.h"

#ifndef __ASSEMBLER__

#include "gpio_signal.h"	/* must precede gpio.h */

#endif /* !__ASSEMBLER__ */

/*
 * Disable features enabled by default.
 */
#undef CONFIG_ADC
#undef CONFIG_LID_SWITCH
#undef CONFIG_HIBERNATE
#undef CONFIG_SPI_FLASH
#undef CONFIG_SWITCH

#define GPIO_WP_L		GPIO_FAKE_IRQ_00
#define GPIO_ENTERING_RW	GPIO_FAKE_OUT_01

#endif /* __CROS_EC_BOARD_H */
