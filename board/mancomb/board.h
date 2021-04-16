/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mancomb board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Baseboard features */
#include "baseboard.h"

/* LED */
#define CONFIG_LED_COMMON
#define CONFIG_LED_ONOFF_STATES

/* USB Type C and USB PD defines */

/* USB Type A Features */

/* BC 1.2 */

/* Volume Button feature */

/* Fan features */

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
