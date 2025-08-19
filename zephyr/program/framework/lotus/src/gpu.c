
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
#include "extpower.h"
#include "gpio/gpio_int.h"
#include "gpio.h"
#include "gpu.h"
#include "board_adc.h"
#include "board_function.h"
#include "board_host_command.h"
#include "console.h"
#include "driver/temp_sensor/f75303.h"
#include "extpower.h"
#include "flash_storage.h"
#include "hooks.h"
#include "i2c.h"
#include "system.h"
#include "thermal.h"
#include "gpu_configuration.h"

LOG_MODULE_REGISTER(gpu, LOG_LEVEL_DBG);

#define VALID_BOARDID(ID1, ID0) ((ID1 << 8) + ID0)
static int fan_present;
static int module_present;
static int module_fault;
static int gpu_id_0;
static int gpu_id_1;
static int switch_status;

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

bool gpu_fan_board_present(void)
{
	return fan_present;
}

void update_gpu_ac_power_state(void)
{
	int level = extpower_is_present();

	/**
	 * The chagrer ACOK status will be off -> on -> off after the adapter is removed.
	 * We need to use the customized extpower_is_present to get the correct status.
	 */
	set_gpu_gpio(GPIO_FUNC_ACDC, level);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, update_gpu_ac_power_state, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, update_gpu_ac_power_state, HOOK_PRIO_DEFAULT);
DECLARE_DEFERRED(update_gpu_ac_power_state);

void update_gpu_ac_mode_deferred(int times)
{
	hook_call_deferred(&update_gpu_ac_power_state_data, times);
}

/* After GPU detect, update the thermal configuration */
void init_gpu_latch(void)
{
	if (board_get_version() >= BOARD_VERSION_7) {
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_beam_open));
	} else {
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_chassis_open));
	}
}
DECLARE_HOOK(HOOK_INIT, init_gpu_latch, HOOK_PRIO_DEFAULT + 2);

int get_gpu_latch(void)
{
	if (board_get_version() >= BOARD_VERSION_7) {
		return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_f_beam_open_l));
	} else {
		return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_chassis_open_l));
	}
}

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

void beam_function(void)
{
	static int cutoff;
	static int gpu_interposer_toggle_count;

	if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		gpu_interposer_toggle_count++;
		if (!cutoff && gpu_interposer_toggle_count >= 10) {
			hook_call_deferred(&gpu_interposer_toggle_deferred_data,
				100 * MSEC);
			cutoff = 1;
		}
	} else
		gpu_interposer_toggle_count = 0;
}

void check_gpu_module(void)
{

	gpu_id_0 = get_hardware_id(ADC_GPU_BOARD_ID_0);
	gpu_id_1 = get_hardware_id(ADC_GPU_BOARD_ID_1);
	switch_status = get_gpu_latch();
	fan_present = 0;
	switch (VALID_BOARDID(gpu_id_0, gpu_id_1)) {
	case VALID_BOARDID(BOARD_VERSION_12, BOARD_VERSION_12):
		LOG_DBG("Detected dual interposer device");
		module_present = 1;
		module_fault = 0;
		break;
	case VALID_BOARDID(BOARD_VERSION_11, BOARD_VERSION_15):
		LOG_DBG("Detected single interposer device");
		module_present = 1;
		module_fault = 0;
		break;
	case VALID_BOARDID(BOARD_VERSION_13, BOARD_VERSION_15):
		LOG_DBG("Detected UMA fan board");
		fan_present = 1;
		module_present = 0;
		module_fault = 0;
		break;
	case VALID_BOARDID(BOARD_VERSION_15, BOARD_VERSION_15):
		LOG_DBG("No gpu module detected %d %d", gpu_id_0, gpu_id_1);
		module_present = 0;
		module_fault = 0;
		if (board_get_version() < BOARD_VERSION_8) {
			fan_present = 1;
		}
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
		init_gpu_module();
	} else {
		deinit_gpu_module();
	}
	if (fan_present) {
		init_uma_fan();
	}
	update_gpu_ac_power_state();

	beam_function();
}
DECLARE_DEFERRED(check_gpu_module);
DECLARE_HOOK(HOOK_INIT, check_gpu_module, HOOK_PRIO_INIT_ADC + 1);

__override void project_chassis_function(enum gpio_signal signal)
{
}

void beam_open_interrupt(enum gpio_signal signal)
{
	int open_state = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_f_beam_open_l));

	/* The dGPU SW is SW4 at DVT phase */
	if (board_get_version() < BOARD_VERSION_7)
		return;

	if (!open_state) {
		/* Make sure the module is off as fast as possible! */
		LOG_DBG("Powering off GPU");
		deinit_gpu_module();
		switch_status = 0;
	} else {
		hook_call_deferred(&check_gpu_module_data, 200 * MSEC);
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
	if ((gpu_status & GPU_EDP_MASK) == ASSERTED_EDP_RESET) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_reset), 0);
		gpu_status &= ~GPU_EDP_MASK;
	}

	if ((gpu_status & GPU_EDP_MASK) == DEASSERTED_EDP_RESET) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_edp_reset), 1);
		gpu_status &= ~GPU_EDP_MASK;
	}

	*host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_CONTROL) = gpu_status;

	/* Polling to check the shared memory of the GPU */
	if (!chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) ||
	    !chipset_in_state(CHIPSET_STATE_ANY_OFF))
		hook_call_deferred(&gpu_smart_access_graphic_data, 10 * MSEC);
}
/* TODO FIXME ON GPU LATE CONNECT */
static void start_smart_access_graphic(void)
{
	/* Check GPU is present then polling the namespace to do the smart access graphic */
	if (gpu_present())
		hook_call_deferred(&gpu_smart_access_graphic_data, 10 * MSEC);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, start_smart_access_graphic, HOOK_PRIO_DEFAULT);





static enum ec_status host_command_expansion_bay_status(struct host_cmd_handler_args *args)
{
	struct ec_response_expansion_bay_status *r = args->response;

	r->state = 0;
	if (module_present) {
		r->state |= MODULE_ENABLED;
	}
	if (module_fault) {
		r->state |= MODULE_FAULT;
	}
	if (switch_status) {
		r->state |= HATCH_SWITCH_CLOSED;
	}
	r->board_id_0 = gpu_id_0;
	r->board_id_1 = gpu_id_1;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_EXPANSION_BAY_STATUS, host_command_expansion_bay_status,
		EC_VER_MASK(0));
