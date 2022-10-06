/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Waddledoo board-specific configuration */

#include "adc.h"
#include "button.h"
#include "cbi_fw_config.h"
#include "cbi_ssfc.h"
#include "cbi_fw_config.h"
#include "charge_manager.h"
#include "charge_state_v2.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "compile_time_macros.h"
#include "driver/accel_bma2x2.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accelgyro_icm_common.h"
#include "driver/accelgyro_icm426xx.h"
#include "driver/accel_kionix.h"
#include "driver/temp_sensor/thermistor.h"
#include "temp_sensor.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/charger/isl923x.h"
#include "driver/tcpm/raa489000.h"
#include "driver/tcpm/tcpci.h"
#include "driver/usb_mux/pi3usb3x532.h"
#include "driver/usb_mux/ps8743.h"
#include "driver/retimer/ps8802.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_config.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "stdbool.h"
#include "switch.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

#define INT_RECHECK_US 5000

#define ADC_VOL_UP_MASK BIT(0)
#define ADC_VOL_DOWN_MASK BIT(1)

static uint8_t new_adc_key_state;

static void ps8762_chaddr_deferred(void);
DECLARE_DEFERRED(ps8762_chaddr_deferred);

/******************************************************************************/
/* USB-A Configuration */
const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_USB_A0_VBUS,
	GPIO_EN_USB_A1_VBUS,
};

#ifdef BOARD_MAGOLOR
/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	/*
	 * F3 key scan cycle completed but scan input is not
	 * charging to logic high when EC start scan next
	 * column for "T" key, so we set .output_settle_us
	 * to 80us from 50us.
	 */
	.output_settle_us = 80,
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x1c, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfe, 0xff, 0xff, 0xff,  /* full set */
	},
};

static const struct ec_response_keybd_config magolor_keybd = {
	/* Default Chromeos keyboard config */
	.num_top_row_keys = 10,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_FORWARD,		/* T2 */
		TK_REFRESH,		/* T3 */
		TK_FULLSCREEN,		/* T4 */
		TK_OVERVIEW,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_VOL_MUTE,		/* T8 */
		TK_VOL_DOWN,		/* T9 */
		TK_VOL_UP,		/* T10 */
	},
	/* No function keys, no numeric keypad, has screenlock key */
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

static const struct ec_response_keybd_config magister_keybd = {
	/* Default Chromeos keyboard config */
	.num_top_row_keys = 10,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_REFRESH,		/* T2 */
		TK_FULLSCREEN,		/* T3 */
		TK_OVERVIEW,		/* T4 */
		TK_SNAPSHOT,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_VOL_MUTE,		/* T8 */
		TK_VOL_DOWN,		/* T9 */
		TK_VOL_UP,		/* T10 */
	},
	/* No function keys, no numeric keypad, has screenlock key */
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

