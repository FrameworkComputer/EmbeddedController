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
static int temps[AMDR23M_COUNT];
/*
 * Tell SMBus we want to read 4 Byte from register offset(0x01665A)
 */
static uint8_t gpu_init_write_value[5] = {
	0x04, 0x0F, 0x01, 0x66, 0x93,
};

/**
 * Read block register from temp sensor.
 */
static int raw_writeblock(int sensor, int offset, uint8_t *data, int len)
{
	return i2c_write_block(amdr23m_sensors[sensor].i2c_port,
			 amdr23m_sensors[sensor].i2c_addr_flags, offset, data, len);
}

static int raw_readblock(int sensor, int offset, uint8_t *data, int len)
{
	return i2c_read_block(amdr23m_sensors[sensor].i2c_port,
			 amdr23m_sensors[sensor].i2c_addr_flags, offset, data, len);
}

static void gpu_init_temp_sensor(int idx)
{
	int rv;

	rv = raw_writeblock(idx, GPU_INIT_OFFSET, gpu_init_write_value,
				ARRAY_SIZE(gpu_init_write_value));

	if (rv == EC_SUCCESS) {
		initialized = 1;
		return;
	}
	CPRINTS("init GPU fail: %d", rv);
}

int amdr23m_get_val_k(int idx, int *temp)
{
	if (idx < 0 || AMDR23M_COUNT <= idx)
		return EC_ERROR_INVAL;

	*temp = temps[idx];
	return EC_SUCCESS;
}

int amd_dgpu_delay(void)
{
	if (*host_get_memmap(EC_CUSTOMIZED_MEMMAP_SYSTEM_FLAGS) & ACPI_DRIVER_READY)
		return true;
	else
		return false;
}

/* INIT GPU first before read the GPU's die tmeperature. */
void amdr23m_update_temperature(int idx)
{
	uint8_t reg[5];
	int rv;


	/*
	 * if not detect GPU should not send I2C.
	 */
	if (!gpu_present() || !gpu_power_enable()) {
		temps[idx] = C_TO_K(0);
		initialized = 0;
		return;
	}

	/*
	 * We shouldn't read the GPU temperature when the state
	 * is not in S0, because GPU is enabled in S0.
	 */
	if (!chipset_in_state(CHIPSET_STATE_ON)) {
		temps[idx] = C_TO_K(0);
		return;
	}

	if (!amd_dgpu_delay()) {
		return;
	}

	if (!initialized) {
		gpu_init_temp_sensor(idx);
		temps[idx] = C_TO_K(0);
		return;
	}

	rv = raw_readblock(idx, GPU_TEMPERATURE_OFFSET, reg, ARRAY_SIZE(reg));

	if (rv) {
		CPRINTS("read GPU Temperature fail");
		temps[idx] = C_TO_K(0);
		return;
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
	temps[idx] = C_TO_K(reg[3] >> 1);
}

void reset_gpu(void)
{
	initialized = 0;
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, reset_gpu, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, reset_gpu, HOOK_PRIO_DEFAULT);
