/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Zork family-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "button.h"
#include "cbi_ec_fw_config.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charge_state_v2.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "cros_board_info.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/retimer/pi3hdx1204.h"
#include "driver/usb_mux/amd_fp5.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "ioexpander.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "tcpci.h"
#include "thermistor.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "util.h"

#define SAFE_RESET_VBUS_MV 5000

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
	GPIO_EC_RST_ODL,
};
const int hibernate_wake_pins_used =  ARRAY_SIZE(hibernate_wake_pins);

/*
 * In the AOZ1380 PPC, there are no programmable features.  We use
 * the attached NCT3807 to control a GPIO to indicate 1A5 or 3A0
 * current limits.
 */
__overridable int board_aoz1380_set_vbus_source_current_limit(int port,
						enum tcpc_rp_value rp)
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

__overridable void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	charge_set_input_current_limit(MAX(charge_ma,
					   CONFIG_CHARGER_INPUT_CURRENT),
				       charge_mv);
}

/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
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
	{ 2761 / THERMISTOR_SCALING_FACTOR, 0},
	{ 2492 / THERMISTOR_SCALING_FACTOR, 10},
	{ 2167 / THERMISTOR_SCALING_FACTOR, 20},
	{ 1812 / THERMISTOR_SCALING_FACTOR, 30},
	{ 1462 / THERMISTOR_SCALING_FACTOR, 40},
	{ 1146 / THERMISTOR_SCALING_FACTOR, 50},
	{ 878 / THERMISTOR_SCALING_FACTOR, 60},
	{ 665 / THERMISTOR_SCALING_FACTOR, 70},
	{ 500 / THERMISTOR_SCALING_FACTOR, 80},
	{ 434 / THERMISTOR_SCALING_FACTOR, 85},
	{ 376 / THERMISTOR_SCALING_FACTOR, 90},
	{ 326 / THERMISTOR_SCALING_FACTOR, 95},
	{ 283 / THERMISTOR_SCALING_FACTOR, 100}
};

const struct thermistor_info thermistor_info = {
	.scaling_factor = THERMISTOR_SCALING_FACTOR,
	.num_pairs = ARRAY_SIZE(thermistor_data),
	.data = thermistor_data,
};

__overridable int board_get_temp(int idx, int *temp_k)
{
	int mv;
	int temp_c;
	enum adc_channel channel;

	/* idx is the sensor index set in board temp_sensors[] */
	switch (idx) {
	case TEMP_SENSOR_CHARGER:
		channel = ADC_TEMP_SENSOR_CHARGER;
		break;
	case TEMP_SENSOR_SOC:
		/* thermistor is not powered in G3 */
		if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
			return EC_ERROR_NOT_POWERED;

		channel = ADC_TEMP_SENSOR_SOC;
		break;
	default:
		return EC_ERROR_INVAL;
	}

	mv = adc_read_channel(channel);
	if (mv < 0)
		return EC_ERROR_INVAL;

	temp_c = thermistor_linear_interpolate(mv, &thermistor_info);
	*temp_k = C_TO_K(temp_c);
	return EC_SUCCESS;
}

#ifndef TEST_BUILD
void lid_angle_peripheral_enable(int enable)
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
#endif

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
		msleep(300);
	}
}

__overridable int check_hdmi_hpd_status(void)
{
	/* Default hdmi insert. */
	return 1;
}

const struct pi3hdx1204_tuning pi3hdx1204_tuning = {
	.eq_ch0_ch1_offset = PI3HDX1204_EQ_DB710,
	.eq_ch2_ch3_offset = PI3HDX1204_EQ_DB710,
	.vod_offset = PI3HDX1204_VOD_115_ALL_CHANNELS,
	.de_offset = PI3HDX1204_DE_DB_MINUS5,
};

void pi3hdx1204_retimer_power(void)
{
	if (ec_config_has_hdmi_retimer_pi3hdx1204()) {
		int enable = chipset_in_or_transitioning_to_state(
			CHIPSET_STATE_ON) && check_hdmi_hpd_status();
		pi3hdx1204_enable(I2C_PORT_TCPC1,
				  PI3HDX1204_I2C_ADDR_FLAGS,
				  enable);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, pi3hdx1204_retimer_power, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, pi3hdx1204_retimer_power, HOOK_PRIO_DEFAULT);

void sbu_fault_interrupt(enum ioex_signal signal)
{
	int port = (signal == IOEX_USB_C0_SBU_FAULT_ODL) ? 0 : 1;

	pd_handle_overcurrent(port);
}
