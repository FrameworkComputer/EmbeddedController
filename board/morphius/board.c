/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Morphius board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "battery_smart.h"
#include "button.h"
#include "cros_board_info.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/ppc/aoz1380.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/retimer/pi3dpx1207.h"
#include "driver/temp_sensor/sb_tsi.h"
#include "driver/temp_sensor/tmp432.h"
#include "driver/usb_mux/amd_fp5.h"
#include "extpower.h"
#include "gpio.h"
#include "fan.h"
#include "fan_chip.h"
#include "hooks.h"
#include "keyboard_8042.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "ps2_chip.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "usb_mux.h"
#include "usb_charge.h"
#include "usbc_ppc.h"

#include "gpio_list.h"

static bool support_aoz_ppc;

#ifdef HAS_TASK_MOTIONSENSE

/* Motion sensors */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

mat33_fp_t base_standard_ref = {
	{ 0, FLOAT_TO_FP(1), 0},
	{ FLOAT_TO_FP(1), 0, 0},
	{ 0, 0, FLOAT_TO_FP(-1)}
};
mat33_fp_t lid_standard_ref = {
	{ 0, FLOAT_TO_FP(1), 0},
	{ FLOAT_TO_FP(-1), 0,  0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

/* sensor private data */
static struct kionix_accel_data g_kx022_data;
static struct bmi_drv_data_t g_bmi160_data;

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
	 .rot_standard_ref = (const mat33_fp_t *)&lid_standard_ref,
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
	 .rot_standard_ref = (const mat33_fp_t *)&base_standard_ref,
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
	 .rot_standard_ref = (const mat33_fp_t *)&base_standard_ref,
	 .min_frequency = BMI_GYRO_MIN_FREQ,
	 .max_frequency = BMI_GYRO_MAX_FREQ,
	},
};

unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

