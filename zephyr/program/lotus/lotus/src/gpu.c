
/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ADC for checking BOARD ID.
 */

#include "adc.h"
#include "battery.h"
#include "chipset.h"
#include "customized_shared_memory.h"
#include "gpio/gpio_int.h"
#include "gpio.h"
#include "gpu.h"
#include "board_adc.h"
#include "board_function.h"
#include "console.h"
#include "driver/temp_sensor/f75303.h"
#include "extpower.h"
#include "flash_storage.h"
#include "hooks.h"
#include "i2c.h"
#include "system.h"
#include "thermal.h"

LOG_MODULE_REGISTER(gpu, LOG_LEVEL_DBG);

#define VALID_BOARDID(ID1, ID0) ((ID1 << 8) + ID0)
#define GPU_F75303_I2C_ADDR_FLAGS 0x4D

#define GPU_F75303_REG_LOCAL_ALERT   0x05
#define GPU_F75303_REG_REMOTE1_ALERT 0x07
#define GPU_F75303_REG_REMOTE2_ALERT 0x15

#define GPU_F75303_REG_REMOTE1_THERM 0x19
#define GPU_F75303_REG_REMOTE2_THERM 0x1A
#define GPU_F75303_REG_LOCAL_THERM   0x21

static int module_present;
static int module_fault;

bool gpu_present(void)
{
	return module_present;
}

bool gpu_power_enable(void)
{
	/* dgpu pwr enable pin will be high at s5 state*/
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return 0;
	else
		return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_dgpu_pwr_en));
}

bool gpu_module_fault(void)
{
	return module_fault;
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

/* After GPU detect, update the thermal configuration */
void update_thermal_configuration(void)
{
	if (gpu_present()) {
		thermal_params[2].temp_fan_max = C_TO_K(69); /* QTH1 */
		thermal_params[2].temp_fan_off = C_TO_K(48); /* QTH1 */
	} else {
		thermal_params[2].temp_fan_max = C_TO_K(62); /* QTH1 */
		thermal_params[2].temp_fan_off = C_TO_K(48); /* QTH1 */
	}
}
DECLARE_HOOK(HOOK_INIT, update_thermal_configuration, HOOK_PRIO_DEFAULT + 2);

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
		LOG_DBG("Detected dual interposer device");
		module_present = 1;
		break;
	case VALID_BOARDID(BOARD_VERSION_11, BOARD_VERSION_15):
	case VALID_BOARDID(BOARD_VERSION_13, BOARD_VERSION_15):
		LOG_DBG("Detected single interposer device");
		module_present = 1;
		break;
	case VALID_BOARDID(BOARD_VERSION_15, BOARD_VERSION_15):
		LOG_DBG("No gpu module detected %d %d", gpu_id_0, gpu_id_1);
		module_present = 0;
		break;
	default:
		LOG_DBG("GPU module Fault");
		module_present = 0;
		module_fault = 1;
	break;
	}

	/* The chassis or f_beam is opened, turn off the power */
	if (!switch_status)
		module_present = 0;

	if (module_present) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_3v_5v_en), 1);
		/* vsys_vadp_en should follow the syson to enable */
		if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_syson)))
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_vsys_en), 1);
		if (board_get_version() >= BOARD_VERSION_7)
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd_gpu_sel), 0);
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_CONTROL) |= GPU_PRESENT;
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_gpu_power_en));
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_3v_5v_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_vsys_en), 0);
		if (board_get_version() >= BOARD_VERSION_7)
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd_gpu_sel), 1);
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_CONTROL) &= GPU_PRESENT;
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_gpu_power_en));
	}
	update_gpu_ac_power_state();
	update_thermal_configuration();
}
DECLARE_DEFERRED(check_gpu_module);
DECLARE_HOOK(HOOK_INIT, check_gpu_module, HOOK_PRIO_INIT_ADC + 1);

void gpu_interposer_toggle_deferred(void)
{
	int rv;

	rv = board_cut_off_battery();
	if (rv == EC_RES_SUCCESS) {
		LOG_DBG("board cut off succeeded.");
		set_battery_in_cut_off();
	} else
		LOG_DBG("board cut off failed!");
}
DECLARE_DEFERRED(gpu_interposer_toggle_deferred);


