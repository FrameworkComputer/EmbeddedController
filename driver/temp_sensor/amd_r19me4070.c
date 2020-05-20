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
#include "power.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/* GPU I2C address */
#define GPU_ADDR_FLAGS                  0x0041

#define GPU_INIT_OFFSET                 0x01
#define GPU_TEMPERATURE_OFFSET          0x03

static int initialized;
/*
 * Tell SMBus we want to read 4 Byte from register offset(0x01665A)
 */
static const uint8_t gpu_init_write_value[5] = {
	0x04, 0x0F, 0x01, 0x66, 0x5A,
};

static void gpu_init_temp_sensor(void)
{
	int rv;
	rv = i2c_write_block(I2C_PORT_GPU, GPU_ADDR_FLAGS, GPU_INIT_OFFSET,
			  gpu_init_write_value,
			  ARRAY_SIZE(gpu_init_write_value));
	if (rv == EC_SUCCESS) {
		initialized = 1;
		return;
	}
	CPRINTS("init GPU fail");
}

/* INIT GPU first before read the GPU's die tmeperature. */
int get_temp_R19ME4070(int idx, int *temp_ptr)
{
	uint8_t reg[5];
	int rv;

	/*
	 * We shouldn't read the GPU temperature when the state
	 * is not in S0, because GPU is enabled in S0.
	 */
	if ((power_get_state()) != POWER_S0)
		return EC_ERROR_BUSY;
	/* if no INIT GPU, must init it first and wait 1 sec. */
	if (!initialized) {
		gpu_init_temp_sensor();
		return EC_ERROR_BUSY;
	}
	rv = i2c_read_block(I2C_PORT_GPU, GPU_ADDR_FLAGS,
			GPU_TEMPERATURE_OFFSET, reg, ARRAY_SIZE(reg));
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
	 * -------------------------------
	 * reg[4] = bit0  - bit7
	 * reg[3] = bit8  - bit15
	 * reg[2] = bit16 - bit23
	 * reg[1] = bit24 - bit31
	 * reg[0] = 0x04
	 */
	*temp_ptr = C_TO_K(reg[3] >> 1);
	return EC_SUCCESS;
}
