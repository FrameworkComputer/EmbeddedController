/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "adc_chip.h"
#include "button.h"
#include "charge_state_v2.h"
#include "cros_board_info.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
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
#include "usb_charge.h"
#include "usb_mux.h"

#include "gpio_list.h"

#ifdef HAS_TASK_MOTIONSENSE

static int board_ver;

/* Motion sensors */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* sensor private data */
static struct kionix_accel_data g_kx022_data;
static struct bmi_drv_data_t g_bmi160_data;

/* Matrix to rotate accelrator into standard reference frame */
const mat33_fp_t base_standard_ref = {
	{ 0, FLOAT_TO_FP(-1), 0},
	{ FLOAT_TO_FP(-1), 0,  0},
	{ 0, 0,  FLOAT_TO_FP(-1)}
};
const mat33_fp_t lid_standard_ref = {
	{ FLOAT_TO_FP(-1), 0,  0},
	{ 0, FLOAT_TO_FP(-1), 0},
	{ 0, 0,  FLOAT_TO_FP(1)}
};

/* TODO(gcc >= 5.0) Remove the casts to const pointer at rot_standard_ref */
struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
	 .name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_KX022,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &kionix_accel_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_kx022_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = KX022_ADDR1_FLAGS,
	 .rot_standard_ref = &lid_standard_ref,
	 .default_range = 2, /* g, enough for laptop. */
	 .min_frequency = KX022_ACCEL_MIN_FREQ,
	 .max_frequency = KX022_ACCEL_MAX_FREQ,
	 .config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100,
		 },
		/* EC use accel for angle detection */
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
	 .default_range = 2, /* g, enough for laptop */
	 .rot_standard_ref = &base_standard_ref,
	 .min_frequency = BMI_ACCEL_MIN_FREQ,
	 .max_frequency = BMI_ACCEL_MAX_FREQ,
	 .config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100,
		 },
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
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

#endif /* HAS_TASK_MOTIONSENSE */

