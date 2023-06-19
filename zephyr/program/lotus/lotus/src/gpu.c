
/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ADC for checking BOARD ID.
 */

#include "adc.h"
#include "chipset.h"
#include "customized_shared_memory.h"
#include "gpio/gpio_int.h"
#include "gpio.h"
#include "gpu.h"
#include "board_adc.h"
#include "board_function.h"
#include "console.h"
#include "driver/temp_sensor/f75303.h"
#include "system.h"
#include "hooks.h"
#include "i2c.h"
#include "flash_storage.h"
#include "extpower.h"

LOG_MODULE_REGISTER(gpu, LOG_LEVEL_INF);

#define VALID_BOARDID(ID1, ID0) ((ID1 << 8) + ID0)
#define GPU_F75303_I2C_ADDR_FLAGS 0x4D

static int module_present;
bool gpu_present(void)
{
	return module_present;
}

static void update_gpu_ac_power_state(void)
{
	if (extpower_is_present() && module_present) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio02_ec), 1);
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio02_ec), 0);
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, update_gpu_ac_power_state, HOOK_PRIO_DEFAULT);

void check_gpu_module(void)
{
	int gpu_id_0 = get_hardware_id(ADC_GPU_BOARD_ID_0);
	int gpu_id_1 = get_hardware_id(ADC_GPU_BOARD_ID_1);
	int switch_status = 0;

	if (board_get_version() >= BOARD_VERSION_7) {
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_beam_open));
		switch_status = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_f_beam_open_l));
	} else {
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_chassis_open));
		switch_status = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_chassis_open_l));
	}

	switch (VALID_BOARDID(gpu_id_1, gpu_id_0)) {
	case VALID_BOARDID(BOARD_VERSION_12, BOARD_VERSION_12):
		LOG_INF("Detected dual interposer device");
		module_present = 1;
		break;
	case VALID_BOARDID(BOARD_VERSION_11, BOARD_VERSION_15):
	case VALID_BOARDID(BOARD_VERSION_13, BOARD_VERSION_15):
		LOG_INF("Detected single interposer device");
		module_present = 1;
		break;

	default:
		LOG_INF("No gpu module detected %d %d", gpu_id_0, gpu_id_1);
		/* Framework TODO remove for DVT, for now force on  unless feature is enabled */
		module_present = 0;
	break;
	}

	/* The chassis or f_beam is opened, turn off the power */
	if (!switch_status)
		module_present = 0;

	if (module_present) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_3v_5v_en), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_vsys_vadp_en), 1);
		if (board_get_version() >= BOARD_VERSION_7)
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd_gpu_sel), 0);
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_CONTROL) |= GPU_PRESENT;
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_3v_5v_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_vsys_vadp_en), 0);
		if (board_get_version() >= BOARD_VERSION_7)
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd_gpu_sel), 1);
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_CONTROL) &= GPU_PRESENT;
	}
	update_gpu_ac_power_state();
}
DECLARE_DEFERRED(check_gpu_module);
DECLARE_HOOK(HOOK_INIT, check_gpu_module, HOOK_PRIO_INIT_ADC + 1);


__override void project_chassis_function(enum gpio_signal signal)
{
	int open_state = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_chassis_open_l));

	/* The dGPU SW is SW3 at DVT phase */
	if (board_get_version() >= BOARD_VERSION_7)
		return;

	if (!open_state) {
		/* Make sure the module is off as fast as possible! */
		LOG_INF("Powering off GPU");
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio02_ec), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_vsys_vadp_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_3v_5v_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw), 0);
		module_present = 0;
	} else {
		hook_call_deferred(&check_gpu_module_data, 50*MSEC);
	}
}

void beam_open_interrupt(enum gpio_signal signal)
{
	int open_state = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_f_beam_open_l));

	/* The dGPU SW is SW4 at DVT phase */
	if (board_get_version() < BOARD_VERSION_7)
		return;

	if (!open_state) {
		/* Make sure the module is off as fast as possible! */
		LOG_INF("Powering off GPU");
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio02_ec), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_vsys_vadp_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_3v_5v_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw), 0);
		module_present = 0;
	} else {
		hook_call_deferred(&check_gpu_module_data, 50*MSEC);
	}
}

void gpu_smart_access_graphic(void);
DECLARE_DEFERRED(gpu_smart_access_graphic);

void gpu_smart_access_graphic(void)
{
	uint8_t gpu_status = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_CONTROL);

	/**
	 * Host updated the shared memory to control the mux,
	 * after switching the mux, clear the shared memory BIT(0) and BIT(1).
	 */
	if ((gpu_status & 0x03) == SET_GPU_MUX) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw), 1);
		gpu_status &= 0xFC;
		gpu_status |= GPU_MUX;
	}

	if ((gpu_status & 0x03) == SET_APU_MUX) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw), 0);
		gpu_status &= 0xFC;
		gpu_status &= ~GPU_MUX;
	}

	/**
	 * Host updated the shared memory to reset the edp,
	 * after controlling the reset pin, clear the shared memory BIT(4) and BIT(5).
	 */
	if ((gpu_status & 0x30) == ASSERTED_EDP_RESET) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_reset), 0);
		gpu_status &= 0xCF;
	}

	if ((gpu_status & 0x30) == DEASSERTED_EDP_RESET) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_reset), 1);
		gpu_status &= 0xCF;
	}

	*host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_CONTROL) = gpu_status;

	/* Polling to check the shared memory of the GPU */
	if (chipset_in_state(CHIPSET_STATE_ON))
		hook_call_deferred(&gpu_smart_access_graphic_data, 10 * MSEC);
}


static void start_smart_access_graphic(void)
{
	/* Check GPU is present then polling the namespace to do the smart access graphic */
	if (gpu_present())
		hook_call_deferred(&gpu_smart_access_graphic_data, 10 * MSEC);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, start_smart_access_graphic, HOOK_PRIO_DEFAULT);

static void f75303_disable_alert_mask(void)
{
	if (gpu_present())
		i2c_write8(I2C_PORT_GPU0, GPU_F75303_I2C_ADDR_FLAGS,
			F75303_ALERT_CHANNEL_MASK, (F75303_DP2_MASK | F75303_DP1_MASK |
			F75303_LOCAL_MASK));

}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, f75303_disable_alert_mask, HOOK_PRIO_DEFAULT);
