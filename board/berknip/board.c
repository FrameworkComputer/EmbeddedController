/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Berknip board configuration */

#include "adc.h"
#include "button.h"
#include "cbi_ec_fw_config.h"
#include "charger.h"
#include "cros_board_info.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/retimer/pi3hdx1204.h"
#include "driver/retimer/tusb544.h"
#include "driver/temp_sensor/sb_tsi.h"
#include "driver/usb_mux/amd_fp5.h"
#include "driver/usb_mux/ps8743.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "usb_charge.h"
#include "usb_mux.h"

static void hdmi_hpd_interrupt(enum gpio_signal signal);

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = {
		.channel = 3,
		.flags = PWM_CONFIG_DSLEEP,
		.freq = 100,
	},
	[PWM_CH_FAN] = {
		.channel = 2,
		.flags = PWM_CONFIG_OPEN_DRAIN,
		.freq = 25000,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = {
		.module = NPCX_MFT_MODULE_1,
		.clk_src = TCKC_LFCLK,
		.pwm_id = PWM_CH_FAN,
	},
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

const int usb_port_enable[USBA_PORT_COUNT] = {
	IOEX_EN_USB_A0_5V,
	IOEX_EN_USB_A1_5V_DB,
};

const struct pi3hdx1204_tuning pi3hdx1204_tuning = {
	.eq_ch0_ch1_offset = PI3HDX1204_EQ_DB710,
	.eq_ch2_ch3_offset = PI3HDX1204_EQ_DB710,
	.vod_offset = PI3HDX1204_VOD_115_ALL_CHANNELS,
	.de_offset = PI3HDX1204_DE_DB_MINUS5,
};

static int check_hdmi_hpd_status(void)
{
	return gpio_get_level(GPIO_DP1_HPD_EC_IN);
}

/*****************************************************************************
 * Board suspend / resume
 */

static void board_chipset_resume(void)
{
	ioex_set_level(IOEX_HDMI_DATA_EN_DB, 1);

	if (ec_config_has_hdmi_retimer_pi3hdx1204()) {
		ioex_set_level(IOEX_HDMI_POWER_EN_DB, 1);
		msleep(PI3HDX1204_POWER_ON_DELAY_MS);
		pi3hdx1204_enable(I2C_PORT_TCPC1, PI3HDX1204_I2C_ADDR_FLAGS,
				  check_hdmi_hpd_status());
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

static void board_chipset_suspend(void)
{
	if (ec_config_has_hdmi_retimer_pi3hdx1204()) {
		pi3hdx1204_enable(I2C_PORT_TCPC1, PI3HDX1204_I2C_ADDR_FLAGS, 0);
		ioex_set_level(IOEX_HDMI_POWER_EN_DB, 0);
	}

	ioex_set_level(IOEX_HDMI_DATA_EN_DB, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/*
 * USB C0 port SBU mux use standalone PI3USB221
 * chip and it need a board specific driver.
 * Overall, it will use chained mux framework.
 */
static int pi3usb221_set_mux(const struct usb_mux *me, mux_state_t mux_state,
			     bool *ack_required)
{
	/* This driver does not use host command ACKs */
	*ack_required = false;

	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		ioex_set_level(IOEX_USB_C0_SBU_FLIP, 0);
	else
		ioex_set_level(IOEX_USB_C0_SBU_FLIP, 1);
	return EC_SUCCESS;
}
/*
 * .init is not necessary here because it has nothing
 * to do. Primary mux will handle mux state so .get is
 * not needed as well. usb_mux.c can handle the situation
 * properly.
 */
const struct usb_mux_driver usbc0_sbu_mux_driver = {
	.set = pi3usb221_set_mux,
};
/*
 * Since PI3USB221 is not a i2c device, .i2c_port and
 * .i2c_addr_flags are not required here.
 */
const struct usb_mux_chain usbc0_sbu_mux = {
	.mux =
		&(const struct usb_mux){
			.usb_port = USBC_PORT_C0,
			.driver = &usbc0_sbu_mux_driver,
		},
};

/*****************************************************************************
 * USB-C MUX/Retimer dynamic configuration
 */

/* Place holder for second mux in USBC1 chain */
struct usb_mux_chain usbc1_mux1;

static void setup_mux(void)
{
	if (ec_config_has_usbc1_retimer_tusb544()) {
		ccprints("C1 TUSB544 detected");
		/*
		 * Main MUX is FP5, secondary MUX is TUSB544
		 *
		 * Replace usb_muxes[USBC_PORT_C1] with the AMD FP5
		 * table entry.
		 */
		usb_muxes[USBC_PORT_C1].mux = &usbc1_amd_fp5_usb_mux;
		/* Set the TUSB544 as the secondary MUX */
		usbc1_mux1.mux = &usbc1_tusb544;
	} else if (ec_config_has_usbc1_retimer_ps8743()) {
		ccprints("C1 PS8743 detected");
		/*
		 * Main MUX is PS8743, secondary MUX is modified FP5
		 *
		 * Replace usb_muxes[USBC_PORT_C1] with the PS8743
		 * table entry.
		 */
		usb_muxes[USBC_PORT_C1].mux = &usbc1_ps8743;
		/* Set the AMD FP5 as the secondary MUX */
		usbc1_mux1.mux = &usbc1_amd_fp5_usb_mux;
		/* Don't have the AMD FP5 flip */
		usbc1_amd_fp5_usb_mux.flags = USB_MUX_FLAG_SET_WITHOUT_FLIP;
	}
}

struct usb_mux_chain usb_muxes[] = {
	[USBC_PORT_C0] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USBC_PORT_C0,
			.i2c_port = I2C_PORT_USB_AP_MUX,
			.i2c_addr_flags = AMD_FP5_MUX_I2C_ADDR_FLAGS,
			.driver = &amd_fp5_usb_mux_driver,
		},
		.next = &usbc0_sbu_mux,
	},
	[USBC_PORT_C1] = {
		/* Filled in dynamically at startup */
		.next = &usbc1_mux1,
	},
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

static int board_tusb544_mux_set(const struct usb_mux *me,
				 mux_state_t mux_state)
{
	int rv = EC_SUCCESS;

	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		rv = tusb544_i2c_field_update8(
			me, TUSB544_REG_USB3_1_1, TUSB544_EQ_RX_MASK,
			TUSB544_EQ_RX_DFP_04_UFP_MINUS15);
		if (rv)
			return rv;

		rv = tusb544_i2c_field_update8(
			me, TUSB544_REG_USB3_1_1, TUSB544_EQ_TX_MASK,
			TUSB544_EQ_TX_DFP_MINUS14_UFP_MINUS33);
		if (rv)
			return rv;

		rv = tusb544_i2c_field_update8(
			me, TUSB544_REG_USB3_1_2, TUSB544_EQ_RX_MASK,
			TUSB544_EQ_RX_DFP_04_UFP_MINUS15);
		if (rv)
			return rv;

		rv = tusb544_i2c_field_update8(
			me, TUSB544_REG_USB3_1_2, TUSB544_EQ_TX_MASK,
			TUSB544_EQ_TX_DFP_MINUS14_UFP_MINUS33);
		if (rv)
			return rv;
	}

	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		rv = tusb544_i2c_field_update8(me, TUSB544_REG_DISPLAYPORT_1,
					       TUSB544_EQ_RX_MASK,
					       TUSB544_EQ_RX_DFP_61_UFP_43);
		if (rv)
			return rv;

		rv = tusb544_i2c_field_update8(me, TUSB544_REG_DISPLAYPORT_1,
					       TUSB544_EQ_TX_MASK,
					       TUSB544_EQ_TX_DFP_61_UFP_43);
		if (rv)
			return rv;

		rv = tusb544_i2c_field_update8(me, TUSB544_REG_DISPLAYPORT_2,
					       TUSB544_EQ_RX_MASK,
					       TUSB544_EQ_RX_DFP_61_UFP_43);
		if (rv)
			return rv;

		rv = tusb544_i2c_field_update8(me, TUSB544_REG_DISPLAYPORT_2,
					       TUSB544_EQ_TX_MASK,
					       TUSB544_EQ_TX_DFP_61_UFP_43);
		if (rv)
			return rv;

		/* Enable IN_HPD on the DB */
		gpio_or_ioex_set_level(board_usbc1_retimer_inhpd, 1);
	} else {
		/* Disable IN_HPD on the DB */
		gpio_or_ioex_set_level(board_usbc1_retimer_inhpd, 0);
	}
	return EC_SUCCESS;
}

const struct usb_mux usbc1_tusb544 = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_TCPC1,
	.i2c_addr_flags = TUSB544_I2C_ADDR_FLAGS1,
	.driver = &tusb544_drv,
	.board_set = &board_tusb544_mux_set,
};
const struct usb_mux usbc1_ps8743 = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_TCPC1,
	.i2c_addr_flags = PS8743_I2C_ADDR1_FLAG,
	.driver = &ps8743_usb_mux_driver,
};

