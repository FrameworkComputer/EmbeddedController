/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Puff board-specific configuration */

#include "adc.h"
#include "button.h"
#include "chipset.h"
#include "common.h"
#include "core/cortex-m/cpu.h"
#include "cros_board_info.h"
#include "driver/als_tcs3400.h"
#include "driver/ina3221.h"
#include "ec_commands.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "power.h"
#include "power/cometlake-discrete.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "thermal.h"
#include "uart.h"
#include "usb_common.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

/* Sensors */

/* TCS3400 private data */
static struct als_drv_data_t g_tcs3400_data = {
	.als_cal.scale = 1,
	.als_cal.uscale = 0,
	.als_cal.offset = 0,
	.als_cal.channel_scale = {
		.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kc */
		.cover_scale = ALS_CHANNEL_SCALE(1.0),     /* CT */
	},
};

static struct tcs3400_rgb_drv_data_t g_tcs3400_rgb_data = {
	/*
	 * b/202465034: calculate the actual coefficients and scaling factors
	 */
	.calibration.rgb_cal[X] = {
		.offset = 0,
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kr */
			.cover_scale = ALS_CHANNEL_SCALE(1.0)
		},
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(0),
	},
	.calibration.rgb_cal[Y] = {
		.offset = 0,
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kg */
			.cover_scale = ALS_CHANNEL_SCALE(1.0)
		},
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(0.1),
	},
	.calibration.rgb_cal[Z] = {
		.offset = 0,
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kb */
			.cover_scale = ALS_CHANNEL_SCALE(1.0)
		},
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(0),
	},
	.calibration.irt = INT_TO_FP(1),
	.saturation.again = TCS_DEFAULT_AGAIN,
	.saturation.atime = TCS_DEFAULT_ATIME,
};

struct motion_sensor_t motion_sensors[] = {
	[CLEAR_ALS] = {
		.name = "Clear Light",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_TCS3400,
		.type = MOTIONSENSE_TYPE_LIGHT,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &tcs3400_drv,
		.drv_data = &g_tcs3400_data,
		.port = I2C_PORT_SENSORS,
		.i2c_spi_addr_flags = TCS3400_I2C_ADDR_FLAGS,
		.rot_standard_ref = NULL,
		.default_range = 0x10000, /* scale = 1x, uscale = 0 */
		.min_frequency = TCS3400_LIGHT_MIN_FREQ,
		.max_frequency = TCS3400_LIGHT_MAX_FREQ,
		.config = {
			/* Run ALS sensor in S0 */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 1000,
			},
		},
	},
	[RGB_ALS] = {
		.name = "RGB Light",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_TCS3400,
		.type = MOTIONSENSE_TYPE_LIGHT_RGB,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &tcs3400_rgb_drv,
		.drv_data = &g_tcs3400_rgb_data,
		.rot_standard_ref = NULL,
		.default_range = 0x10000, /* scale = 1x, uscale = 0 */
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/* ALS instances when LPC mapping is needed. Each entry directs to a sensor. */
const struct motion_sensor_t *motion_als_sensors[] = {
	&motion_sensors[CLEAR_ALS],
};
BUILD_ASSERT(ARRAY_SIZE(motion_als_sensors) == ALS_COUNT);

static void power_monitor(void);
DECLARE_DEFERRED(power_monitor);

static uint8_t usbc_overcurrent;
static int32_t base_5v_power;

/*
 * Power usage for each port as measured or estimated.
 * Units are milliwatts (5v x ma current)
 */
#define PWR_BASE_LOAD (5 * 1335)
#define PWR_FRONT_HIGH (5 * 1603)
#define PWR_FRONT_LOW (5 * 963)
#define PWR_REAR (5 * 1075)
#define PWR_HDMI (5 * 562)
#define PWR_C_HIGH (5 * 3740)
#define PWR_C_LOW (5 * 2090)
#define PWR_MAX (5 * 10000)

/*
 * Update the 5V power usage, assuming no throttling,
 * and invoke the power monitoring.
 */
static void update_5v_usage(void)
{
	int front_ports = 0;
	/*
	 * Recalculate the 5V load, assuming no throttling.
	 */
	base_5v_power = PWR_BASE_LOAD;
	if (!gpio_get_level(GPIO_USB_A0_OC_ODL)) {
		front_ports++;
		base_5v_power += PWR_FRONT_LOW;
	}
	if (!gpio_get_level(GPIO_USB_A1_OC_ODL)) {
		front_ports++;
		base_5v_power += PWR_FRONT_LOW;
	}
	/*
	 * Only 1 front port can run higher power at a time.
	 */
	if (front_ports > 0)
		base_5v_power += PWR_FRONT_HIGH - PWR_FRONT_LOW;
	if (!gpio_get_level(GPIO_USB_A2_OC_ODL))
		base_5v_power += PWR_REAR;
	if (!gpio_get_level(GPIO_USB_A3_OC_ODL))
		base_5v_power += PWR_REAR;
	if (!gpio_get_level(GPIO_HDMI_CONN0_OC_ODL))
		base_5v_power += PWR_HDMI;
	if (!gpio_get_level(GPIO_HDMI_CONN1_OC_ODL))
		base_5v_power += PWR_HDMI;
	if (usbc_overcurrent)
		base_5v_power += PWR_C_HIGH;
	/*
	 * Invoke the power handler immediately.
	 */
	hook_call_deferred(&power_monitor_data, 0);
}
DECLARE_DEFERRED(update_5v_usage);
/*
 * Start power monitoring after ADCs have been initialised.
 */
DECLARE_HOOK(HOOK_INIT, update_5v_usage, HOOK_PRIO_INIT_ADC + 1);

static void port_ocp_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&update_5v_usage_data, 0);
}

