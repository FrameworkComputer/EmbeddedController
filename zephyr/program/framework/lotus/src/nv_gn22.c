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

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_THERMAL, format, ##args)

/* GPU I2C address */
#define GPU_ADDR_FLAGS 0x004F

#define NV_GPU_TEMPERATURE_OFFSET 0x00

#define DISPLAY_NODE DT_NODELABEL(pwm6)
PINCTRL_DT_DEFINE(DISPLAY_NODE);

static enum dds_pin_mode current_dds_pin_mode = PIN_PWM;

void nv_gn22_set_dds_pin_mode(enum dds_pin_mode mode)
{
	const struct pinctrl_dev_config *pcfg = PINCTRL_DT_DEV_CONFIG_GET(DISPLAY_NODE);

	if (current_dds_pin_mode == mode)
		return;

	switch (mode) {
	case PIN_PWM:
		CPRINTS("DDS gpio set pwm");
		pinctrl_apply_state(pcfg, PINCTRL_STATE_DEFAULT);
		break;
	case PIN_INPUT:
		CPRINTS("DDS gpio set input");
		pinctrl_apply_state(pcfg, PINCTRL_STATE_SLEEP);
		break;
	default:
		CPRINTS("Invalid DDS pin mode: defaulting to input");
		pinctrl_apply_state(pcfg, PINCTRL_STATE_SLEEP);
		mode = PIN_INPUT;
		break;
	}

	current_dds_pin_mode = mode;
}

void nv_gn22_configure_gpio(void)
{
	static uint8_t last_dds_pwm_switch = 0xFF;
	uint8_t gpu_vendor = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_TYPE);
	uint8_t dds_pwm_switch = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_DDS_PWM_SOURCING);

	if (gpu_vendor != GPU_NV_GN22) {
		return;
	}

	if (dds_pwm_switch == last_dds_pwm_switch)
		return;

	if ((dds_pwm_switch & 0x01) == DDS_PWM_EC_CONTROL) {
		nv_gn22_set_dds_pin_mode(PIN_PWM);
	} else {
		nv_gn22_set_dds_pin_mode(PIN_INPUT);
	}

	last_dds_pwm_switch = dds_pwm_switch;
}

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

static int nv_gn22_dds_mode_cmd(int argc, const char **argv)
{
	if (argc == 2 && !strncmp(argv[1], "get", 3)) {
		ccprintf("Current DDS pin mode: %s\n",
			current_dds_pin_mode == PIN_PWM ? "(PWM)" : "(INPUT)");
		return EC_SUCCESS;
	}

	if (argc == 3 && !strncmp(argv[1], "set", 3)) {
		if (!strcasecmp(argv[2], "pwm")) {
			nv_gn22_set_dds_pin_mode(PIN_PWM);
		} else if (!strcasecmp(argv[2], "input")) {
			nv_gn22_set_dds_pin_mode(PIN_INPUT);
		}
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(dds, nv_gn22_dds_mode_cmd,
			"get | set pwm|input",
			"Get or set DDS display pin mode");