/*****************************************************************************
 * Use FW_CONFIG to set correct configuration.
 */
enum gpio_signal GPIO_S0_PGOOD = GPIO_S0_PWROK_OD_V0;
static uint32_t board_ver;
int board_usbc1_retimer_inhpd = GPIO_USB_C1_HPD_IN_DB_V1;

static void board_version_check(void)
{
	cbi_get_board_version(&board_ver);

	if (board_ver <= 2)
		chg_chips[0].i2c_port = I2C_PORT_CHARGER_V0;

	if (board_ver == 2) {
		power_signal_list[X86_S0_PGOOD].gpio = GPIO_S0_PWROK_OD_V1;
		GPIO_S0_PGOOD = GPIO_S0_PWROK_OD_V1;
	}
}
/*
 * Use HOOK_PRIO_INIT_I2C so we re-map before charger_chips_init()
 * talks to the charger.
 */
DECLARE_HOOK(HOOK_INIT, board_version_check, HOOK_PRIO_INIT_I2C);

static void board_remap_gpio(void)
{
	if (board_ver >= 3) {
		/*
		 * TODO: remove code when older version_2
		 * hardware is retired and no longer needed
		 */
		gpio_set_flags(GPIO_USB_C1_HPD_IN_DB_V1, GPIO_OUT_LOW);
		board_usbc1_retimer_inhpd = GPIO_USB_C1_HPD_IN_DB_V1;

		if (ec_config_has_hdmi_retimer_pi3hdx1204())
			gpio_enable_interrupt(GPIO_DP1_HPD_EC_IN);

	} else
		board_usbc1_retimer_inhpd = IOEX_USB_C1_HPD_IN_DB;
}