__override void project_chassis_function(enum gpio_signal signal)
{
	int open_state = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_chassis_open_l));

	/* The dGPU SW is SW3 at DVT phase */
	if (board_get_version() >= BOARD_VERSION_7)
		return;

	if (!open_state) {
		/* Make sure the module is off as fast as possible! */
		LOG_DBG("Powering off GPU");
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio02_ec), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_vsys_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_3v_5v_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw), 0);
		module_present = 0;
		update_thermal_configuration();
	} else {
		hook_call_deferred(&check_gpu_module_data, 50*MSEC);
	}
}

void beam_open_interrupt(enum gpio_signal signal)
{
	int open_state = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_f_beam_open_l));
	static int gpu_interposer_toggle_count;
	static int cutoff;

	/* The dGPU SW is SW4 at DVT phase */
	if (board_get_version() < BOARD_VERSION_7)
		return;

	if (!open_state) {
		/* Make sure the module is off as fast as possible! */
		LOG_DBG("Powering off GPU");
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio02_ec), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_vsys_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_3v_5v_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw), 0);
		module_present = 0;
		update_thermal_configuration();

		if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
			gpu_interposer_toggle_count++;

			if (!cutoff && gpu_interposer_toggle_count >= 10) {
				hook_call_deferred(&gpu_interposer_toggle_deferred_data,
					100 * MSEC);
				cutoff = 1;
			}
		} else
			gpu_interposer_toggle_count = 0;
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
	if (!chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) ||
	    !chipset_in_state(CHIPSET_STATE_ANY_OFF))
		hook_call_deferred(&gpu_smart_access_graphic_data, 10 * MSEC);
}

static void start_smart_access_graphic(void)
{
	/* Check GPU is present then polling the namespace to do the smart access graphic */
	if (gpu_present())
		hook_call_deferred(&gpu_smart_access_graphic_data, 10 * MSEC);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, start_smart_access_graphic, HOOK_PRIO_DEFAULT);

static void reset_smart_access_graphic(void)
{
	/* smart access graphic default should be hybrid mode */
	if (chipset_in_state(CHIPSET_STATE_ON))
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, reset_smart_access_graphic, HOOK_PRIO_DEFAULT);

static void reset_mux_status(void)
{
	uint8_t gpu_status = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_CONTROL);

	/* When the system shutdown, the gpu mux needs to switch to iGPU */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw), 0);
	gpu_status &= 0xFC;
	gpu_status &= ~GPU_MUX;
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, reset_mux_status, HOOK_PRIO_DEFAULT);

static void gpu_board_f75303_initial(void)
{
	int idx, rv;
	uint8_t temp[6] = {105, 105, 105, 110, 110, 110};
	uint8_t reg_arr[6] = {
		GPU_F75303_REG_LOCAL_ALERT,
		GPU_F75303_REG_REMOTE1_ALERT,
		GPU_F75303_REG_REMOTE2_ALERT,
		GPU_F75303_REG_REMOTE1_THERM,
		GPU_F75303_REG_REMOTE2_THERM,
		GPU_F75303_REG_LOCAL_THERM,
	};

	if (gpu_present() && chipset_in_state(CHIPSET_STATE_ON)) {
		for (idx = 0; idx < sizeof(reg_arr); idx++) {
			rv = i2c_write8(I2C_PORT_GPU0, GPU_F75303_I2C_ADDR_FLAGS,
					reg_arr[idx], temp[idx]);

			if (rv != EC_SUCCESS)
				LOG_INF("gpu f75303 init reg 0x%02x failed", reg_arr[idx]);

			k_msleep(1);
		}
	}
}
DECLARE_DEFERRED(gpu_board_f75303_initial);

void gpu_power_enable_handler(void)
{
	/* we needs to re-initial the thermal sensor and gpu when gpu power enable */
	if (gpu_power_enable())
		hook_call_deferred(&gpu_board_f75303_initial_data, 500 * MSEC);

}
