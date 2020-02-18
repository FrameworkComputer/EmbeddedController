/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* R19ME4070 temperature sensor module for Chrome EC */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "amd_r19me4070.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/* GPU I2C address */
#define GPU_ADDR_FLAGS                  0x82

/*
 * Tell SMBus slave which register to read before GPU read
 * temperature, call it "GPU INIT".
 */
#define GPU_INIT_OFFSET                 0x01
#define GPU_TEMPERATURE_OFFSET          0x03
#define GPU_INIT_WRITE_VALUE            0x0F01665A

static int initialized;

static int read_gpu_temp(int *temp)
{
	return i2c_read32(I2C_PORT_GPU, GPU_ADDR_FLAGS, GPU_TEMPERATURE_OFFSET,
			  temp);
}

static void gpu_init_temp_sensor(void)
{
	int rv;

	rv = i2c_write32(I2C_PORT_GPU, GPU_ADDR_FLAGS, GPU_INIT_OFFSET,
			  GPU_INIT_WRITE_VALUE);
	if (rv == EC_SUCCESS) {
		initialized = 1;
		return;
	}
	CPRINTS("init GPU fail");
}
DECLARE_HOOK(HOOK_INIT, gpu_init_temp_sensor, HOOK_PRIO_INIT_I2C + 1);

/* INIT GPU first before read the GPU's die tmeperature. */
int get_temp_R19ME4070(int idx, int *temp_ptr)
{
	int reg, rv;

	/* if no INIT GPU, must init it first and wait 1 sec. */
	if (!initialized) {
		gpu_init_temp_sensor();
		return EC_ERROR_BUSY;
	}
	rv = read_gpu_temp(&reg);
	if (rv) {
		CPRINTS("read GPU Temperature fail");
		return rv;
	}
	/*
	 * The register is four bytes, bit[17:9] represents the GPU temperature.
	 * 0x000 : 0	ﾟC
	 * 0x001 : 1	ﾟC
	 * 0x002 : 2	ﾟC
	 * ...
	 * 0x1FF : 511	ﾟC
	 */
	*temp_ptr = C_TO_K((reg >> 9) & (0x1ff));
	return EC_SUCCESS;
}