static const struct ec_response_keybd_config magpie_keybd = {
	.num_top_row_keys = 10,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_FORWARD,		/* T2 */
		TK_REFRESH,		/* T3 */
		TK_FULLSCREEN,		/* T4 */
		TK_OVERVIEW,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_VOL_MUTE,		/* T8 */
		TK_VOL_DOWN,		/* T9 */
		TK_VOL_UP,		/* T10 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY | KEYBD_CAP_NUMERIC_KEYPAD,
};

static const struct ec_response_keybd_config magma_keybd = {
	.num_top_row_keys = 10,
	.action_keys = {
		TK_BACK,		/* T1 */
		TK_REFRESH,		/* T2 */
		TK_FULLSCREEN,		/* T3 */
		TK_OVERVIEW,		/* T4 */
		TK_SNAPSHOT,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_VOL_MUTE,		/* T8 */
		TK_VOL_DOWN,		/* T9 */
		TK_VOL_UP,		/* T10 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY | KEYBD_CAP_NUMERIC_KEYPAD,
};

__override uint8_t board_keyboard_row_refresh(void)
{
	if (gpio_get_level(GPIO_EC_VIVALDIKEYBOARD_ID))
		return 3;
	else
		return 2;
}

__override const struct ec_response_keybd_config *
board_vivaldi_keybd_config(void)
{
	if (get_cbi_fw_config_numeric_pad()) {
		if (gpio_get_level(GPIO_EC_VIVALDIKEYBOARD_ID))
			return &magma_keybd;
		else
			return &magpie_keybd;
	} else {
		if (gpio_get_level(GPIO_EC_VIVALDIKEYBOARD_ID))
			return &magister_keybd;
		else
			return &magolor_keybd;
	}
}
#endif

/*
 * We have total 30 pins for keyboard connecter {-1, -1} mean
 * the N/A pin that don't consider it and reserve index 0 area
 * that we don't have pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ -1, -1 }, { 0, 5 },	{ 1, 1 }, { 1, 0 },   { 0, 6 },	  { 0, 7 },
	{ -1, -1 }, { -1, -1 }, { 1, 4 }, { 1, 3 },   { -1, -1 }, { 1, 6 },
	{ 1, 7 },   { 3, 1 },	{ 2, 0 }, { 1, 5 },   { 2, 6 },	  { 2, 7 },
	{ 2, 1 },   { 2, 4 },	{ 2, 5 }, { 1, 2 },   { 2, 3 },	  { 2, 2 },
	{ 3, 0 },   { -1, -1 }, { 0, 4 }, { -1, -1 }, { 8, 2 },	  { -1, -1 },
	{ -1, -1 },
};
const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);

/* C0 interrupt line shared by BC 1.2 and charger */
static void check_c0_line(void);
DECLARE_DEFERRED(check_c0_line);

static void notify_c0_chips(void)
{
	/*
	 * The interrupt line is shared between the TCPC and BC 1.2 detection
	 * chip.  Therefore we'll need to check both ICs.
	 */
	schedule_deferred_pd_interrupt(0);
	usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
}

static void check_c0_line(void)
{
	/*
	 * If line is still being held low, see if there's more to process from
	 * one of the chips
	 */
	if (!gpio_get_level(GPIO_USB_C0_INT_ODL)) {
		notify_c0_chips();
		hook_call_deferred(&check_c0_line_data, INT_RECHECK_US);
	}
}

static void usb_c0_interrupt(enum gpio_signal s)
{
	/* Cancel any previous calls to check the interrupt line */
	hook_call_deferred(&check_c0_line_data, -1);

	/* Notify all chips using this line that an interrupt came in */
	notify_c0_chips();

	/* Check the line again in 5ms */
	hook_call_deferred(&check_c0_line_data, INT_RECHECK_US);
}

/* C1 interrupt line shared by BC 1.2, TCPC, and charger */
static void check_c1_line(void);
DECLARE_DEFERRED(check_c1_line);

static void notify_c1_chips(void)
{
	schedule_deferred_pd_interrupt(1);
	usb_charger_task_set_event(1, USB_CHG_EVENT_BC12);
}

static void check_c1_line(void)
{
	/*
	 * If line is still being held low, see if there's more to process from
	 * one of the chips.
	 */
	if (!gpio_get_level(GPIO_SUB_C1_INT_EN_RAILS_ODL)) {
		notify_c1_chips();
		hook_call_deferred(&check_c1_line_data, INT_RECHECK_US);
	}
}

static void sub_usb_c1_interrupt(enum gpio_signal s)
{
	/* Cancel any previous calls to check the interrupt line */
	hook_call_deferred(&check_c1_line_data, -1);

	/* Notify all chips using this line that an interrupt came in */
	notify_c1_chips();

	/* Check the line again in 5ms */
	hook_call_deferred(&check_c1_line_data, INT_RECHECK_US);
}

static void sub_hdmi_hpd_interrupt(enum gpio_signal s)
{
	int hdmi_hpd_odl = gpio_get_level(GPIO_EC_I2C_SUB_C1_SDA_HDMI_HPD_ODL);

	gpio_set_level(GPIO_EC_AP_USB_C1_HDMI_HPD, !hdmi_hpd_odl);
}

#include "gpio_list.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1] = {
		.name = "TEMP_SENSOR1",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_2] = {
		.name = "TEMP_SENSOR2",
		.input_ch = NPCX_ADC_CH1,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_SUB_ANALOG] = {
		.name = "SUB_ANALOG",
		.input_ch = NPCX_ADC_CH2,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_VSNS_PP3300_A] = {
		.name = "PP3300_A_PGOOD",
		.input_ch = NPCX_ADC_CH9,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Thermistors */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1] = { .name = "Memory",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_1 },
	[TEMP_SENSOR_2] = { .name = "Ambient",
			    .type = TEMP_SENSOR_TYPE_BOARD,
			    .read = get_temp_3v3_51k1_47k_4050b,
			    .idx = ADC_TEMP_SENSOR_2 },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * TODO(b/202062363): Remove when clang is fixed.
 */
