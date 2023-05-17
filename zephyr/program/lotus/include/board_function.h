/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_FUNCTION_H
#define __CROS_EC_BOARD_FUNCTION_H

enum function_type {
	/* type for function switch */
	TYPE_NAME	= 0,
	TYPE_BBRAM	= 1,
	TYPE_FLASH	= 2,
};

void bios_function_detect(void);
int ac_boot_status(void);

#endif	/* __CROS_EC_BOARD_FUNCTION_H */
