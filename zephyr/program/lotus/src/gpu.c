
/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ADC for checking BOARD ID.
 */

#include "adc.h"
#include "board_adc.h"
#include "console.h"
#include "system.h"
#include "hooks.h"

#define CPRINTS(format, args...) cprints(CC_GPIO, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_GPIO, format, ## args)


#ifdef CONFIG_PLATFORM_EC_GPU_POWER_CONTROL

static void check_gpu_module(void)
{
	int gpu_board_version_0 = get_hardware_id(ADC_GPU_BOARD_ID_0);
	int gpu_board_version_1 = get_hardware_id(ADC_GPU_BOARD_ID_1);
	int prevent_power_on = 0;

	if (gpu_board_version_0 == BOARD_VERSION_13) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_3v_5v_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw), 0);
	} else if (gpu_board_version_0 == BOARD_VERSION_11) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_3v_5v_en), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw), 1);
	} else if (gpu_board_version_0 == BOARD_VERSION_12) {
		if (gpu_board_version_1 == BOARD_VERSION_12) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_3v_5v_en), 1);
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw), 1);
		} else {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_3v_5v_en), 0);
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw), 0);
			prevent_power_on = 1;
		}
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_3v_5v_en), 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw), 0);
		prevent_power_on = 1;
	}

	/* TODO: LED blink error*/
	if (prevent_power_on)
		CPRINTS("GPU connect error, prevent power on");
}
DECLARE_HOOK(HOOK_INIT, check_gpu_module, HOOK_PRIO_INIT_ADC + 1);

#endif /* CONFIG_PLATFORM_EC_GPU_POWER_CONTROL */