#define THERMAL_A                \
	{                        \
		.temp_host = { \
			[EC_TEMP_THRESH_WARN] = 0, \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(70), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(85), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_WARN] = 0, \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(65), \
			[EC_TEMP_THRESH_HALT] = 0, \
		}, \
	}
__maybe_unused static const struct ec_thermal_config thermal_a = THERMAL_A;

/*
 * TODO(b/202062363): Remove when clang is fixed.
 */
#define THERMAL_B                \
	{                        \
		.temp_host = { \
			[EC_TEMP_THRESH_WARN] = 0, \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(73), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(85), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_WARN] = 0, \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(65), \
			[EC_TEMP_THRESH_HALT] = 0, \
		}, \
	}
__maybe_unused static const struct ec_thermal_config thermal_b = THERMAL_B;

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

static void setup_thermal(void)
{
	thermal_params[TEMP_SENSOR_1] = thermal_a;
	thermal_params[TEMP_SENSOR_2] = thermal_b;
}

#ifdef BOARD_MAGOLOR
static void board_update_no_keypad_by_fwconfig(void)
{
	if (!get_cbi_fw_config_numeric_pad()) {
#ifndef TEST_BUILD
		/* Disable scanning KSO13 & 14 if keypad isn't present. */
		keyboard_raw_set_cols(KEYBOARD_COLS_NO_KEYPAD);
		keyscan_config.actual_key_mask[11] = 0xfa;
		keyscan_config.actual_key_mask[12] = 0xca;
#endif
	}
}
#endif