static void setup_fw_config(void)
{
	setup_mux();

	board_remap_gpio();
}
/* Use HOOK_PRIO_INIT_I2C + 2 to be after ioex_init(). */
DECLARE_HOOK(HOOK_INIT, setup_fw_config, HOOK_PRIO_INIT_I2C + 2);

static void hdmi_hpd_handler(void)
{
	/* Pass HPD through DB OPT1 HDMI connector to AP's DP1 */
	int hpd = check_hdmi_hpd_status();

	gpio_set_level(GPIO_EC_DP1_HPD, hpd);
	ccprints("HDMI HPD %d", hpd);
	pi3hdx1204_enable(
		I2C_PORT_TCPC1, PI3HDX1204_I2C_ADDR_FLAGS,
		chipset_in_or_transitioning_to_state(CHIPSET_STATE_ON) && hpd);
}
DECLARE_DEFERRED(hdmi_hpd_handler);

static void hdmi_hpd_interrupt(enum gpio_signal signal)
{
	/* Debounce for 2 msec */
	hook_call_deferred(&hdmi_hpd_handler_data, (2 * MSEC));
}

/*****************************************************************************
 * Fan
 */

/* Physical fans. These are logically separate from pwm_channels. */
const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0, /* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};
const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 3000,
	.rpm_start = 3500,
	.rpm_max = 6200,
};
const struct fan_t fans[] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == FAN_CH_COUNT);

int board_get_temp(int idx, int *temp_k)
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

		/* adc power not ready when transition to S5 */
		if (chipset_in_or_transitioning_to_state(
			    CHIPSET_STATE_SOFT_OFF))
			return EC_ERROR_NOT_POWERED;

		channel = ADC_TEMP_SENSOR_SOC;
		break;
	case TEMP_SENSOR_5V_REGULATOR:
		/* thermistor is not powered in G3 */
		if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
			return EC_ERROR_NOT_POWERED;

		/* adc power not ready when transition to S5 */
		if (chipset_in_or_transitioning_to_state(
			    CHIPSET_STATE_SOFT_OFF))
			return EC_ERROR_NOT_POWERED;

		channel = ADC_TEMP_SENSOR_5V_REGULATOR;
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