#endif /* HAS_TASK_MOTIONSENSE */

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
	[PWM_CH_POWER_LED] = {
		.channel = 0,
		.flags = PWM_CONFIG_DSLEEP,
		.freq = 100,
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
 * USB-C MUX/Retimer dynamic configuration
 */
static void setup_mux(void)
{
	if (ec_config_has_usbc1_retimer_ps8802()) {
		ccprints("C1 PS8802 detected");

		/*
		 * Main MUX is PS8802, secondary MUX is modified FP5
		 *
		 * Replace usb_muxes[USBC_PORT_C1] with the PS8802
		 * table entry.
		 */
		memcpy(&usb_muxes[USBC_PORT_C1],
		       &usbc1_ps8802,
		       sizeof(struct usb_mux));

		/* Set the AMD FP5 as the secondary MUX */
		usb_muxes[USBC_PORT_C1].next_mux = &usbc1_amd_fp5_usb_mux;

		/* Don't have the AMD FP5 flip */
		usbc1_amd_fp5_usb_mux.flags = USB_MUX_FLAG_SET_WITHOUT_FLIP;

	} else if (ec_config_has_usbc1_retimer_ps8818()) {
		ccprints("C1 PS8818 detected");

		/*
		 * Main MUX is FP5, secondary MUX is PS8818
		 *
		 * Replace usb_muxes[USBC_PORT_C1] with the AMD FP5
		 * table entry.
		 */
		memcpy(&usb_muxes[USBC_PORT_C1],
		       &usbc1_amd_fp5_usb_mux,
		       sizeof(struct usb_mux));

		/* Set the PS8818 as the secondary MUX */
		usb_muxes[USBC_PORT_C1].next_mux = &usbc1_ps8818;
	}
}

const struct pi3dpx1207_usb_control pi3dpx1207_controls[] = {
	[USBC_PORT_C0] = {
		.enable_gpio = IOEX_USB_C0_DATA_EN,
		.dp_enable_gpio = GPIO_USB_C0_IN_HPD,
	},
	[USBC_PORT_C1] = {
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3dpx1207_controls) == USBC_PORT_COUNT);

const struct usb_mux usbc0_pi3dpx1207_usb_retimer = {
	.usb_port = USBC_PORT_C0,
	.i2c_port = I2C_PORT_TCPC0,
	.i2c_addr_flags = PI3DPX1207_I2C_ADDR_FLAGS,
	.driver = &pi3dpx1207_usb_retimer,
};

struct usb_mux usb_muxes[] = {
	[USBC_PORT_C0] = {
		.usb_port = USBC_PORT_C0,
		.i2c_port = I2C_PORT_USB_AP_MUX,
		.i2c_addr_flags = AMD_FP5_MUX_I2C_ADDR_FLAGS,
		.driver = &amd_fp5_usb_mux_driver,
		.next_mux = &usbc0_pi3dpx1207_usb_retimer,
	},
	[USBC_PORT_C1] = {
		/* Filled in dynamically at startup */
	},
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

/*****************************************************************************
 * Use FW_CONFIG to set correct configuration.
 */

enum gpio_signal gpio_ec_ps2_reset = GPIO_EC_PS2_RESET_V1;

static void board_remap_gpio(void)
{
	uint32_t board_ver = 0;
	int ppc_id = 0;

	cbi_get_board_version(&board_ver);

	if (board_ver >= 3) {
		gpio_ec_ps2_reset = GPIO_EC_PS2_RESET_V1;
		ccprintf("GPIO_EC_PS2_RESET_V1\n");
	} else {
		gpio_ec_ps2_reset = GPIO_EC_PS2_RESET_V0;
		ccprintf("GPIO_EC_PS2_RESET_V0\n");
	}

	ioex_get_level(IOEX_PPC_ID, &ppc_id);

	support_aoz_ppc = ((board_ver >= 3) && !ppc_id);
	if (support_aoz_ppc) {
		ccprintf("DB USBC PPC aoz1380\n");
		ppc_chips[USBC_PORT_C1].drv = &aoz1380_drv;
	}
}

void setup_fw_config(void)
{
	/* Enable Gyro interrupts */
	gpio_enable_interrupt(GPIO_6AXIS_INT_L);

	/* Enable PS2 power interrupts */
	gpio_enable_interrupt(GPIO_EN_PWR_TOUCHPAD_PS2);

	ps2_enable_channel(NPCX_PS2_CH0, 1, send_aux_data_to_host);

	setup_mux();

	board_remap_gpio();
}
DECLARE_HOOK(HOOK_INIT, setup_fw_config, HOOK_PRIO_INIT_I2C + 2);

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
	.rpm_min = 3000,
	.rpm_start = 3000,
	.rpm_max = 4900,
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
	[TEMP_SENSOR_5V_REGULATOR] = {
		.name = "5V_REGULATOR",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = tmp432_get_val,
		.idx = TMP432_IDX_LOCAL,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

const static struct ec_thermal_config thermal_thermistor = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
		[EC_TEMP_THRESH_HALT] = C_TO_K(92),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
	},
	.temp_fan_off = C_TO_K(25),
	.temp_fan_max = C_TO_K(58),
};

const static struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
		[EC_TEMP_THRESH_HALT] = C_TO_K(92),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
	},
	.temp_fan_off = C_TO_K(25),
	.temp_fan_max = C_TO_K(58),
};

struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

static void setup_fans(void)
{
	thermal_params[TEMP_SENSOR_CHARGER] = thermal_thermistor;
	thermal_params[TEMP_SENSOR_SOC] = thermal_thermistor;
	thermal_params[TEMP_SENSOR_CPU] = thermal_cpu;
}
DECLARE_HOOK(HOOK_INIT, setup_fans, HOOK_PRIO_DEFAULT);

struct fan_step {
	int on;
	int off;
	int rpm;
};

static const struct fan_step fan_table0[] = {
	{.on =  0, .off =  3, .rpm = 0},
	{.on = 32, .off =  3, .rpm = 3000},
	{.on = 65, .off = 53, .rpm = 3500},
	{.on = 74, .off = 62, .rpm = 3800},
	{.on = 82, .off = 71, .rpm = 4200},
	{.on = 91, .off = 79, .rpm = 4500},
	{.on = 100, .off = 88, .rpm = 4900},
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

	if (fan_table[current_level].rpm !=
		fan_get_rpm_target(FAN_CH(fan)))
		cprints(CC_THERMAL, "Setting fan RPM to %d",
			fan_table[current_level].rpm);

	return fan_table[current_level].rpm;
}