/* Enable HDMI any time the SoC is on */
static void hdmi_enable(void)
{
	if (get_cbi_fw_config_db() == DB_1A_HDMI)
		gpio_set_level(GPIO_EC_I2C_SUB_C1_SCL_HDMI_EN_ODL, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, hdmi_enable, HOOK_PRIO_DEFAULT);

static void hdmi_disable(void)
{
	if (get_cbi_fw_config_db() == DB_1A_HDMI)
		gpio_set_level(GPIO_EC_I2C_SUB_C1_SCL_HDMI_EN_ODL, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, hdmi_disable, HOOK_PRIO_DEFAULT);

void board_hibernate(void)
{
	/*
	 * Both charger ICs need to be put into their "low power mode" before
	 * entering the Z-state.
	 */
	if (board_get_charger_chip_count() > 1)
		raa489000_hibernate(1, true);
	raa489000_hibernate(0, true);
}

void board_reset_pd_mcu(void)
{
	/*
	 * TODO(b:147316511): Here we could issue a digital reset to the IC,
	 * unsure if we actually want to do that or not yet.
	 */
}

#ifdef BOARD_WADDLEDOO
static void reconfigure_5v_gpio(void)
{
	/*
	 * b/147257497: On early waddledoo boards, GPIO_EN_PP5000 was swapped
	 * with GPIO_VOLUP_BTN_ODL. Therefore, we'll actually need to set that
	 * GPIO instead for those boards.  Note that this breaks the volume up
	 * button functionality.
	 */
	if (system_get_board_version() < 0) {
		CPRINTS("old board - remapping 5V en");
		gpio_set_flags(GPIO_VOLUP_BTN_ODL, GPIO_OUT_LOW);
	}
}
DECLARE_HOOK(HOOK_INIT, reconfigure_5v_gpio, HOOK_PRIO_INIT_I2C + 1);
#endif /* BOARD_WADDLEDOO */

static void set_5v_gpio(int level)
{
	int version;
	enum gpio_signal gpio = GPIO_EN_PP5000;

	/*
	 * b/147257497: On early waddledoo boards, GPIO_EN_PP5000 was swapped
	 * with GPIO_VOLUP_BTN_ODL. Therefore, we'll actually need to set that
	 * GPIO instead for those boards.  Note that this breaks the volume up
	 * button functionality.
	 */
	if (IS_ENABLED(BOARD_WADDLEDOO)) {
		version = system_get_board_version();

		/*
		 * If the CBI EEPROM wasn't formatted, assume it's a very early
		 * board.
		 */
		gpio = version < 0 ? GPIO_VOLUP_BTN_ODL : GPIO_EN_PP5000;
	}

	gpio_set_level(gpio, level);
}

static void ps8762_chaddr_deferred(void)
{
	/* Switch PS8762 I2C Address to 0x50*/
	if (ps8802_chg_i2c_addr(I2C_PORT_SUB_USB_C1) == EC_SUCCESS)
		CPRINTS("Switch PS8762 address to 0x50 success");
	else
		CPRINTS("Switch PS8762 address to 0x50 failed");
}

__override void board_power_5v_enable(int enable)
{
	/*
	 * Port 0 simply has a GPIO to turn on the 5V regulator, however, 5V is
	 * generated locally on the sub board and we need to set the comparator
	 * polarity on the sub board charger IC.
	 */
	set_5v_gpio(!!enable);

	if (get_cbi_fw_config_db() == DB_1A_HDMI) {
		gpio_set_level(GPIO_SUB_C1_INT_EN_RAILS_ODL, !enable);
	} else {
		if (isl923x_set_comparator_inversion(1, !!enable))
			CPRINTS("Failed to %sable sub rails!",
				enable ? "en" : "dis");

		if (!enable)
			return;
		/*
		 * Port C1 the PP3300_USB_C1  assert, delay 15ms
		 * colud be accessed PS8762 by I2C.
		 */
		if (get_cbi_ssfc_usb_mux() == SSFC_USBMUX_PS8762)
			hook_call_deferred(&ps8762_chaddr_deferred_data,
					   15 * MSEC);
	}
}

__override uint8_t board_get_usb_pd_port_count(void)
{
	if (get_cbi_fw_config_db() == DB_1A_HDMI)
		return CONFIG_USB_PD_PORT_MAX_COUNT - 1;
	else
		return CONFIG_USB_PD_PORT_MAX_COUNT;
}

__override uint8_t board_get_charger_chip_count(void)
{
	if (get_cbi_fw_config_db() == DB_1A_HDMI)
		return CHARGER_NUM - 1;
	else
		return CHARGER_NUM;
}

int board_is_sourcing_vbus(int port)
{
	int regval;

	tcpc_read(port, TCPC_REG_POWER_STATUS, &regval);
	return !!(regval & TCPC_REG_POWER_STATUS_SOURCING_VBUS);
}

int board_set_active_charge_port(int port)
{
	int is_real_port = (port >= 0 && port < board_get_usb_pd_port_count());
	int i;
	int old_port;

	if (!is_real_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	old_port = charge_manager_get_active_charge_port();

	CPRINTS("New chg p%d", port);

	/* Disable all ports. */
	if (port == CHARGE_PORT_NONE) {
		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			tcpc_write(i, TCPC_REG_COMMAND,
				   TCPC_REG_COMMAND_SNK_CTRL_LOW);
			raa489000_enable_asgate(i, false);
		}

		return EC_SUCCESS;
	}

	/* Check if port is sourcing VBUS. */
	if (board_is_sourcing_vbus(port)) {
		CPRINTS("Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i == port)
			continue;

		if (tcpc_write(i, TCPC_REG_COMMAND,
			       TCPC_REG_COMMAND_SNK_CTRL_LOW))
			CPRINTS("p%d: sink path disable failed.", i);
		raa489000_enable_asgate(i, false);
	}

	/*
	 * Stop the charger IC from switching while changing ports.  Otherwise,
	 * we can overcurrent the adapter we're switching to. (crbug.com/926056)
	 */
	if (old_port != CHARGE_PORT_NONE)
		charger_discharge_on_ac(1);

	/* Enable requested charge port. */
	if (raa489000_enable_asgate(port, true) ||
	    tcpc_write(port, TCPC_REG_COMMAND,
		       TCPC_REG_COMMAND_SNK_CTRL_HIGH)) {
		CPRINTS("p%d: sink path enable failed.", port);
		charger_discharge_on_ac(0);
		return EC_ERROR_UNKNOWN;
	}

	/* Allow the charger IC to begin/continue switching. */
	charger_discharge_on_ac(0);

	return EC_SUCCESS;
}

void board_set_charge_limit(int port, int supplier, int charge_ma, int max_ma,
			    int charge_mv)
{
	int icl = MAX(charge_ma, CONFIG_CHARGER_INPUT_CURRENT);

	/*
	 * b/147463641: The charger IC seems to overdraw ~4%, therefore we
	 * reduce our target accordingly.
	 */
	icl = icl * 96 / 100;
	charge_set_input_current_limit(icl, charge_mv);
}

__override void typec_set_source_current_limit(int port, enum tcpc_rp_value rp)
{
	if (port < 0 || port > board_get_usb_pd_port_count())
		return;

	raa489000_set_output_current(port, rp);
}

/* Sensors */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* Matrices to rotate accelerometers into the standard reference. */
static const mat33_fp_t lid_standard_ref = { { FLOAT_TO_FP(1), 0, 0 },
					     { 0, FLOAT_TO_FP(-1), 0 },
					     { 0, 0, FLOAT_TO_FP(-1) } };

/* Matrices to rotate accelerometers into the magister reference. */
static const mat33_fp_t lid_magister_ref = { { FLOAT_TO_FP(-1), 0, 0 },
					     { 0, FLOAT_TO_FP(-1), 0 },
					     { 0, 0, FLOAT_TO_FP(1) } };

static const mat33_fp_t base_standard_ref = { { 0, FLOAT_TO_FP(1), 0 },
					      { FLOAT_TO_FP(1), 0, 0 },
					      { 0, 0, FLOAT_TO_FP(-1) } };

/* BMA253 private data */
static struct accelgyro_saved_data_t g_bma253_data;

/* BMI160 private data */
static struct bmi_drv_data_t g_bmi160_data;

#ifdef BOARD_MAGOLOR
static const mat33_fp_t base_icm_ref = { { FLOAT_TO_FP(-1), 0, 0 },
					 { 0, FLOAT_TO_FP(1), 0 },
					 { 0, 0, FLOAT_TO_FP(-1) } };

/* ICM426 private data */
static struct icm_drv_data_t g_icm426xx_data;
/* KX022 private data */
static struct kionix_accel_data g_kx022_data;

struct motion_sensor_t kx022_lid_accel = {
	.name = "Lid Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_KX022,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_LID,
	.drv = &kionix_accel_drv,
	.mutex = &g_lid_mutex,
	.drv_data = &g_kx022_data,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = KX022_ADDR0_FLAGS,
	.rot_standard_ref = &lid_standard_ref,
	.min_frequency = KX022_ACCEL_MIN_FREQ,
	.max_frequency = KX022_ACCEL_MAX_FREQ,
	.default_range = 2, /* g, to support tablet mode */
	.config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	},
};

struct motion_sensor_t icm426xx_base_accel = {
	.name = "Base Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_ICM426XX,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &icm426xx_drv,
	.mutex = &g_base_mutex,
	.drv_data = &g_icm426xx_data,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
	.default_range = 4, /* g, to meet CDD 7.3.1/C-1-4 reqs.*/
	.rot_standard_ref = &base_icm_ref,
	.min_frequency = ICM426XX_ACCEL_MIN_FREQ,
	.max_frequency = ICM426XX_ACCEL_MAX_FREQ,
	.config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 13000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		},
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		},
	},
};