const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_5V_REGULATOR] = {
		.name = "5V_REGULATOR",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_CHARGER] = {
		.name = "CHARGER",
		.input_ch = NPCX_ADC_CH2,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_SOC] = {
		.name = "SOC",
		.input_ch = NPCX_ADC_CH3,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_CHARGER] = {
		.name = "Charger",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_temp,
		.idx = TEMP_SENSOR_CHARGER,
	},
	[TEMP_SENSOR_SOC] = {
		.name = "SOC",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_temp,
		.idx = TEMP_SENSOR_SOC,
	},
	[TEMP_SENSOR_CPU] = {
		.name = "CPU",
		.type = TEMP_SENSOR_TYPE_CPU,
		.read = sb_tsi_get_val,
		.idx = 0,
	},
	[TEMP_SENSOR_5V_REGULATOR] = {
		.name = "5V_REGULATOR",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = board_get_temp,
		.idx = TEMP_SENSOR_5V_REGULATOR,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

const static struct ec_thermal_config thermal_thermistor_soc = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(62),
		[EC_TEMP_THRESH_HALT] = C_TO_K(66),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(57),
	},
	.temp_fan_off = C_TO_K(39),
	.temp_fan_max = C_TO_K(60),
};

const static struct ec_thermal_config thermal_thermistor_charger = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(99),
		[EC_TEMP_THRESH_HALT] = C_TO_K(99),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(98),
	},
	.temp_fan_off = C_TO_K(98),
	.temp_fan_max = C_TO_K(99),
};

const static struct ec_thermal_config thermal_thermistor_5v = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(60),
		[EC_TEMP_THRESH_HALT] = C_TO_K(99),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(50),
	},
	.temp_fan_off = C_TO_K(98),
	.temp_fan_max = C_TO_K(99),
};

const static struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(100),
		[EC_TEMP_THRESH_HALT] = C_TO_K(105),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(99),
	},
};

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

struct fan_step {
	int on;
	int off;
	int rpm;
};

static const struct fan_step fan_table0[] = {
	{ .on = 0, .off = 5, .rpm = 0 },
	{ .on = 29, .off = 5, .rpm = 3700 },
	{ .on = 38, .off = 19, .rpm = 4000 },
	{ .on = 48, .off = 33, .rpm = 4500 },
	{ .on = 62, .off = 43, .rpm = 4800 },
	{ .on = 76, .off = 52, .rpm = 5200 },
	{ .on = 100, .off = 67, .rpm = 6200 },
};
/* All fan tables must have the same number of levels */
#define NUM_FAN_LEVELS ARRAY_SIZE(fan_table0)

static const struct fan_step *fan_table = fan_table0;