/******************************************************************************/

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/******************************************************************************/
/* SPI devices */
const struct spi_device_t spi_devices[] = {};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/******************************************************************************/
/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_FAN] = { .channel = 5,
			 .flags = PWM_CONFIG_OPEN_DRAIN,
			 .freq = 25000 },
	[PWM_CH_LED_RED] = { .channel = 0,
			     .flags = PWM_CONFIG_DSLEEP,
			     .freq = 2000 },
	[PWM_CH_LED_WHITE] = { .channel = 2,
			       .flags = PWM_CONFIG_DSLEEP,
			       .freq = 2000 },
};

/******************************************************************************/
/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "ina",
	  .port = I2C_PORT_INA,
	  .kbps = 400,
	  .scl = GPIO_I2C0_SCL,
	  .sda = GPIO_I2C0_SDA },
	{ .name = "ppc0",
	  .port = I2C_PORT_PPC0,
	  .kbps = 400,
	  .scl = GPIO_I2C1_SCL,
	  .sda = GPIO_I2C1_SDA },
	{ .name = "scaler",
	  .port = I2C_PORT_SCALER,
	  .kbps = 400,
	  .scl = GPIO_I2C2_SCL,
	  .sda = GPIO_I2C2_SDA },
	{ .name = "tcpc0",
	  .port = I2C_PORT_TCPC0,
	  .kbps = 400,
	  .scl = GPIO_I2C3_SCL,
	  .sda = GPIO_I2C3_SDA },
	{ .name = "power",
	  .port = I2C_PORT_POWER,
	  .kbps = 400,
	  .scl = GPIO_I2C5_SCL,
	  .sda = GPIO_I2C5_SDA },
	{ .name = "eeprom",
	  .port = I2C_PORT_EEPROM,
	  .kbps = 400,
	  .scl = GPIO_I2C7_SCL,
	  .sda = GPIO_I2C7_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

const struct adc_t adc_channels[] = {
	[ADC_SNS_PP3300] = {
		/*
		 * 4700/5631 voltage divider: can take the value out of range
		 * for 32-bit signed integers, so truncate to 470/563 yielding
		 * <0.1% error and a maximum intermediate value of 1623457792,
		 * which comfortably fits in int32.
		 */
		.name = "SNS_PP3300",
		.input_ch = NPCX_ADC_CH2,
		.factor_mul = ADC_MAX_VOLT * 563,
		.factor_div = (ADC_READ_MAX + 1) * 470,
	},
	[ADC_SNS_PP1050] = {
		.name = "SNS_PP1050",
		.input_ch = NPCX_ADC_CH7,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
	},
	[ADC_VBUS] = {  /* 5/39 voltage divider */
		.name = "VBUS",
		.input_ch = NPCX_ADC_CH4,
		.factor_mul = ADC_MAX_VOLT * 39,
		.factor_div = (ADC_READ_MAX + 1) * 5,
	},
	[ADC_PPVAR_IMON] = {  /* 500 mV/A */
		.name = "PPVAR_IMON",
		.input_ch = NPCX_ADC_CH9,
		.factor_mul = ADC_MAX_VOLT * 2, /* Milliamps */
		.factor_div = ADC_READ_MAX + 1,
	},
	[ADC_TEMP_SENSOR_1] = {
		.name = "TEMP_SENSOR_1",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
	},
	[ADC_TEMP_SENSOR_2] = {
		.name = "TEMP_SENSOR_2",
		.input_ch = NPCX_ADC_CH1,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_CORE] = {
		.name = "Core",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_1,
	},
	[TEMP_SENSOR_WIFI] = {
		.name = "Wifi",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_2,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/******************************************************************************/
/* Wake up pins */
const enum gpio_signal hibernate_wake_pins[] = {};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/******************************************************************************/
/* Physical fans. These are logically separate from pwm_channels. */
const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0, /* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};

const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 2400,
	.rpm_start = 2400,
	.rpm_max = 5300,
};

const struct fan_t fans[] = {
	[FAN_CH_0] = { .conf = &fan_conf_0, .rpm = &fan_rpm_0, },
};
BUILD_ASSERT(ARRAY_SIZE(fans) == FAN_CH_COUNT);

/******************************************************************************/
/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = { NPCX_MFT_MODULE_2, TCKC_LFCLK, PWM_CH_FAN },
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

/******************************************************************************/
/* Thermal control; drive fan based on temperature sensors. */
static const int temp_fan_off = C_TO_K(35);
static const int temp_fan_max = C_TO_K(55);
const static struct ec_thermal_config thermal_a = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
		[EC_TEMP_THRESH_HALT] = C_TO_K(90),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(78),
		[EC_TEMP_THRESH_HALT] = 0,
	},
		.temp_fan_off = temp_fan_off,
		.temp_fan_max = temp_fan_max,
};

static const struct ec_thermal_config thermal_b = {
		.temp_host = {
			[EC_TEMP_THRESH_WARN] = 0,
			[EC_TEMP_THRESH_HIGH] = C_TO_K(78),
			[EC_TEMP_THRESH_HALT] = C_TO_K(85),
		},
		.temp_host_release = {
			[EC_TEMP_THRESH_WARN] = 0,
			[EC_TEMP_THRESH_HIGH] = C_TO_K(70),
			[EC_TEMP_THRESH_HALT] = 0,
		},
	};

struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_CORE] = thermal_a,
	[TEMP_SENSOR_WIFI] = thermal_a,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

