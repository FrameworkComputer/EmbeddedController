/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Zork family-specific configuration */

#include "adc.h"
#include "button.h"
#include "cbi_ec_fw_config.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "cros_board_info.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/charger/isl9241.h"
#include "driver/retimer/pi3hdx1204.h"
#include "driver/usb_mux/amd_fp5.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "ioexpander.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "printf.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "util.h"

#define SAFE_RESET_VBUS_MV 5000

/*
 * For legacy BC1.2 charging with CONFIG_CHARGE_RAMP_SW, ramp up input current
 * until voltage drops to 4.5V. Don't go lower than this to be kind to the
 * charger (see b/67964166).
 */
#define BC12_MIN_VOLTAGE 4500

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
	GPIO_EC_RST_ODL,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/*
 * In the AOZ1380 PPC, there are no programmable features.  We use
 * the attached NCT3807 to control a GPIO to indicate 1A5 or 3A0
 * current limits.
 */
__overridable int
board_aoz1380_set_vbus_source_current_limit(int port, enum tcpc_rp_value rp)
{
	int rv;

	/* Use the TCPC to set the current limit */
	rv = ioex_set_level(IOEX_USB_C0_PPC_ILIM_3A_EN,
			    (rp == TYPEC_RP_3A0) ? 1 : 0);

	return rv;
}

static void baseboard_chipset_suspend(void)
{
	/* Disable display and keyboard backlights. */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT_L, 1);
	ioex_set_level(IOEX_KB_BL_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, baseboard_chipset_suspend,
	     HOOK_PRIO_DEFAULT);

static void baseboard_chipset_resume(void)
{
	/* Enable display and keyboard backlights. */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT_L, 0);
	ioex_set_level(IOEX_KB_BL_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, baseboard_chipset_resume, HOOK_PRIO_DEFAULT);

/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	/*
	 * F3 key scan cycle completed but scan input is not
	 * charging to logic high when EC start scan next
	 * column for "T" key, so we set .output_settle_us
	 * to 80us
	 */
	.output_settle_us = 80,
	.debounce_down_us = 6 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 1500,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = SECOND,
	.actual_key_mask = {
		0x3c, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

/*
 * We use 11 as the scaling factor so that the maximum mV value below (2761)
 * can be compressed to fit in a uint8_t.
 */
#define THERMISTOR_SCALING_FACTOR 11

/*
 * Values are calculated from the "Resistance VS. Temperature" table on the
 * Murata page for part NCP15WB473F03RC. Vdd=3.3V, R=30.9Kohm.
 */
const struct thermistor_data_pair thermistor_data[] = {
	{ 2761 / THERMISTOR_SCALING_FACTOR, 0 },
	{ 2492 / THERMISTOR_SCALING_FACTOR, 10 },
	{ 2167 / THERMISTOR_SCALING_FACTOR, 20 },
	{ 1812 / THERMISTOR_SCALING_FACTOR, 30 },
	{ 1462 / THERMISTOR_SCALING_FACTOR, 40 },
	{ 1146 / THERMISTOR_SCALING_FACTOR, 50 },
	{ 878 / THERMISTOR_SCALING_FACTOR, 60 },
	{ 665 / THERMISTOR_SCALING_FACTOR, 70 },
	{ 500 / THERMISTOR_SCALING_FACTOR, 80 },
	{ 434 / THERMISTOR_SCALING_FACTOR, 85 },
	{ 376 / THERMISTOR_SCALING_FACTOR, 90 },
	{ 326 / THERMISTOR_SCALING_FACTOR, 95 },
	{ 283 / THERMISTOR_SCALING_FACTOR, 100 }
};

const struct thermistor_info thermistor_info = {
	.scaling_factor = THERMISTOR_SCALING_FACTOR,
	.num_pairs = ARRAY_SIZE(thermistor_data),
	.data = thermistor_data,
};

__override void lid_angle_peripheral_enable(int enable)
{
	if (ec_config_has_lid_angle_tablet_mode()) {
		int chipset_in_s0 = chipset_in_state(CHIPSET_STATE_ON);

		if (enable) {
			keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_ANGLE);
		} else {
			/*
			 * Ensure that the chipset is off before disabling the
			 * keyboard. When the chipset is on, the EC keeps the
			 * keyboard enabled and the AP decides whether to
			 * ignore input devices or not.
			 */
			if (!chipset_in_s0)
				keyboard_scan_enable(0,
						     KB_SCAN_DISABLE_LID_ANGLE);
		}
	}
}

static void cbi_init(void)
{
	uint32_t val;

	if (cbi_get_board_version(&val) == EC_SUCCESS)
		ccprints("Board Version: %d (0x%x)", val, val);
	else
		ccprints("Board Version: not set in cbi");

	if (cbi_get_sku_id(&val) == EC_SUCCESS)
		ccprints("SKU ID: %d (0x%x)", val, val);
	else
		ccprints("SKU ID: not set in cbi");

	val = get_cbi_fw_config();
	if (val != UNINITIALIZED_FW_CONFIG)
		ccprints("FW Config: %d (0x%x)", val, val);
	else
		ccprints("FW Config: not set in cbi");
}
DECLARE_HOOK(HOOK_INIT, cbi_init, HOOK_PRIO_INIT_I2C + 1);

