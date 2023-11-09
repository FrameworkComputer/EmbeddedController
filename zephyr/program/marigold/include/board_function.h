/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_FUNCTION_H
#define __CROS_EC_BOARD_FUNCTION_H

enum function_type {
	/* type for function switch */
	TYPE_MEMMAP	= 0,
	TYPE_BBRAM	= 1,
	TYPE_FLASH	= 2,
};

/* set bios function bit to bbram when ACPI_DRIVER_READY */
void bios_function_detect(void);

/* return BIOS setup AC attach bit status */
int ac_boot_status(void);

/* for host cmd clear count */
int chassis_cmd_clear(int type);

__override_proto void project_chassis_function(enum gpio_signal signal);

uint32_t get_system_percentage(void);

#endif	/* __CROS_EC_BOARD_FUNCTION_H */