static const struct fan_step_1_1 fan_table0[] = {
	{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(35),
	  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(41),
	  .rpm = 2400 },
	{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(40),
	  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(44),
	  .rpm = 2900 },
	{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(42),
	  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(46),
	  .rpm = 3400 },
	{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(44),
	  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(48),
	  .rpm = 3900 },
	{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(46),
	  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(50),
	  .rpm = 4400 },
	{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(48),
	  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(52),
	  .rpm = 4900 },
	{ .decreasing_temp_ratio_threshold = TEMP_TO_RATIO(50),
	  .increasing_temp_ratio_threshold = TEMP_TO_RATIO(55),
	  .rpm = 5300 },
};
#define NUM_FAN_LEVELS ARRAY_SIZE(fan_table0)

static const struct fan_step_1_1 *fan_table = fan_table0;

int fan_percent_to_rpm(int fan, int temp_ratio)
{
	return temp_ratio_to_rpm_hysteresis(fan_table, NUM_FAN_LEVELS, fan,
					    temp_ratio, NULL);
}

/* Power sensors */
const struct ina3221_t ina3221[] = {
	{ I2C_PORT_INA, 0x40, { "PP3300_G", "PP5000_A", "PP3300_WLAN" } },
	{ I2C_PORT_INA, 0x42, { "PP3300_A", "PP3300_SSD", "PP3300_LAN" } },
	{ I2C_PORT_INA, 0x43, { NULL, "PP1200_U", "PP2500_DRAM" } }
};
const unsigned int ina3221_count = ARRAY_SIZE(ina3221);