/* Battery functions */
#define SB_OPTIONALMFG_FUNCTION2        0x26
#define SMART_CHARGE_SUPPORT            0x01
#define SMART_CHARGE_ENABLE             0x02
#define SB_SMART_CHARGE_ENABLE          1
#define SB_SMART_CHARGE_DISABLE         0
static void sb_smart_charge_mode(int enable)
{
	int val, rv;

	rv = sb_read(SB_OPTIONALMFG_FUNCTION2, &val);
	if (rv)
		return;
	if (val & SMART_CHARGE_SUPPORT) {
		if (enable)
			val |= SMART_CHARGE_ENABLE;
		else
			val &= ~SMART_CHARGE_ENABLE;
		sb_write(SB_OPTIONALMFG_FUNCTION2, val);
	}
}
/* Called on AP S3 -> S0 transition */
static void board_chipset_startup(void)
{
	/* Normal charge current */
	sb_smart_charge_mode(SB_SMART_CHARGE_DISABLE);

	/* hdmi retimer power on */
	ioex_set_level(IOEX_HDMI_POWER_EN_DB, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_startup, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S3 transition */
static void board_chipset_suspend(void)
{
	/* SMART charge current */
	sb_smart_charge_mode(SB_SMART_CHARGE_ENABLE);

	/* hdmi retimer power off */
	ioex_set_level(IOEX_HDMI_POWER_EN_DB, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

__override void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_PPC_FAULT_ODL:
		aoz1380_interrupt(USBC_PORT_C0);
		break;

	case GPIO_USB_C1_PPC_INT_ODL:
		if (support_aoz_ppc)
			aoz1380_interrupt(USBC_PORT_C1);
		else
			nx20p348x_interrupt(USBC_PORT_C1);
		break;

	default:
		break;
	}
}

/*
 * In the AOZ1380 PPC, there are no programmable features.  We use
 * the attached NCT3807 to control a GPIO to indicate 1A5 or 3A0
 * current limits.
 */
__override int board_aoz1380_set_vbus_source_current_limit(int port,
						enum tcpc_rp_value rp)
{
	int rv;

	/* Use the TCPC to set the current limit */
	if (port == 0) {
		rv = ioex_set_level(IOEX_USB_C0_PPC_ILIM_3A_EN,
			    (rp == TYPEC_RP_3A0) ? 1 : 0);
	} else {
		rv = ioex_set_level(IOEX_USB_C1_PPC_ILIM_3A_EN,
			    (rp == TYPEC_RP_3A0) ? 1 : 0);
	}

	return rv;
}

static void trackpoint_reset_deferred(void)
{
	gpio_set_level(gpio_ec_ps2_reset, 1);
	msleep(2);
	gpio_set_level(gpio_ec_ps2_reset, 0);
	msleep(10);
}
DECLARE_DEFERRED(trackpoint_reset_deferred);

void send_aux_data_to_device(uint8_t data)
{
	ps2_transmit_byte(NPCX_PS2_CH0, data);
}

void ps2_pwr_en_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&trackpoint_reset_deferred_data, MSEC);
}

/*****************************************************************************
 * Power signals
 */

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

#ifdef CONFIG_KEYBOARD_FACTORY_TEST
/*
 * Map keyboard connector pins to EC GPIO pins for factory test.
 * Pins mapped to {-1, -1} are skipped.
 * The connector has 24 pins total, and there is no pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
		{3, 0}, {2, 2}, {2, 3}, {1, 2}, {2, 5},
		{2, 4}, {2, 1}, {2, 7}, {2, 6}, {1, 5},
		{2, 0}, {3, 1}, {1, 7}, {1, 6}, {-1, -1},
		{1, 3}, {1, 4}, {-1, -1}, {-1, -1}, {0, 7},
		{0, 6}, {1, 0}, {1, 1}, {0, 5},
};

const int keyboard_factory_scan_pins_used =
			ARRAY_SIZE(keyboard_factory_scan_pins);
#endif

