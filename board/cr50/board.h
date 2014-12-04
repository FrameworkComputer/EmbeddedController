/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_H
#define __BOARD_H

/* Features that we don't want just yet */
#undef CONFIG_CMD_LID_ANGLE
#undef CONFIG_CMD_POWERINDEBUG
#undef CONFIG_DMA_DEFAULT_HANDLERS
#undef CONFIG_FLASH
#undef CONFIG_FMAP
#undef CONFIG_HIBERNATE
#undef CONFIG_LID_SWITCH

/* Need space to store the pin muxing configuration */
#define CONFIG_GPIO_LARGE_ALT_INFO

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

#include "gpio_signal.h"

/* user button interrupt handler */
void button_event(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