static uint16_t board_version;
static uint32_t sku_id;
static uint32_t fw_config;

static void cbi_init(void)
{
	/*
	 * Load board info from CBI to control per-device configuration.
	 *
	 * If unset it's safe to treat the board as a proto, just C10 gating
	 * won't be enabled.
	 */
	uint32_t val;

	if (cbi_get_board_version(&val) == EC_SUCCESS && val <= UINT16_MAX)
		board_version = val;
	if (cbi_get_sku_id(&val) == EC_SUCCESS)
		sku_id = val;
	if (cbi_get_fw_config(&val) == EC_SUCCESS)
		fw_config = val;
	CPRINTS("Board Version: %d, SKU ID: 0x%08x, F/W config: 0x%08x",
		board_version, sku_id, fw_config);
}
DECLARE_HOOK(HOOK_INIT, cbi_init, HOOK_PRIO_INIT_I2C + 1);

static void board_sensors_init(void)
{
	/* Enable interrupt for the TCS3400 color light sensor */
	switch (board_version) {
	case BOARD_VERSION_PROTO:
	case BOARD_VERSION_PRE_EVT:
	case BOARD_VERSION_EVT:
		/*
		 * b/203224828: These versions incorrectly use a 1.8V interrupt
		 * line, which sends a constant interrupt signal and eventually
		 * triggers a watchdog reset, so we keep it disabled.
		 */
		gpio_disable_interrupt(GPIO_EC_RGB_INT_L);
		CPRINTS("ALS interrupt disabled (detected known-bad hardware)");
		break;

	case BOARD_VERSION_DVT:
	case BOARD_VERSION_PVT:
	default:
		gpio_enable_interrupt(GPIO_EC_RGB_INT_L);
		CPRINTS("ALS interrupt enabled");
		break;
	}
}
/* Ensure board_sensors_init runs after cbi_init. */
DECLARE_HOOK(HOOK_INIT, board_sensors_init, HOOK_PRIO_INIT_I2C + 2);

static void board_init(void)
{
	uint8_t *memmap_batt_flags;

	/* Override some GPIO interrupt priorities.
	 *
	 * These interrupts are timing-critical for AP power sequencing, so we
	 * increase their NVIC priority from the default of 3. This affects
	 * whole MIWU groups of 8 GPIOs since they share an IRQ.
	 *
	 * Latency at the default priority level can be hundreds of
	 * microseconds while other equal-priority IRQs are serviced, so GPIOs
	 * requiring faster response must be higher priority.
	 */
	/* CPU_C10_GATE_L on GPIO6.7: must be ~instant for ~60us response. */
	cpu_set_interrupt_priority(NPCX_IRQ_WKINTH_1, 1);
	/*
	 * slp_s3_interrupt (GPIOA.5 on WKINTC_0) must respond within 200us
	 * (tPLT18); less critical than the C10 gate.
	 */
	cpu_set_interrupt_priority(NPCX_IRQ_WKINTC_0, 2);

	/* Always claim AC is online, because we don't have a battery. */
	memmap_batt_flags = host_get_memmap(EC_MEMMAP_BATT_FLAG);
	*memmap_batt_flags |= EC_BATT_FLAG_AC_PRESENT;
	/*
	 * For board version < 2, the directly connected recovery
	 * button is not available.
	 */
	if (board_version < 2)
		button_disable_gpio(BUTTON_RECOVERY);

	/*
	 * Early Scout devices does not setup EC_MIC_OE in RO, so it needs
	 * to be done explicitly instead.
	 */
	gpio_set_level(GPIO_EC_MIC_OE, 1);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/******************************************************************************/
/* USB-A port control */
const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_PP5000_USB_VBUS,
};

