/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Dooly board-specific configuration */

#include "accelgyro.h"
#include "adc.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "core/cortex-m/cpu.h"
#include "cros_board_info.h"
#include "driver/accel_bma2x2.h"
#include "driver/als_tcs3400.h"
#include "driver/ina3221.h"
#include "driver/led/mp3385.h"
#include "driver/led/oz554.h"
#include "driver/ppc/sn5s330.h"
#include "driver/tcpm/anx7447.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
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
#include "usb_charge.h"
#include "usb_common.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* Sensors */
static struct mutex g_accel_mutex;
static struct accelgyro_saved_data_t g_bma253_data;

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
	 * TODO: calculate the actual coefficients and scaling factors
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

const mat33_fp_t screen_standard_ref = { { 0, FLOAT_TO_FP(1), 0 },
					 { FLOAT_TO_FP(1), 0, 0 },
					 { 0, 0, FLOAT_TO_FP(-1) } };

struct motion_sensor_t motion_sensors[] = {
	[SCREEN_ACCEL] = {
		.name = "Screen Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMA255,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bma2x2_accel_drv,
		.mutex = &g_accel_mutex,
		.drv_data = &g_bma253_data,
		.port = I2C_PORT_SENSORS,
		.i2c_spi_addr_flags = BMA2x2_I2C_ADDR2_FLAGS,
		.rot_standard_ref = &screen_standard_ref,
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
	[CLEAR_ALS] = {
		.name = "Clear Light",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_TCS3400,
		.type = MOTIONSENSE_TYPE_LIGHT,
		.location = MOTIONSENSE_LOC_LID,
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
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_TCS3400,
		.type = MOTIONSENSE_TYPE_LIGHT_RGB,
		.location = MOTIONSENSE_LOC_LID,
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

/* On ECs without an FPU, the fp_t type is backed by a 32-bit fixed precision
 * representation that can only store values in the range [-32K, +32K]. Some
 * intermediary values produced in tcs3400_translate_to_xyz() do not fit in
 * that range, so we define and use a 64-bit fixed representation instead.
 */
typedef int64_t fp64_t;
#define INT_TO_FP64(x) ((int64_t)(x) << 32)
#define FP64_TO_INT(x) ((x) >> 32)
#define FLOAT_TO_FP64(x) ((int64_t)((x) * (float)(1LL << 32)))

__override void tcs3400_translate_to_xyz(struct motion_sensor_t *s,
					 int32_t *crgb_data, int32_t *xyz_data)
{
	struct tcs_saturation_t *sat_p =
		&(TCS3400_RGB_DRV_DATA(s + 1)->saturation);

	int32_t cur_gain = (1 << (2 * sat_p->again));
	int32_t integration_time_us =
		tcs3400_get_integration_time(sat_p->atime);

	fp64_t c_coeff, r_coeff, g_coeff, b_coeff;
	fp64_t result;

	/* Use different coefficients based on n_interval = (G+B)/C */
	fp64_t gb_sum = INT_TO_FP64(crgb_data[2]) + INT_TO_FP64(crgb_data[3]);
	fp64_t n_interval = gb_sum / MAX(crgb_data[0], 1);

	if (n_interval < FLOAT_TO_FP64(0.692)) {
		const float scale = 799.797;

		c_coeff = FLOAT_TO_FP64(0.009 * scale);
		r_coeff = FLOAT_TO_FP64(0.056 * scale);
		g_coeff = FLOAT_TO_FP64(2.735 * scale);
		b_coeff = FLOAT_TO_FP64(-1.903 * scale);
	} else if (n_interval < FLOAT_TO_FP64(1.012)) {
		const float scale = 801.347;

		c_coeff = FLOAT_TO_FP64(0.202 * scale);
		r_coeff = FLOAT_TO_FP64(-1.1 * scale);
		g_coeff = FLOAT_TO_FP64(8.692 * scale);
		b_coeff = FLOAT_TO_FP64(-7.068 * scale);
	} else {
		const float scale = 795.574;

		c_coeff = FLOAT_TO_FP64(-0.661 * scale);
		r_coeff = FLOAT_TO_FP64(1.334 * scale);
		g_coeff = FLOAT_TO_FP64(1.095 * scale);
		b_coeff = FLOAT_TO_FP64(-1.821 * scale);
	}

	/* Multiply each channel by the coefficient and compute the sum.
	 * Note: int * fp64_t = fp64_t and fp64_t + fp64_t = fp64_t.
	 */
	result = crgb_data[0] * c_coeff + crgb_data[1] * r_coeff +
		 crgb_data[2] * g_coeff + crgb_data[3] * b_coeff;

	/* Adjust for exposure time and sensor gain.
	 * Note: fp64_t / int = fp64_t.
	 */
	result /= MAX(integration_time_us * cur_gain / 1000, 1);

	/* Some C/R/G/B coefficients are negative, so the result could also be
	 * negative and must be clamped at zero.
	 *
	 * The value of xyz_data[1] is stored in a 16 bit integer later on, so
	 * it must be clamped at INT16_MAX.
	 */
	xyz_data[1] = MIN(MAX(FP64_TO_INT(result), 0), INT16_MAX);
}

static void ppc_interrupt(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_TCPPC_INT_ODL:
		sn5s330_interrupt(USB_PD_PORT_TCPC_0);
		break;
	case GPIO_USB_C1_TCPPC_INT_ODL:
		sn5s330_interrupt(USB_PD_PORT_TCPC_1);
		break;
	default:
		break;
	}
}

int ppc_get_alert_status(int port)
{
	int status = 0;

	switch (port) {
	case USB_PD_PORT_TCPC_0:
		status = gpio_get_level(GPIO_USB_C0_TCPPC_INT_ODL) == 0;
		break;
	case USB_PD_PORT_TCPC_1:
		status = gpio_get_level(GPIO_USB_C1_TCPPC_INT_ODL) == 0;
		break;
	default:
		break;
	}
	return status;
}

static void tcpc_alert_event(enum gpio_signal signal)
{
	switch (signal) {
	case GPIO_USB_C0_TCPC_INT_ODL:
		schedule_deferred_pd_interrupt(USB_PD_PORT_TCPC_0);
		break;
	case GPIO_USB_C1_TCPC_INT_ODL:
		schedule_deferred_pd_interrupt(USB_PD_PORT_TCPC_1);
		break;
	default:
		break;
	}
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;
	int level;

	/*
	 * Check which port has the ALERT line set and ignore if that TCPC has
	 * its reset line active.
	 */
	if (!gpio_get_level(GPIO_USB_C0_TCPC_INT_ODL)) {
		level = !!(tcpc_config[USB_PD_PORT_TCPC_0].flags &
			   TCPC_FLAGS_RESET_ACTIVE_HIGH);
		if (gpio_get_level(GPIO_USB_C0_TCPC_RST) != level)
			status |= PD_STATUS_TCPC_ALERT_0;
	}
	if (!gpio_get_level(GPIO_USB_C1_TCPC_INT_ODL)) {
		level = !!(tcpc_config[USB_PD_PORT_TCPC_1].flags &
			   TCPC_FLAGS_RESET_ACTIVE_HIGH);
		if (gpio_get_level(GPIO_USB_C1_TCPC_RST) != level)
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}

/* Called when the charge manager has switched to a new port. */
__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	/* Blink alert if insufficient power per system_can_boot_ap(). */
	int insufficient_power =
		(charge_ma * charge_mv) <
		(CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON * 1000);
	led_alert(insufficient_power);
}

static uint8_t usbc_0_overcurrent;
static uint8_t usbc_1_overcurrent;
static int32_t base_5v_power;

/*
 * Power usage for each port as measured or estimated.
 * Units are milliwatts (5v x ma current)
 */
#define PWR_BASE_LOAD (5 * 1335)
#define PWR_FRONT_HIGH (5 * 1603)
#define PWR_FRONT_LOW (5 * 963)
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
	if (usbc_0_overcurrent)
		base_5v_power += PWR_C_HIGH;
	if (usbc_1_overcurrent)
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
/*
 * Barrel jack power supply handling
 *
 * EN_PPVAR_BJ_ADP_L must default active to ensure we can power on when the
 * barrel jack is connected, and the USB-C port can bring the EC up fine in
 * dead-battery mode. Both the USB-C and barrel jack switches do reverse
 * protection, so we're safe to turn one on then the other off- but we should
 * only do that if the system is off since it might still brown out.
 */

/*
 * Barrel-jack power adapter ratings.
 */
static const struct {
	int voltage;
	int current;
} bj_power[] = {
	{ /* 0 - 65W (also default) */
	  .voltage = 19500,
	  .current = 3200 },
	{ /* 1 - 90W */
	  .voltage = 19500,
	  .current = 4600 },
};

#define ADP_DEBOUNCE_MS 1000 /* Debounce time for BJ plug/unplug */
/* Debounced connection state of the barrel jack */
static int8_t adp_connected = -1;
static void adp_connect_deferred(void)
{
	struct charge_port_info pi = { 0 };
	int connected = !gpio_get_level(GPIO_BJ_ADP_PRESENT_L);

	/* Debounce */
	if (connected == adp_connected)
		return;
	if (connected) {
		unsigned int bj = ec_config_get_bj_power();

		pi.voltage = bj_power[bj].voltage;
		pi.current = bj_power[bj].current;
	}
	charge_manager_update_charge(CHARGE_SUPPLIER_DEDICATED,
				     DEDICATED_CHARGE_PORT, &pi);
	adp_connected = connected;
}
DECLARE_DEFERRED(adp_connect_deferred);

/* IRQ for BJ plug/unplug. It shouldn't be called if BJ is the power source. */
void adp_connect_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&adp_connect_deferred_data, ADP_DEBOUNCE_MS * MSEC);
}

