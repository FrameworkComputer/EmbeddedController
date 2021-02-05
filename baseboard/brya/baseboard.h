/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Brya baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/*
 * By default, enable all console messages excepted HC
 */
#define CC_DEFAULT     (CC_ALL & ~(BIT(CC_HOSTCMD)))

/* NPCX9 config */
#define NPCX9_PWM1_SEL    1  /* GPIO C2 is used as PWM1. */
/*
 * This defines which pads (GPIO10/11 or GPIO64/65) are connected to
 * the "UART1" (NPCX_UART_PORT0) controller when used for
 * CONSOLE_UART.
 */
#define NPCX_UART_MODULE2	1 /* 1:GPIO64/65 for UART1 */

#ifndef __ASSEMBLER__

#include "baseboard_usbc_config.h"

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