int64_t get_time_dsw_pwrok(void)
{
	/* DSW_PWROK is turned on before EC was powered. */
	return -20 * MSEC;
}

int extpower_is_present(void)
{
	/* genesis: If the EC is running, then there is external power */
	return 1;
}

int board_is_c10_gate_enabled(void)
{
	/*
	 * Puff proto drives EN_PP5000_HDMI from EN_S0_RAILS so we cannot gate
	 * core rails while in S0 because HDMI should remain powered.
	 * EN_PP5000_HDMI is a separate EC output on all other boards.
	 */
	return board_version != 0;
}

void board_enable_s0_rails(int enable)
{
	/* This output isn't connected on protos; safe to set anyway. */
	gpio_set_level(GPIO_EN_PP5000_HDMI, enable);

	/*
	 * Toggle scaler power and its downstream USB devices.
	 */
	gpio_set_level(GPIO_EC_SCALER_EN, enable);
	gpio_set_level(GPIO_PWR_CTRL, enable);
	gpio_set_level(GPIO_EC_MX8M_ONOFF, enable);
	gpio_set_level(GPIO_EC_CAM_V3P3_EN, enable);

	gpio_set_level(GPIO_EN_LOAD_SWITCH, enable);
}

/*
 * TPU is turned on in S0, off in S0ix and lower.
 */
static void disable_tpu_power(void)
{
	gpio_set_level(GPIO_PP3300_TPU_A_EN, 0);
	gpio_set_level(GPIO_EC_IMX8_EN, 0);
}

static void enable_tpu_power(void)
{
	gpio_set_level(GPIO_PP3300_TPU_A_EN, 1);
	gpio_set_level(GPIO_EC_IMX8_EN, 1);
}

DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, disable_tpu_power, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, enable_tpu_power, HOOK_PRIO_DEFAULT);

int ec_config_get_usb4_present(void)
{
	return !(fw_config & EC_CFG_NO_USB4_MASK);
}

unsigned int ec_config_get_thermal_solution(void)
{
	return (fw_config & EC_CFG_THERMAL_MASK) >> EC_CFG_THERMAL_L;
}

static void setup_thermal(void)
{
	unsigned int table = ec_config_get_thermal_solution();
	/* Configure Fan */
	switch (table) {
	/* Default and table0 use single fan */
	case 0:
	default:
		thermal_params[TEMP_SENSOR_CORE] = thermal_a;
		thermal_params[TEMP_SENSOR_WIFI] = thermal_a;
		break;
	/* Table1 is fanless */
	case 1:
		fan_set_count(0);
		thermal_params[TEMP_SENSOR_CORE] = thermal_b;
		break;
	}
}
/* fan_set_count should be called before  HOOK_INIT/HOOK_PRIO_DEFAULT */
DECLARE_HOOK(HOOK_INIT, setup_thermal, HOOK_PRIO_DEFAULT - 1);

