/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* AMD R23M temperature sensor module for Chrome EC */

#include "board_host_command.h"
#include "customized_shared_memory.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpu.h"
#include "hooks.h"
#include "i2c.h"
#include "lotus/amd_r23m.h"
#include "lotus/gpu_temp.h"
#include "power.h"
#include "temp_sensor/temp_sensor.h"
#include "util.h"


#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_THERMAL, format, ##args)

/* GPU I2C address */
#define GPU_ADDR_FLAGS 0x0040

#define GPU_INIT_OFFSET 0x01
#define GPU_TEMPERATURE_OFFSET 0x03

static int initialized;
/*
 * Tell SMBus we want to read 4 Byte from register offset(0x01665A)
 */
static uint8_t gpu_init_write_value[5] = {
	0x04, 0x0F, 0x01, 0x66, 0x93,
};

static int gpu_init_temp_sensor(void)
{
	int rv;

	rv = i2c_write_block(I2C_PORT_GPU0, GPU_ADDR_FLAGS, GPU_INIT_OFFSET,
			     gpu_init_write_value,
			     ARRAY_SIZE(gpu_init_write_value));

	if (rv == EC_SUCCESS) {
		initialized = 1;
		return rv;
	}
	CPRINTS("init GPU fail: %d", rv);

	return rv;
}

/* INIT GPU first before read the GPU's die tmeperature. */
int get_amd_gpu_temp(int idx, int *temp)
{
	uint8_t reg[5];
	int rv;


	/*
	 * if not detect GPU should not send I2C.
	 */
	if (!gpu_present() || !gpu_power_enable()) {
		*temp = C_TO_K(0);
		initialized = 0;
		return EC_ERROR_NOT_POWERED;
	}

	if (!initialized) {
		rv = gpu_init_temp_sensor();
		*temp = C_TO_K(0);
		return rv;
	}

	rv = i2c_read_block(I2C_PORT_GPU0, GPU_ADDR_FLAGS,
				GPU_TEMPERATURE_OFFSET, reg, ARRAY_SIZE(reg));

	if (rv) {
		CPRINTS("read amd GPU Temperature fail");
		*temp = C_TO_K(0);
		return rv;
	}
	/*
	 * The register is four bytes, bit[17:9] represents the GPU temperature.
	 * 0x000 : 0	ﾟC
	 * 0x001 : 1	ﾟC
	 * 0x002 : 2	ﾟC
	 * ...
	 * 0x1FF : 511	ﾟC
	 * -------------------------------
	 * reg[4] = bit0  - bit7
	 * reg[3] = bit8  - bit15
	 * reg[2] = bit16 - bit23
	 * reg[1] = bit24 - bit31
	 * reg[0] = 0x04
	 */
	*temp = C_TO_K(reg[3] >> 1);

	return EC_SUCCESS;
}

void reset_gpu(void)
{
	initialized = 0;
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, reset_gpu, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, reset_gpu, HOOK_PRIO_DEFAULT);
