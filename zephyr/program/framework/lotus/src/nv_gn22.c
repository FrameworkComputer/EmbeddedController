/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NV GN22 temperature sensor module for Chrome EC */

#include "board_host_command.h"
#include "customized_shared_memory.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpu.h"
#include "gpu_configuration.h"
#include "hooks.h"
#include "i2c.h"
#include "lotus/nv_gn22.h"
#include "lotus/gpu_temp.h"
#include "power.h"
#include "temp_sensor/temp_sensor.h"
#include "util.h"


#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_THERMAL, format, ##args)

/* GPU I2C address */
#define GPU_ADDR_FLAGS 0x004F

#define NV_GPU_TEMPERATURE_OFFSET 0x00

int get_nv_gpu_temp(int idx, int *temp)
{
	int reg;
	int rv;

	/*
	 * if not detect GPU should not send I2C.
	 */
	if (!gpu_present() || !gpu_power_enable()) {
		*temp = C_TO_K(0);
		return EC_ERROR_NOT_POWERED;
	}

	rv = i2c_read8(I2C_PORT_GPU0, GPU_ADDR_FLAGS,
					NV_GPU_TEMPERATURE_OFFSET, &reg);

	if (rv) {
		CPRINTS("read nv GPU Temperature fail");
		*temp = C_TO_K(0);
		return rv;
	}

	*temp = C_TO_K(reg);

	return EC_SUCCESS;
}
