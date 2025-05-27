/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"

#include <zephyr/drivers/syscon.h>

/* TODO(b/432659361): use Egis HAL defines once it is supported. */
#define ROM_ADDRESS 0x70000000

#define AOSMU_SECURE_CON 0x0C
#define AOSMU_RESET_VECTOR 0x10

#define SMU_SECURE_WARM_RST BIT(2)
#define SMU_SECURE_TARGET_BOOTLOADER BIT(16)

static const struct device *const syscon_dev =
	DEVICE_DT_GET(DT_NODELABEL(syscon));

void chip_enter_bootloader(uint8_t mode)
{
	uint32_t syscon;

	syscon_read_reg(syscon_dev, AOSMU_SECURE_CON, &syscon);
	syscon_write_reg(syscon_dev, AOSMU_RESET_VECTOR, 0x70000000);
	syscon_write_reg(syscon_dev, AOSMU_SECURE_CON,
			 syscon | SMU_SECURE_TARGET_BOOTLOADER);
	syscon_write_reg(syscon_dev, AOSMU_SECURE_CON,
			 syscon | SMU_SECURE_WARM_RST |
				 SMU_SECURE_TARGET_BOOTLOADER);
}