/*
 * Returns 1 for boards that are convertible into tablet mode, and zero for
 * clamshells.
 */
int board_is_lid_angle_tablet_mode(void)
{
	return ec_config_has_lid_angle_tablet_mode();
}

__override uint32_t board_override_feature_flags0(uint32_t flags0)
{
	/*
	 * Remove keyboard backlight feature for devices that don't support it.
	 */
	if (ec_config_has_pwm_keyboard_backlight() == PWM_KEYBOARD_BACKLIGHT_NO)
		return (flags0 & ~EC_FEATURE_MASK_0(EC_FEATURE_PWM_KEYB));
	else
		return flags0;
}

__overridable void zork_board_hibernate(void)
{
	/* Stub for model specific hibernate callback */
}

void board_hibernate(void)
{
	int port;

	/*
	 * If we are charging, then drop the Vbus level down to 5V to ensure
	 * that we don't get locked out of the 6.8V OVLO for our PPCs in
	 * dead-battery mode. This is needed when the TCPC/PPC rails go away.
	 * (b/79218851, b/143778351, b/147007265)
	 */
	port = charge_manager_get_active_charge_port();
	if (port != CHARGE_PORT_NONE) {
		pd_request_source_voltage(port, SAFE_RESET_VBUS_MV);

		/* Give PD task and PPC chip time to get to 5V */
		msleep(900);
	}

	zork_board_hibernate();
}

__overridable int check_hdmi_hpd_status(void)
{
	/* Default hdmi insert. */
	return 1;
}

void sbu_fault_interrupt(enum ioex_signal signal)
{
	int port = (signal == IOEX_USB_C0_SBU_FAULT_ODL) ? 0 : 1;

	pd_handle_overcurrent(port);
}

static void set_ac_prochot(void)
{
	isl9241_set_ac_prochot(CHARGER_SOLO, ZORK_AC_PROCHOT_CURRENT_MA);
}
DECLARE_HOOK(HOOK_INIT, set_ac_prochot, HOOK_PRIO_DEFAULT);

DECLARE_DEFERRED(board_print_temps);
int temps_interval;

void board_print_temps(void)
{
	int t, i;
	int rv;
	char ts_str[PRINTF_TIMESTAMP_BUF_SIZE];

	snprintf_timestamp_now(ts_str, sizeof(ts_str));
	cprintf(CC_THERMAL, "[%s ", ts_str);
	for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {
		rv = temp_sensor_read(i, &t);
		if (rv == EC_SUCCESS)
			cprintf(CC_THERMAL, "%s=%dK (%dC) ",
				temp_sensors[i].name, t, K_TO_C(t));
	}
	cprintf(CC_THERMAL, "]\n");

	if (temps_interval > 0)
		hook_call_deferred(&board_print_temps_data,
				   temps_interval * SECOND);
}

static int command_temps_log(int argc, const char **argv)
{
	char *e = NULL;

	if (argc != 2)
		return EC_ERROR_PARAM_COUNT;

	temps_interval = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	board_print_temps();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(tempslog, command_temps_log, "seconds",
			"Print temp sensors periodically");

/*
 * b/164921478: On G3->S5, wait for RSMRST_L to be deasserted before asserting
 * PWRBTN_L.
 */
void board_pwrbtn_to_pch(int level)
{
	/* Add delay for G3 exit if asserting PWRBTN_L and S5_PGOOD is low. */
	if (!level && !gpio_get_level(GPIO_S5_PGOOD)) {
		/*
		 * From measurement, wait 80 ms for RSMRST_L to rise after
		 * S5_PGOOD.
		 */
		msleep(80);

		if (!gpio_get_level(GPIO_S5_PGOOD))
			ccprints("Error: pwrbtn S5_PGOOD low");
	}
	gpio_set_level(GPIO_PCH_PWRBTN_L, level);
}

/**
 * Return if VBUS is sagging too low
 */
int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	int voltage = 0;
	int rv;

	rv = charger_get_vbus_voltage(port, &voltage);

	if (rv) {
		ccprints("%s rv=%d", __func__, rv);
		return 0;
	}

	/*
	 * b/168569046: The ISL9241 sometimes incorrectly reports 0 for unknown
	 * reason, causing ramp to stop at 0.5A. Workaround this by ignoring 0.
	 * This partly defeats the point of ramping, but will still catch
	 * VBUS below 4.5V and above 0V.
	 */
	if (voltage == 0) {
		ccprints("%s vbus=0", __func__);
		return 0;
	}

	if (voltage < BC12_MIN_VOLTAGE)
		ccprints("%s vbus=%d", __func__, voltage);

	return voltage < BC12_MIN_VOLTAGE;
}

/**
 * Always ramp up input current since AP needs higher power, even if battery is
 * very low or full. We can always re-ramp if input current increases beyond
 * what supplier can provide.
 */
__override int charge_is_consuming_full_input_current(void)
{
	return 1;
}