static void adp_state_init(void)
{
	/*
	 * Initialize all charge suppliers to 0. The charge manager waits until
	 * all ports have reported in before doing anything.
	 */
	for (int i = 0; i < CHARGE_PORT_COUNT; i++) {
		for (int j = 0; j < CHARGE_SUPPLIER_COUNT; j++)
			charge_manager_update_charge(j, i, NULL);
	}

	/* Report charge state from the barrel jack. */
	adp_connect_deferred();
}
DECLARE_HOOK(HOOK_INIT, adp_state_init, HOOK_PRIO_INIT_CHARGE_MANAGER + 1);

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
			     .flags = PWM_CONFIG_ACTIVE_LOW | PWM_CONFIG_DSLEEP,
			     .freq = 2000 },
	[PWM_CH_LED_WHITE] = { .channel = 2,
			       .flags = PWM_CONFIG_ACTIVE_LOW |
					PWM_CONFIG_DSLEEP,
			       .freq = 2000 },
};

/******************************************************************************/
/* USB-C TCPC Configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = AN7447_TCPC0_I2C_ADDR_FLAGS,
		},
		.drv = &anx7447_tcpm_drv,
		.flags = TCPC_FLAGS_RESET_ACTIVE_HIGH,
	},
	[USB_PD_PORT_TCPC_1] = {
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC1,
			.addr_flags = AN7447_TCPC0_I2C_ADDR_FLAGS,
		},
		.drv = &anx7447_tcpm_drv,
		.flags = TCPC_FLAGS_RESET_ACTIVE_HIGH,
	},
};
const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_TCPC_0] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USB_PD_PORT_TCPC_0,
			.driver = &anx7447_usb_mux_driver,
			.hpd_update = &anx7447_tcpc_update_hpd_status,
		},
	},
	[USB_PD_PORT_TCPC_1] = {
		.mux = &(const struct usb_mux) {
			.usb_port = USB_PD_PORT_TCPC_1,
			.driver = &anx7447_usb_mux_driver,
			.hpd_update = &anx7447_tcpc_update_hpd_status,
		},
	},
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
	{ .name = "ppc1",
	  .port = I2C_PORT_PPC1,
	  .kbps = 400,
	  .scl = GPIO_I2C2_SCL,
	  .sda = GPIO_I2C2_SDA },
	{ .name = "tcpc0",
	  .port = I2C_PORT_TCPC0,
	  .kbps = 400,
	  .scl = GPIO_I2C3_SCL,
	  .sda = GPIO_I2C3_SDA },
	{ .name = "tcpc1",
	  .port = I2C_PORT_TCPC1,
	  .kbps = 400,
	  .scl = GPIO_I2C4_SCL,
	  .sda = GPIO_I2C4_SDA },
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
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1] = {
		.name = "PP3300",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_1,
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
	.rpm_min = 1900,
	.rpm_start = 2400,
	.rpm_max = 4300,
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
const static struct ec_thermal_config thermal_a = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		[EC_TEMP_THRESH_HALT] = C_TO_K(78),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = 0,
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_fan_off = C_TO_K(41),
	.temp_fan_max = C_TO_K(72),
};

struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1] = thermal_a,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

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
static uint32_t ssfc;

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
	if (cbi_get_ssfc(&val) == EC_SUCCESS)
		ssfc = val;
	CPRINTS("Board Version: %d, SKU ID: 0x%08x, "
		"F/W config: 0x%08x, SSFC: 0x%08x ",
		board_version, sku_id, fw_config, ssfc);
}
DECLARE_HOOK(HOOK_INIT, cbi_init, HOOK_PRIO_INIT_I2C + 1);

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

	gpio_enable_interrupt(GPIO_BJ_ADP_PRESENT_L);

	/* Enable interrupt for the TCS3400 color light sensor */
	gpio_enable_interrupt(GPIO_ALS_GSENSOR_INT_ODL);

	/* Always claim AC is online, because we don't have a battery. */
	memmap_batt_flags = host_get_memmap(EC_MEMMAP_BATT_FLAG);
	*memmap_batt_flags |= EC_BATT_FLAG_AC_PRESENT;

	/* Initial backlight ic setting by ssfc */
	if (ec_ssfc_get_led_ic() == SSFC_LED_MP3385)
		mp3385_board_init();
	else
		oz554_board_init();
	gpio_enable_interrupt(GPIO_PANEL_BACKLIGHT_EN);
	/* set low to disable EC_PCH_RTCRST */
	gpio_set_level(GPIO_EC_PCH_RTCRST, 0);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_chipset_startup(void)
{
	/*
	 * Workaround to restore VBUS on PPC.
	 * PP1 is sourced from PP5000_A, and when the CPU shuts down and
	 * this rail drops, the PPC will internally turn off PP1_EN.
	 * When the CPU starts again, and the rail is restored, the PPC
	 * does not turn PP1_EN on again, causing VBUS to stay turned off.
	 * The workaround is to check whether the PPC is sourcing VBUS, and
	 * if so, make sure it is enabled.
	 */
	if (ppc_is_sourcing_vbus(USB_PD_PORT_TCPC_0))
		ppc_vbus_source_enable(USB_PD_PORT_TCPC_0, 1);
	if (ppc_is_sourcing_vbus(USB_PD_PORT_TCPC_1))
		ppc_vbus_source_enable(USB_PD_PORT_TCPC_1, 1);

	/* set high to enable EDID ROM WP */
	gpio_set_level(GPIO_EC_EDID_WP_DISABLE_L, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

static void board_chipset_shutdown(void)
{
	/* set low to prevent power leakage */
	gpio_set_level(GPIO_EC_EDID_WP_DISABLE_L, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);
/******************************************************************************/
/* USB-C PPC Configuration */
struct ppc_config_t ppc_chips[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[USB_PD_PORT_TCPC_0] = { .i2c_port = I2C_PORT_PPC0,
				 .i2c_addr_flags = SN5S330_ADDR0_FLAGS,
				 .drv = &sn5s330_drv },
	[USB_PD_PORT_TCPC_1] = { .i2c_port = I2C_PORT_PPC1,
				 .i2c_addr_flags = SN5S330_ADDR0_FLAGS,
				 .drv = &sn5s330_drv },
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* USB-A port control */
const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_PP5000_USB_VBUS,
};

/* Power Delivery and charging functions */
static void board_tcpc_init(void)
{
	/*
	 * Reset TCPC if we have had a system reset.
	 * With EFSv2, it is possible to be in RW without
	 * having reset the TCPC.
	 */
	if (system_get_reset_flags() & EC_RESET_FLAG_POWER_ON)
		board_reset_pd_mcu();
	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_TCPPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C0_TCPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_TCPPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_TCPC_INT_ODL);
	/* Enable other overcurrent interrupts */
	gpio_enable_interrupt(GPIO_USB_A0_OC_ODL);
	gpio_enable_interrupt(GPIO_USB_A1_OC_ODL);
}
/* Make sure this is called after fw_config is initialised */
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C + 2);

int64_t get_time_dsw_pwrok(void)
{
	/* DSW_PWROK is turned on before EC was powered. */
	return -20 * MSEC;
}

void board_reset_pd_mcu(void)
{
	int level0 = !!(tcpc_config[USB_PD_PORT_TCPC_0].flags &
			TCPC_FLAGS_RESET_ACTIVE_HIGH);
	int level1 = !!(tcpc_config[USB_PD_PORT_TCPC_1].flags &
			TCPC_FLAGS_RESET_ACTIVE_HIGH);

	gpio_set_level(GPIO_USB_C0_TCPC_RST, level0);
	gpio_set_level(GPIO_USB_C1_TCPC_RST, level1);
	crec_msleep(BOARD_TCPC_C0_RESET_HOLD_DELAY);
	gpio_set_level(GPIO_USB_C0_TCPC_RST, !level0);
	gpio_set_level(GPIO_USB_C1_TCPC_RST, !level1);
	if (BOARD_TCPC_C0_RESET_POST_DELAY)
		crec_msleep(BOARD_TCPC_C0_RESET_POST_DELAY);
}

int board_set_active_charge_port(int port)
{
	CPRINTS("Requested charge port change to %d", port);

	/*
	 * The charge manager may ask us to switch to no charger if we're
	 * running off USB-C only but upstream doesn't support PD. It requires
	 * that we accept this switch otherwise it triggers an assert and EC
	 * reset; it's not possible to boot the AP anyway, but we want to avoid
	 * resetting the EC so we can continue to do the "low power" LED blink.
	 */
	if (port == CHARGE_PORT_NONE)
		return EC_SUCCESS;

	if (port < 0 || CHARGE_PORT_COUNT <= port)
		return EC_ERROR_INVAL;

	if (port == charge_manager_get_active_charge_port())
		return EC_SUCCESS;

	/* Don't charge from a source port */
	if (board_vbus_source_enabled(port))
		return EC_ERROR_INVAL;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		int bj_active, bj_requested;

		if (charge_manager_get_active_charge_port() != CHARGE_PORT_NONE)
			/* Change is only permitted while the system is off */
			return EC_ERROR_INVAL;

		/*
		 * Current setting is no charge port but the AP is on, so the
		 * charge manager is out of sync (probably because we're
		 * reinitializing after sysjump). Reject requests that aren't
		 * in sync with our outputs.
		 */
		bj_active = !gpio_get_level(GPIO_EN_PPVAR_BJ_ADP_L);
		bj_requested = port == CHARGE_PORT_BARRELJACK;
		if (bj_active != bj_requested)
			return EC_ERROR_INVAL;
	}

	CPRINTS("New charger p%d", port);

	switch (port) {
	case CHARGE_PORT_TYPEC0:
	case CHARGE_PORT_TYPEC1:
		/* TODO(b/143975429) need to touch the PD controller? */
		gpio_set_level(GPIO_EN_PPVAR_BJ_ADP_L, 1);
		break;
	case CHARGE_PORT_BARRELJACK:
		/* Make sure BJ adapter is sourcing power */
		if (gpio_get_level(GPIO_BJ_ADP_PRESENT_L))
			return EC_ERROR_INVAL;
		/* TODO(b/143975429) need to touch the PD controller? */
		gpio_set_level(GPIO_EN_PPVAR_BJ_ADP_L, 0);
		break;
	default:
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

void board_overcurrent_event(int port, int is_overcurrented)
{
	switch (port) {
	case USB_PD_PORT_TCPC_0:
		usbc_0_overcurrent = is_overcurrented;
		break;
	case USB_PD_PORT_TCPC_1:
		usbc_1_overcurrent = is_overcurrented;
		break;
	default:
		return;
	}
	update_5v_usage();
}

int extpower_is_present(void)
{
	return adp_connected;
}

int board_is_c10_gate_enabled(void)
{
	return 0;
}

void board_enable_s0_rails(int enable)
{
}

unsigned int ec_config_get_bj_power(void)
{
	unsigned int bj = (fw_config & EC_CFG_BJ_POWER_MASK) >>
			  EC_CFG_BJ_POWER_L;
	/* Out of range value defaults to 0 */
	if (bj >= ARRAY_SIZE(bj_power))
		bj = 0;
	return bj;
}

unsigned int ec_config_get_thermal_solution(void)
{
	return (fw_config & EC_CFG_THERMAL_MASK) >> EC_CFG_THERMAL_L;
}

unsigned int ec_ssfc_get_led_ic(void)
{
	return (ssfc & EC_SSFC_LED_MASK) >> EC_SSFC_LED_L;
}

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

/* PROCHOT_DEFER_OFF is to extend CPU prochot long enough
 * to pass safety requirement 30 * 2ms = 60 ms
 */
#define PROCHOT_DEFER_OFF 30

static void power_monitor(void)
{
	static uint32_t current_state;
	static uint32_t history[POWER_READINGS];
	static uint8_t index;
	static uint8_t prochot_linger;
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
		/*
		 * Clear the first entry of the power table so that
		 * it is re-initilalised when the CPU starts.
		 */
		history[0] = 0;
	} else {
		int32_t charger_mw;

		delay = POWER_DELAY_MS * MSEC;
		/*
		 * Get current charger limit (in mw).
		 * If not configured yet, skip.
		 */
		charger_mw = charge_manager_get_power_limit_uw() / 1000;
		if (charger_mw != 0) {
			int32_t gap, total, max, power;
			int i;

			/*
			 * Read power usage.
			 */
			power = (adc_read_channel(ADC_VBUS) *
				 adc_read_channel(ADC_PPVAR_IMON)) /
				1000;
			/* Init power table */
			if (history[0] == 0) {
				for (i = 0; i < POWER_READINGS; i++)
					history[i] = power;
			}
			/*
			 * Update the power readings and
			 * calculate the average and max.
			 */
			history[index] = power;
			index = (index + 1) % POWER_READINGS;
			total = 0;
			max = history[0];
			for (i = 0; i < POWER_READINGS; i++) {
				total += history[i];
				if (history[i] > max)
					max = history[i];
			}
			/*
			 * For Type-C power supplies, there is
			 * less tolerance for exceeding the rating,
			 * so use the max power that has been measured
			 * over the measuring period.
			 * For barrel-jack supplies, the rating can be
			 * exceeded briefly, so use the average.
			 */
			if (charge_manager_get_supplier() == CHARGE_SUPPLIER_PD)
				power = max;
			else
				power = total / POWER_READINGS;
			/*
			 * Calculate gap, and if negative, power
			 * demand is exceeding configured power budget, so
			 * throttling is required to reduce the demand.
			 */
			gap = charger_mw - power;
			/*
			 * Limiting type-A power.
			 */
			if (gap <= 0) {
				new_state |= THROT_TYPE_A;
				headroom_5v += PWR_FRONT_HIGH - PWR_FRONT_LOW;
				if (!(current_state & THROT_TYPE_A))
					gap += POWER_GAIN_TYPE_A;
			}
			/*
			 * If the type-C port is sourcing power,
			 * check whether it should be throttled.
			 * TODO(amcrae): selectively disable ports.
			 */
			if (gap <= 0 && (ppc_is_sourcing_vbus(0) ||
					 ppc_is_sourcing_vbus(1))) {
				new_state |= THROT_TYPE_C;
				headroom_5v += PWR_C_HIGH - PWR_C_LOW;
				if (!(current_state & THROT_TYPE_C))
					gap += POWER_GAIN_TYPE_C * 2;
			}
			/*
			 * As a last resort, turn on PROCHOT to
			 * throttle the CPU.
			 */
			if (gap <= 0) {
				prochot_linger = 0;
				new_state |= THROT_PROCHOT;
			} else if (prochot_linger < PROCHOT_DEFER_OFF) {
				/*
				 * Do not turn off PROCHOT immediately.
				 */
				prochot_linger++;
				new_state |= THROT_PROCHOT;
			}
		}
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
		if (!((new_state & THROT_TYPE_C) || usbc_0_overcurrent ||
		      usbc_1_overcurrent)) {
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
	if (diff & THROT_TYPE_C) {
		enum tcpc_rp_value rp = (new_state & THROT_TYPE_C) ?
						TYPEC_RP_1A5 :
						TYPEC_RP_3A0;

		ppc_set_vbus_source_current_limit(0, rp);
		tcpm_select_rp_value(0, rp);
		pd_update_contract(0);

		ppc_set_vbus_source_current_limit(1, rp);
		tcpm_select_rp_value(1, rp);
		pd_update_contract(1);
	}
	if (diff & THROT_TYPE_A) {
		int typea_bc = (new_state & THROT_TYPE_A) ? 1 : 0;

		gpio_set_level(GPIO_USB_A_LOW_PWR_OD, typea_bc);
	}
	hook_call_deferred(&power_monitor_data, delay);
}

void oz554_board_init(void)
{
	int pin_status = 0;

	pin_status |= gpio_get_level(GPIO_PANEL_ID0) << 0;
	pin_status |= gpio_get_level(GPIO_PANEL_ID1) << 1;

	switch (pin_status) {
	case 0x00:
		CPRINTS("PANEL_HAN01.10A");
		oz554_set_config(0, 0xF3);
		oz554_set_config(2, 0x4C);
		oz554_set_config(5, 0xB7);
		break;
	case 0x02:
		CPRINTS("PANEL_WF9_SSA2");
		oz554_set_config(0, 0xF3);
		oz554_set_config(2, 0x55);
		oz554_set_config(5, 0x87);
		break;
	default:
		CPRINTS("PANEL UNKNOWN");
		break;
	}
}

void mp3385_board_init(void)
{
	int pin_status = 0;

	pin_status |= gpio_get_level(GPIO_PANEL_ID0) << 0;
	pin_status |= gpio_get_level(GPIO_PANEL_ID1) << 1;

	switch (pin_status) {
	case 0x00:
		CPRINTS("PANEL_HAN01.10A");
		mp3385_set_config(0, 0xF1);
		mp3385_set_config(2, 0x4C);
		mp3385_set_config(5, 0xB7);
		break;
	case 0x02:
		CPRINTS("PANEL_WF9_SSA2");
		mp3385_set_config(0, 0xF1);
		mp3385_set_config(2, 0x55);
		mp3385_set_config(5, 0x87);
		break;
	default:
		CPRINTS("PANEL UNKNOWN");
		break;
	}
}

void board_backlight_enable_interrupt(enum gpio_signal signal)
{
	switch (ec_ssfc_get_led_ic()) {
	case SSFC_LED_OZ554:
		oz554_interrupt(signal);
		break;
	case SSFC_LED_MP3385:
		mp3385_interrupt(signal);
		break;
	default:
		oz554_interrupt(signal);
		break;
	}
}
