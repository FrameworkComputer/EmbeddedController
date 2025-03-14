/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPU temperature sensor module for Chrome EC */

#include "board_host_command.h"
#include "customized_shared_memory.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpu.h"
#include "gpu_configuration.h"
#include "hooks.h"
#include "i2c.h"
#include "lotus/amd_r23m.h"
#include "lotus/nv_gn22.h"
#include "lotus/gpu_temp.h"
#include "power.h"
#include "temp_sensor/temp_sensor.h"
#include "util.h"


#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_THERMAL, format, ##args)

static int temps;

int gpu_get_val_k(int idx, int *temp)
{
	if (!chipset_in_state(CHIPSET_STATE_ON) || !gpu_is_working()) {
		return EC_ERROR_NOT_POWERED;
	}

	*temp = temps;

	return EC_SUCCESS;
}

int dgpu_delay(void)
{
	if (*host_get_memmap(EC_CUSTOMIZED_MEMMAP_SYSTEM_FLAGS) & ACPI_DRIVER_READY)
		return true;
	else
		return false;
}

void gpu_update_temperature(int idx)
{
	int temp = 0;
	int rv = EC_ERROR_UNKNOWN;
	uint8_t gpu_vendor;

	gpu_vendor = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_TYPE);

	/*
	 * We shouldn't read the GPU temperature when the state
	 * is not in S0, because GPU is enabled in S0.
	 */
	if (!chipset_in_state(CHIPSET_STATE_ON)) {
		temps = C_TO_K(0);
		return;
	}

	if (!dgpu_delay()) {
		return;
	}

	if (gpu_vendor == GPU_AMD_R23M) {
		rv = get_amd_gpu_temp(idx, &temp);
	} else if (gpu_vendor == GPU_NV_GN22) {
		rv = get_nv_gpu_temp(idx, &temp);
	}

	if (rv == EC_SUCCESS)
		temps = temp;
	else
		temps = C_TO_K(0);
}
