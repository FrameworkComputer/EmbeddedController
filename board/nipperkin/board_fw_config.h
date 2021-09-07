/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _NIPPERKIN_BOARD_FW_CONFIG__H_
#define _NIPPERKIN_BOARD_FW_CONFIG__H_

/****************************************************************************
 * Nipperkin CBI FW Configuration
 */

/*
 * Keyboard Backlight (1 bit)
 */
#define FW_CONFIG_KBLIGHT_OFFSET		0
#define FW_CONFIG_KBLIGHT_WIDTH			1
#define FW_CONFIG_KBLIGHT_NO			0
#define FW_CONFIG_KBLIGHT_YES			1

/*
 * Bit 1 ~ 6 not related to EC function
 */

/*
 * Keyboard (1 bit)
 */
#define FW_CONFIG_KEYBOARD_OFFSET		7
#define FW_CONFIG_KEYBOARD_WIDTH		1
#define FW_CONFIG_KEYBOARD_PRIVACY_YES		0
#define FW_CONFIG_KEYBOARD_PRIVACY_NO		1

bool board_has_privacy_panel(void);

#endif /* _NIPPERKIN_BOARD_FW_CONFIG__H_ */