/*
 * Power monitoring and management.
 *
 * The overall goal is to gracefully manage the power demand so that
 * the power budgets are met without letting the system fall into
 * power deficit (perhaps causing a brownout).
 *
 * There are 2 power budgets that need to be managed:
 *  - overall system power as measured on the main power supply rail.
 *  - 5V power delivered to the USB and HDMI ports.
 *
 * The actual system power demand is calculated from the VBUS voltage and
 * the input current (read from a shunt), averaged over 5 readings.
 * The power budget limit is from the charge manager.
 *
 * The 5V power cannot be read directly. Instead, we rely on overcurrent
 * inputs from the USB and HDMI ports to indicate that the port is in use
 * (and drawing maximum power).
 *
 * There are 3 throttles that can be applied (in priority order):
 *
 *  - Type A BC1.2 front port restriction (3W)
 *  - Type C PD (throttle to 1.5A if sourcing)
 *  - Turn on PROCHOT, which immediately throttles the CPU.
 *
 *  The first 2 throttles affect both the system power and the 5V rails.
 *  The third is a last resort to force an immediate CPU throttle to
 *  reduce the overall power use.
 *
 *  The strategy is to determine what the state of the throttles should be,
 *  and to then turn throttles off or on as needed to match this.
 *
 *  This function runs on demand, or every 2 ms when the CPU is up,
 *  and continually monitors the power usage, applying the
 *  throttles when necessary.
 *
 *  All measurements are in milliwatts.
 */
#define THROT_TYPE_A BIT(0)
#define THROT_TYPE_C BIT(1)
#define THROT_PROCHOT BIT(2)

/*
 * Power gain if front USB A ports are limited.
 */
#define POWER_GAIN_TYPE_A 3200
/*
 * Power gain if Type C port is limited.
 */
#define POWER_GAIN_TYPE_C 8800
/*
 * Power is averaged over 10 ms, with a reading every 2 ms.
 */
#define POWER_DELAY_MS 2
#define POWER_READINGS (10 / POWER_DELAY_MS)

static void power_monitor(void)
{
	static uint32_t current_state;
	int32_t delay;
	uint32_t new_state = 0, diff;
	int32_t headroom_5v = PWR_MAX - base_5v_power;

	/*
	 * If CPU is off or suspended, no need to throttle
	 * or restrict power.
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF | CHIPSET_STATE_SUSPEND)) {
		/*
		 * Slow down monitoring, assume no throttling required.
		 */
		delay = 20 * MSEC;
	} else {
		delay = POWER_DELAY_MS * MSEC;
	}
	/*
	 * Check the 5v power usage and if necessary,
	 * adjust the throttles in priority order.
	 *
	 * Either throttle may have already been activated by
	 * the overall power control.
	 *
	 * We rely on the overcurrent detection to inform us
	 * if the port is in use.
	 *
	 *  - If type C not already throttled:
	 *	* If not overcurrent, prefer to limit type C [1].
	 *	* If in overcurrentuse:
	 *		- limit type A first [2]
	 *		- If necessary, limit type C [3].
	 *  - If type A not throttled, if necessary limit it [2].
	 */
	if (headroom_5v < 0) {
		/*
		 * Check whether type C is not throttled,
		 * and is not overcurrent.
		 */
		if (!((new_state & THROT_TYPE_C) || usbc_overcurrent)) {
			/*
			 * [1] Type C not in overcurrent, throttle it.
			 */
			headroom_5v += PWR_C_HIGH - PWR_C_LOW;
			new_state |= THROT_TYPE_C;
		}
		/*
		 * [2] If type A not already throttled, and power still
		 * needed, limit type A.
		 */
		if (!(new_state & THROT_TYPE_A) && headroom_5v < 0) {
			headroom_5v += PWR_FRONT_HIGH - PWR_FRONT_LOW;
			new_state |= THROT_TYPE_A;
		}
		/*
		 * [3] If still under-budget, limit type C.
		 * No need to check if it is already throttled or not.
		 */
		if (headroom_5v < 0)
			new_state |= THROT_TYPE_C;
	}
	/*
	 * Turn the throttles on or off if they have changed.
	 */
	diff = new_state ^ current_state;
	current_state = new_state;
	if (diff & THROT_PROCHOT) {
		int prochot = (new_state & THROT_PROCHOT) ? 0 : 1;

		gpio_set_level(GPIO_EC_PROCHOT_ODL, prochot);
	}
	if (diff & THROT_TYPE_A) {
		int typea_bc = (new_state & THROT_TYPE_A) ? 1 : 0;

		gpio_set_level(GPIO_USB_A_LOW_PWR_OD, typea_bc);
	}
	hook_call_deferred(&power_monitor_data, delay);
}
