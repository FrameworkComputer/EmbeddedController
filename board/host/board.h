/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Emulator board configuration */

#ifndef __BOARD_H
#define __BOARD_H

#define CONFIG_HOST_EMU
#define CONFIG_HOSTCMD
#define CONFIG_KEYBOARD_PROTOCOL_MKBP

enum gpio_signal {
	GPIO_EC_INT,

	GPIO_COUNT
};

#endif /* __BOARD_H */