struct motion_sensor_t icm426xx_base_gyro = {
	.name = "Base Gyro",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_ICM426XX,
	.type = MOTIONSENSE_TYPE_GYRO,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &icm426xx_drv,
	.mutex = &g_base_mutex,
	.drv_data = &g_icm426xx_data,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
	.default_range = 1000, /* dps */
	.rot_standard_ref = &base_icm_ref,
	.min_frequency = ICM426XX_GYRO_MIN_FREQ,
	.max_frequency = ICM426XX_GYRO_MAX_FREQ,
};
#endif

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMA255,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bma2x2_accel_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_bma253_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMA2x2_I2C_ADDR1_FLAGS,
		.rot_standard_ref = &lid_standard_ref,
		.default_range = 2,
		.min_frequency = BMA255_ACCEL_MIN_FREQ,
		.max_frequency = BMA255_ACCEL_MAX_FREQ,
		.config = {
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
		},
	},
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.rot_standard_ref = &base_standard_ref,
		.default_range = 4,
		.min_frequency = BMI_ACCEL_MIN_FREQ,
		.max_frequency = BMI_ACCEL_MAX_FREQ,
		.config = {
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 13000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
		},
	},
	[BASE_GYRO] = {
		.name = "Base Gyro",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.default_range = 1000, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_GYRO_MIN_FREQ,
		.max_frequency = BMI_GYRO_MAX_FREQ,
	},
};

unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/**
 * Handle debounced pen input changing state.
 */
static void pendetect_deferred(void)
{
	int pen_charge_enable = !gpio_get_level(GPIO_PEN_DET_ODL) &&
				!chipset_in_state(CHIPSET_STATE_ANY_OFF);

	if (pen_charge_enable)
		gpio_set_level(GPIO_EN_PP5000_PEN, 1);
	else
		gpio_set_level(GPIO_EN_PP5000_PEN, 0);

	CPRINTS("Pen charge %sable", pen_charge_enable ? "en" : "dis");
}
DECLARE_DEFERRED(pendetect_deferred);

void pen_detect_interrupt(enum gpio_signal signal)
{
	/* pen input debounce time */
	hook_call_deferred(&pendetect_deferred_data, (100 * MSEC));
}

static void pen_charge_check(void)
{
	if (get_cbi_fw_config_stylus() == STYLUS_PRESENT)
		hook_call_deferred(&pendetect_deferred_data, (100 * MSEC));
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, pen_charge_check, HOOK_PRIO_LAST);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, pen_charge_check, HOOK_PRIO_LAST);

/*****************************************************************************
 * USB-C MUX/Retimer dynamic configuration
 */
struct usb_mux usbc1_mux0 = {
	.usb_port = 1,
	.i2c_port = I2C_PORT_SUB_USB_C1,
	.i2c_addr_flags = PS8802_I2C_ADDR_FLAGS_CUSTOM,
	.driver = &ps8802_usb_mux_driver,
};

static void setup_mux(void)
{
	if (get_cbi_ssfc_usb_mux() == SSFC_USBMUX_PS8743) {
		usbc1_mux0.i2c_addr_flags = PS8743_I2C_ADDR0_FLAG;
		usbc1_mux0.driver = &ps8743_usb_mux_driver;
		ccprints("PS8743 USB MUX");
	} else
		ccprints("PS8762 USB MUX");
}

