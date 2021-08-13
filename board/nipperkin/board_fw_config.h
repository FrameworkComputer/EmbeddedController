/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _GUYBRUSH_BOARD_FW_CONFIG__H_
#define _GUYBRUSH_BOARD_FW_CONFIG__H_

/****************************************************************************
 * Guybrush CBI FW Configuration
 */

/*
 * USB Daughter Board (2 bits)
 */
#define FW_CONFIG_USB_DB_OFFSET			0
#define FW_CONFIG_USB_DB_WIDTH			2
#define FW_CONFIG_USB_DB_A1_PS8811_C1_PS8818	0

/*
 * Form Factor (1 bits)
 */
#define FW_CONFIG_FORM_FACTOR_OFFSET		2
#define FW_CONFIG_FORM_FACTOR_WIDTH		1
#define FW_CONFIG_FORM_FACTOR_CLAMSHELL		0
#define FW_CONFIG_FORM_FACTOR_CONVERTIBLE	1

/*
 * Keyboard Backlight (1 bit)
 */
#define FW_CONFIG_KBLIGHT_OFFSET		3
#define FW_CONFIG_KBLIGHT_WIDTH			1
#define FW_CONFIG_KBLIGHT_NO			0
#define FW_CONFIG_KBLIGHT_YES			1


#endif /* _GUYBRUSH_CBI_FW_CONFIG__H_ */
