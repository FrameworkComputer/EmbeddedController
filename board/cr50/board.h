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

/* How do I identify nonexistant GPIOs? */
#define DUMMY_GPIO_BANK -1

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

#include "gpio_signal.h"

#endif /* !__ASSEMBLER__ */

#endif /* __BOARD_H */
