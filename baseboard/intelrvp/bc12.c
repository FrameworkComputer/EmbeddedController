/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel BASEBOARD-RVP BC1.2 specific configuration */

#include "common.h"
#include "max14637.h"

/* BC1.2 chip Configuration */
#ifdef CONFIG_BC12_DETECT_MAX14637
const struct max14637_config_t max14637_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[TYPE_C_PORT_0] = {
		.chip_enable_pin = GPIO_USB_C0_BC12_VBUS_ON_ODL,
		.chg_det_pin = GPIO_USB_C0_BC12_CHG_DET_L,
		.flags = MAX14637_FLAGS_CHG_DET_ACTIVE_LOW,
	},
#ifdef HAS_TASK_PD_C1
	[TYPE_C_PORT_1] = {
		.chip_enable_pin = GPIO_USB_C1_BC12_VBUS_ON_ODL,
		.chg_det_pin = GPIO_USB_C1_BC12_CHG_DET_L,
		.flags = MAX14637_FLAGS_CHG_DET_ACTIVE_LOW,
	},
#endif /* HAS_TASK_PD_C1 */
};
BUILD_ASSERT(ARRAY_SIZE(max14637_config) == CONFIG_USB_PD_PORT_MAX_COUNT);
#endif /* CONFIG_BC12_DETECT_MAX14637 */