const struct power_signal_info power_signal_list[] = {
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
		.gpio = GPIO_S0_PGOOD,
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

/*****************************************************************************
 * Retimers
 */

static void retimers_on(void)
{
	ioex_set_level(IOEX_USB_A1_RETIMER_EN, 1);

	/* hdmi retimer power on */
	ioex_set_level(IOEX_HDMI_POWER_EN_DB, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, retimers_on, HOOK_PRIO_DEFAULT);

static void retimers_off(void)
{
	ioex_set_level(IOEX_USB_A1_RETIMER_EN, 0);

	/* hdmi retimer power off */
	ioex_set_level(IOEX_HDMI_POWER_EN_DB, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, retimers_off, HOOK_PRIO_DEFAULT);

/*
 * USB C0 port SBU mux use standalone FSUSB42UMX
 * chip and it need a board specific driver.
 * Overall, it will use chained mux framework.
 */
static int fsusb42umx_set_mux(const struct usb_mux *me, mux_state_t mux_state)
{
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		ioex_set_level(IOEX_USB_C0_SBU_FLIP, 1);
	else
		ioex_set_level(IOEX_USB_C0_SBU_FLIP, 0);
	return EC_SUCCESS;
}
/*
 * .init is not necessary here because it has nothing
 * to do. Primary mux will handle mux state so .get is
 * not needed as well. usb_mux.c can handle the situation
 * properly.
 */
const struct usb_mux_driver usbc0_sbu_mux_driver = {
	.set = fsusb42umx_set_mux,
};
/*
 * Since FSUSB42UMX is not a i2c device, .i2c_port and
 * .i2c_addr_flags are not required here.
 */
const struct usb_mux usbc0_sbu_mux = {
	.usb_port = USBC_PORT_C0,
	.driver = &usbc0_sbu_mux_driver,
};

/*****************************************************************************
 * USB-C MUX/Retimer dynamic configuration
 */

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
		memcpy(&usb_muxes[USBC_PORT_C1],
		       &usbc1_amd_fp5_usb_mux,
		       sizeof(struct usb_mux));
		/* Set the TUSB544 as the secondary MUX */
		usb_muxes[USBC_PORT_C1].next_mux = &usbc1_tusb544;
	} else if (ec_config_has_usbc1_retimer_ps8743()) {
		ccprints("C1 PS8743 detected");
		/*
		 * Main MUX is PS8743, secondary MUX is modified FP5
		 *
		 * Replace usb_muxes[USBC_PORT_C1] with the PS8743
		 * table entry.
		 */
		memcpy(&usb_muxes[USBC_PORT_C1],
		       &usbc1_ps8743,
		       sizeof(struct usb_mux));
		/* Set the AMD FP5 as the secondary MUX */
		usb_muxes[USBC_PORT_C1].next_mux = &usbc1_amd_fp5_usb_mux;
		/* Don't have the AMD FP5 flip */
		usbc1_amd_fp5_usb_mux.flags = USB_MUX_FLAG_SET_WITHOUT_FLIP;
	}
}

struct usb_mux usb_muxes[] = {
	[USBC_PORT_C0] = {
		.usb_port = USBC_PORT_C0,
		.i2c_port = I2C_PORT_USB_AP_MUX,
		.i2c_addr_flags = AMD_FP5_MUX_I2C_ADDR_FLAGS,
		.driver = &amd_fp5_usb_mux_driver,
		.next_mux = &usbc0_sbu_mux,
	},
	[USBC_PORT_C1] = {
		/* Filled in dynamically at startup */
	},
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

static int board_tusb544_mux_set(const struct usb_mux *me,
				mux_state_t mux_state)
{
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Enable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 1);
	} else {
		/* Disable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 0);
	}
	return EC_SUCCESS;
}

static int board_ps8743_mux_set(const struct usb_mux *me,
				mux_state_t mux_state)
{
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		/* Enable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 1);
	else
		/* Disable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 0);

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
	.board_set = &board_ps8743_mux_set,
};

/*****************************************************************************
 * Use FW_CONFIG to set correct configuration.
 */

void setup_fw_config(void)
{
	int rv;

	rv = cbi_get_board_version(&board_ver);
	if (rv) {
		ccprints("Fail to get board_ver");
		/* Default for v3 */
		board_ver = 3;
	}

	/* Enable Gyro interrupts */
	gpio_enable_interrupt(GPIO_6AXIS_INT_L);

	setup_mux();

	if (ec_config_has_hdmi_conn_hpd()) {
		if (board_ver < 3)
			ioex_enable_interrupt(IOEX_HDMI_CONN_HPD_3V3_DB);
		else
			gpio_enable_interrupt(GPIO_DP1_HPD_EC_IN);
	}
}
DECLARE_HOOK(HOOK_INIT, setup_fw_config, HOOK_PRIO_INIT_I2C + 2);

__override int check_hdmi_hpd_status(void)
{
	int hpd = 0;

	if (board_ver < 3)
		ioex_get_level(IOEX_HDMI_CONN_HPD_3V3_DB, &hpd);
	else
		hpd = gpio_get_level(GPIO_DP1_HPD_EC_IN);

	return hpd;
}

static void hdmi_hpd_handler(void)
{
	/* Pass HPD through from DB OPT1 HDMI connector to AP's DP1. */
	int hpd = check_hdmi_hpd_status();

	gpio_set_level(GPIO_DP1_HPD, hpd);
	ccprints("HDMI HPD %d", hpd);
	pi3hdx1204_retimer_power();
}
DECLARE_DEFERRED(hdmi_hpd_handler);

void hdmi_hpd_interrupt(enum gpio_signal signal)
{
	/* Debounce for 2 msec. */
	hook_call_deferred(&hdmi_hpd_handler_data, (2 * MSEC));
}

void hdmi_hpd_interrupt_v2(enum ioex_signal signal)
{
	/* Debounce for 2 msec. */
	hook_call_deferred(&hdmi_hpd_handler_data, (2 * MSEC));
}

/*****************************************************************************
 * Fan
 */

/* Physical fans. These are logically separate from pwm_channels. */
const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0,	/* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};
const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 2800,
	.rpm_start = 2800,
	.rpm_max = 6000,
};
const struct fan_t fans[] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == FAN_CH_COUNT);

const struct adc_t adc_channels[] = {
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
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

const static struct ec_thermal_config thermal_thermistor = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
		[EC_TEMP_THRESH_HALT] = C_TO_K(95),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(70),
	},
	.temp_fan_off = 0,
	.temp_fan_max = 0,
};

const static struct ec_thermal_config thermal_soc = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
	.temp_fan_off = 0,
	.temp_fan_max = 0,
};

const static struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
		[EC_TEMP_THRESH_HALT] = C_TO_K(95),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
	},
	.temp_fan_off = C_TO_K(37),
	.temp_fan_max = C_TO_K(90),
};

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

struct fan_step {
	int on;
	int off;
	int rpm;
};

/* Note: Do not make the fan on/off point equal to 0 or 100 */
static const struct fan_step fan_table0[] = {
	{.on =  0, .off =  2, .rpm = 0},
	{.on = 15, .off =  2, .rpm = 2800},
	{.on = 23, .off = 13, .rpm = 3200},
	{.on = 30, .off = 21, .rpm = 3400},
	{.on = 38, .off = 28, .rpm = 3700},
	{.on = 45, .off = 36, .rpm = 4200},
	{.on = 55, .off = 43, .rpm = 4500},
	{.on = 66, .off = 53, .rpm = 5300},
};
/* All fan tables must have the same number of levels */
#define NUM_FAN_LEVELS ARRAY_SIZE(fan_table0)

static const struct fan_step *fan_table = fan_table0;

static void setup_fans(void)
{
	thermal_params[TEMP_SENSOR_CHARGER] = thermal_thermistor;
	thermal_params[TEMP_SENSOR_SOC] = thermal_soc;
	thermal_params[TEMP_SENSOR_CPU] = thermal_cpu;
}
DECLARE_HOOK(HOOK_INIT, setup_fans, HOOK_PRIO_DEFAULT);

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

	if (fan_table[current_level].rpm !=
		fan_get_rpm_target(FAN_CH(fan)))
		cprints(CC_THERMAL, "Setting fan RPM to %d",
			fan_table[current_level].rpm);

	return fan_table[current_level].rpm;
}

__override void board_set_charge_limit(int port, int supplier, int charge_ma,
			int max_ma, int charge_mv)
{
	/*
	 * Limit the input current to 95% negotiated limit,
	 * to account for the charger chip margin.
	 */
	charge_ma = charge_ma * 95 / 100;

	charge_set_input_current_limit(MAX(charge_ma,
				CONFIG_CHARGER_INPUT_CURRENT),
				charge_mv);
}