int fan_percent_to_rpm(int fan, int pct)
{
	static int current_level;
	static int previous_pct;
	int i;

	/*
	 * Compare the pct and previous pct, we have the three paths :
	 *  1. decreasing path. (check the off point)
	 *  2. increasing path. (check the on point)
	 *  3. invariant path. (return the current RPM)
	 */
	if (pct < previous_pct) {
		for (i = current_level; i >= 0; i--) {
			if (pct <= fan_table[i].off)
				current_level = i - 1;
			else
				break;
		}
	} else if (pct > previous_pct) {
		for (i = current_level + 1; i < NUM_FAN_LEVELS; i++) {
			if (pct >= fan_table[i].on)
				current_level = i;
			else
				break;
		}
	}

	if (current_level < 0)
		current_level = 0;

	previous_pct = pct;

	if (fan_table[current_level].rpm != fan_get_rpm_target(FAN_CH(fan)))
		cprints(CC_THERMAL, "Setting fan RPM to %d",
			fan_table[current_level].rpm);

	return fan_table[current_level].rpm;
}

static void setup_fans(void)
{
	thermal_params[TEMP_SENSOR_CHARGER] = thermal_thermistor_charger;
	thermal_params[TEMP_SENSOR_SOC] = thermal_thermistor_soc;
	thermal_params[TEMP_SENSOR_CPU] = thermal_cpu;
	thermal_params[TEMP_SENSOR_5V_REGULATOR] = thermal_thermistor_5v;
}
DECLARE_HOOK(HOOK_INIT, setup_fans, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_KEYBOARD_FACTORY_TEST
/*
 * Map keyboard connector pins to EC GPIO pins for factory test.
 * Pins mapped to {-1, -1} are skipped.
 * The connector has 24 pins total, and there is no pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ 0, 5 }, { 1, 1 }, { 1, 0 }, { 0, 6 },	  { 0, 7 },   { 1, 4 },
	{ 1, 3 }, { 1, 6 }, { 1, 7 }, { 3, 1 },	  { 2, 0 },   { 1, 5 },
	{ 2, 6 }, { 2, 7 }, { 2, 1 }, { 2, 4 },	  { 2, 5 },   { 1, 2 },
	{ 2, 3 }, { 2, 2 }, { 3, 0 }, { -1, -1 }, { -1, -1 }, { -1, -1 },
};

const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);
#endif

/*****************************************************************************
 * Power signals
 */

struct power_signal_info power_signal_list[] = {
	[X86_SLP_S3_N] = {
		.gpio = GPIO_PCH_SLP_S3_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S3_DEASSERTED",
	},
	[X86_SLP_S5_N] = {
		.gpio = GPIO_PCH_SLP_S5_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S5_DEASSERTED",
	},
	[X86_S0_PGOOD] = {
		.gpio = GPIO_S0_PWROK_OD_V0,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "S0_PGOOD",
	},
	[X86_S5_PGOOD] = {
		.gpio = GPIO_S5_PGOOD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "S5_PGOOD",
	},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

enum gpio_signal board_usbc_port_to_hpd_gpio(int port)
{
	/* USB-C0 always uses USB_C0_HPD (= DP3_HPD). */
	if (port == 0)
		return GPIO_USB_C0_HPD;

	/*
	 * USB-C1 OPT3 DB
	 *    version_2 uses GPIO_NO_HPD
	 *    version_3 uses USB_C1_HPD_IN_DB_V1 via RTD2141B MST hub
	 *    to drive AP HPD, EC drives MST hub HPD input
	 *    from USB-PD messages..
	 */
	else if (ec_config_has_mst_hub_rtd2141b())
		return (board_ver >= 3) ? GPIO_USB_C1_HPD_IN_DB_V1 :
					  GPIO_NO_HPD;

	/* USB-C1 OPT1 DB uses DP2_HPD. */
	return GPIO_DP2_HPD;
}
