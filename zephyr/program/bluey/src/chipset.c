/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Bluey chipset-specific configuration */

#include "battery.h"
#include "common.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "power/qcom.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

void board_chipset_pre_init(void)
{
	/* Cache the battery dynamic information before AP power on */
	if (battery_is_present() == BP_YES) {
		battery_poll_dynamic_info();
		CPRINTS("battery dynamic information cached");
	}
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, board_chipset_pre_init, HOOK_PRIO_DEFAULT);

void board_chipset_startup(void)
{
#if defined(CONFIG_BOARD_BLUEY)
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_off_odl), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 1);
#endif

#if defined(CONFIG_BOARD_QUARTZ)
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_haptic_en_ec), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tpad_en), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_bl_off_odl), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 1);
#else
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_usb_en), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_hdmi_pwr), 1);
#endif
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_3v_s3_en), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000), 1);
	/* Update the AC event during boot */
	extpower_update_host_events(gpio_get_level(GPIO_AC_PRESENT));
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

void board_chipset_shutdown(void)
{
#if defined(CONFIG_BOARD_BLUEY)
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_off_odl), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 0);
#endif

#if defined(CONFIG_BOARD_QUARTZ)
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_haptic_en_ec), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_tpad_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_bl_off_odl), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000_fan), 0);
#else
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_usb_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_hdmi_pwr), 0);
#endif
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_3v_s3_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_ppvar_oled), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_pp5000), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

void passthru_lid_open_to_pmic(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_pmic_lid_open_od),
			gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_lid_open)));
}

void passthru_ac_on_to_pmic(void)
{
	gpio_pin_set_dt(
		GPIO_DT_FROM_NODELABEL(gpio_ec_pmic_acok),
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_acok_od_z5)));
}