void board_init(void)
{
	int on;

	gpio_enable_interrupt(GPIO_USB_C0_INT_ODL);
	check_c0_line();

	if (get_cbi_fw_config_db() == DB_1A_HDMI) {
		/* Disable i2c on HDMI pins */
		gpio_config_pin(MODULE_I2C, GPIO_EC_I2C_SUB_C1_SDA_HDMI_HPD_ODL,
				0);
		gpio_config_pin(MODULE_I2C, GPIO_EC_I2C_SUB_C1_SCL_HDMI_EN_ODL,
				0);

		/* Set HDMI and sub-rail enables to output */
		gpio_set_flags(GPIO_EC_I2C_SUB_C1_SCL_HDMI_EN_ODL,
			       chipset_in_state(CHIPSET_STATE_ON) ?
				       GPIO_ODR_LOW :
				       GPIO_ODR_HIGH);
		gpio_set_flags(GPIO_SUB_C1_INT_EN_RAILS_ODL, GPIO_ODR_HIGH);

		/* Select HDMI option */
		gpio_set_level(GPIO_HDMI_SEL_L, 0);

		/* Enable interrupt for passing through HPD */
		gpio_enable_interrupt(GPIO_EC_I2C_SUB_C1_SDA_HDMI_HPD_ODL);

	} else {
		/* Set SDA as an input */
		gpio_set_flags(GPIO_EC_I2C_SUB_C1_SDA_HDMI_HPD_ODL, GPIO_INPUT);

		/* Enable C1 interrupt and check if it needs processing */
		gpio_enable_interrupt(GPIO_SUB_C1_INT_EN_RAILS_ODL);
		check_c1_line();
	}

	setup_mux();

	/* Enable gpio interrupt for base accelgyro sensor */
	gpio_enable_interrupt(GPIO_BASE_SIXAXIS_INT_L);
	if (get_cbi_fw_config_tablet_mode()) {
#ifdef BOARD_MAGOLOR
		if (get_cbi_ssfc_base_sensor() == SSFC_SENSOR_ICM426XX) {
			motion_sensors[BASE_ACCEL] = icm426xx_base_accel;
			motion_sensors[BASE_GYRO] = icm426xx_base_gyro;
			ccprints("BASE GYRO is ICM426XX");
		} else
			ccprints("BASE GYRO is BMI160");

		if (get_cbi_ssfc_lid_sensor() == SSFC_SENSOR_KX022) {
			motion_sensors[LID_ACCEL] = kx022_lid_accel;
			ccprints("LID_ACCEL is KX022");
		} else {
			if (system_get_board_version() >= 5) {
				motion_sensors[LID_ACCEL].rot_standard_ref =
					&lid_magister_ref;
			}
			ccprints("LID_ACCEL is BMA253");
		}
#endif
		motion_sensor_count = ARRAY_SIZE(motion_sensors);
		/* Enable gpio interrupt for base accelgyro sensor */
		gpio_enable_interrupt(GPIO_BASE_SIXAXIS_INT_L);
	} else {
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
		/* Base accel is not stuffed, don't allow line to float */
		gpio_set_flags(GPIO_BASE_SIXAXIS_INT_L,
			       GPIO_INPUT | GPIO_PULL_DOWN);
	}

	if (get_cbi_fw_config_stylus() == STYLUS_PRESENT) {
		gpio_enable_interrupt(GPIO_PEN_DET_ODL);
		/* Make sure pen detection is triggered or not at sysjump */
		pen_charge_check();
	} else {
		gpio_disable_interrupt(GPIO_PEN_DET_ODL);
		gpio_set_flags(GPIO_PEN_DET_ODL, GPIO_INPUT | GPIO_PULL_DOWN);
	}

	/* Turn on 5V if the system is on, otherwise turn it off. */
	on = chipset_in_state(CHIPSET_STATE_ON | CHIPSET_STATE_ANY_SUSPEND |
			      CHIPSET_STATE_SOFT_OFF);
	board_power_5v_enable(on);

	/* Initialize THERMAL */
	setup_thermal();

#ifdef BOARD_MAGOLOR
	/* Support Keyboard Pad */
	board_update_no_keypad_by_fwconfig();
#endif
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void motion_interrupt(enum gpio_signal signal)
{
#ifdef BOARD_MAGOLOR
	switch (get_cbi_ssfc_base_sensor()) {
	case SSFC_SENSOR_ICM426XX:
		icm426xx_interrupt(signal);
		break;
	case SSFC_SENSOR_BMI160:
	default:
		bmi160_interrupt(signal);
		break;
	}
#else
	bmi160_interrupt(signal);
#endif
}

__override void ocpc_get_pid_constants(int *kp, int *kp_div, int *ki,
				       int *ki_div, int *kd, int *kd_div)
{
	*kp = 1;
	*kp_div = 20;
	*ki = 1;
	*ki_div = 250;
	*kd = 0;
	*kd_div = 1;
}

int pd_snk_is_vbus_provided(int port)
{
	return pd_check_vbus_level(port, VBUS_PRESENT);
}

const struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},

	{
		.i2c_port = I2C_PORT_SUB_USB_C1,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};
const unsigned int chg_cnt = ARRAY_SIZE(chg_chips);

const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	{
		.i2c_port = I2C_PORT_USB_C0,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
		.flags = PI3USB9201_ALWAYS_POWERED,
	},

	{
		.i2c_port = I2C_PORT_SUB_USB_C1,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
		.flags = PI3USB9201_ALWAYS_POWERED,
	},
};

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = {
		.channel = 3,
		.flags = PWM_CONFIG_DSLEEP,
		.freq = 10000,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C0,
			.addr_flags = RAA489000_TCPC0_I2C_FLAGS,
		},
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
		.drv = &raa489000_tcpm_drv,
	},

	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_SUB_USB_C1,
			.addr_flags = RAA489000_TCPC0_I2C_FLAGS,
		},
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
		.drv = &raa489000_tcpm_drv,
	},
};

struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USBC_PORT_C0] = {
		.mux = &(const struct usb_mux) {
			.usb_port = 0,
			.i2c_port = I2C_PORT_USB_C0,
			.i2c_addr_flags = PI3USB3X532_I2C_ADDR0,
			.driver = &pi3usb3x532_usb_mux_driver,
		},
	},
	[USBC_PORT_C1] = {
		.mux = &usbc1_mux0,
	}
};

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;
	int regval;

	/*
	 * The interrupt line is shared between the TCPC and BC1.2 detector IC.
	 * Therefore, go out and actually read the alert registers to report the
	 * alert status.
	 */
	if (!gpio_get_level(GPIO_USB_C0_INT_ODL)) {
		if (!tcpc_read16(0, TCPC_REG_ALERT, &regval)) {
			/* The TCPCI Rev 1.0 spec says to ignore bits 14:12. */
			if (!(tcpc_config[0].flags & TCPC_FLAGS_TCPCI_REV2_0))
				regval &= ~((1 << 14) | (1 << 13) | (1 << 12));

			if (regval)
				status |= PD_STATUS_TCPC_ALERT_0;
		}
	}

	if (board_get_usb_pd_port_count() > 1 &&
	    !gpio_get_level(GPIO_SUB_C1_INT_EN_RAILS_ODL)) {
		if (!tcpc_read16(1, TCPC_REG_ALERT, &regval)) {
			/* TCPCI spec Rev 1.0 says to ignore bits 14:12. */
			if (!(tcpc_config[1].flags & TCPC_FLAGS_TCPCI_REV2_0))
				regval &= ~((1 << 14) | (1 << 13) | (1 << 12));

			if (regval)
				status |= PD_STATUS_TCPC_ALERT_1;
		}
	}

	return status;
}

int adc_to_physical_value(enum gpio_signal gpio)
{
	if (gpio == GPIO_VOLUME_UP_L)
		return !!(new_adc_key_state & ADC_VOL_UP_MASK);
	else if (gpio == GPIO_VOLUME_DOWN_L)
		return !!(new_adc_key_state & ADC_VOL_DOWN_MASK);

	CPRINTS("Not a volume up or down key");
	return 0;
}

int button_is_adc_detected(enum gpio_signal gpio)
{
	return (gpio == GPIO_VOLUME_DOWN_L) || (gpio == GPIO_VOLUME_UP_L);
}

static void adc_vol_key_press_check(void)
{
	int volt = adc_read_channel(ADC_SUB_ANALOG);
	static uint8_t old_adc_key_state;
	uint8_t adc_key_state_change;

	if (volt > 2400 && volt < 2540) {
		/* volume-up is pressed */
		new_adc_key_state = ADC_VOL_UP_MASK;
	} else if (volt > 2600 && volt < 2740) {
		/* volume-down is pressed */
		new_adc_key_state = ADC_VOL_DOWN_MASK;
	} else if (volt < 2300) {
		/* both volumn-up and volume-down are pressed */
		new_adc_key_state = ADC_VOL_UP_MASK | ADC_VOL_DOWN_MASK;
	} else if (volt > 2780) {
		/* both volumn-up and volume-down are released */
		new_adc_key_state = 0;
	}
	if (new_adc_key_state != old_adc_key_state) {
		adc_key_state_change = old_adc_key_state ^ new_adc_key_state;
		if (adc_key_state_change & ADC_VOL_UP_MASK)
			button_interrupt(GPIO_VOLUME_UP_L);
		if (adc_key_state_change & ADC_VOL_DOWN_MASK)
			button_interrupt(GPIO_VOLUME_DOWN_L);

		old_adc_key_state = new_adc_key_state;
	}
}
DECLARE_HOOK(HOOK_TICK, adc_vol_key_press_check, HOOK_PRIO_DEFAULT);

/* This callback disables keyboard when convertibles are fully open */
__override void lid_angle_peripheral_enable(int enable)
{
	int chipset_in_s0 = chipset_in_state(CHIPSET_STATE_ON);

	/*
	 * If the lid is in tablet position via other sensors,
	 * ignore the lid angle, which might be faulty then
	 * disable keyboard.
	 */
	if (tablet_get_mode())
		enable = 0;

	if (enable) {
		keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_ANGLE);
	} else {
		/*
		 * Ensure that the chipset is off before disabling the keyboard.
		 * When the chipset is on, the EC keeps the keyboard enabled and
		 * the AP decides whether to ignore input devices or not.
		 */
		if (!chipset_in_s0)
			keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_ANGLE);
	}
}
