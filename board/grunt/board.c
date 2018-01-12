/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Grunt board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charge_state_v2.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/ppc/sn5s330.h"
#include "driver/tcpm/anx74xx.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/temp_sensor/sb_tsi.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
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
#include "temp_sensor.h"
#include "thermistor.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static void tcpc_alert_event(enum gpio_signal signal)
{
	if ((signal == GPIO_USB_C0_PD_INT_ODL) &&
			!gpio_get_level(GPIO_USB_C0_PD_RST_L))
		return;

	if ((signal == GPIO_USB_C1_PD_INT_ODL) &&
			!gpio_get_level(GPIO_USB_C1_PD_RST_L))
		return;

#ifdef HAS_TASK_PDCMD
	/* Exchange status with TCPCs */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
#endif
}

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
static void anx74xx_cable_det_handler(void)
{
	int cable_det = gpio_get_level(GPIO_USB_C0_CABLE_DET);
	int reset_n = gpio_get_level(GPIO_USB_C0_PD_RST_L);

	/*
	 * A cable_det low->high transition was detected. If following the
	 * debounce time, cable_det is high, and reset_n is low, then ANX3429 is
	 * currently in standby mode and needs to be woken up. Set the
	 * TCPC_RESET event which will bring the ANX3429 out of standby
	 * mode. Setting this event is gated on reset_n being low because the
	 * ANX3429 will always set cable_det when transitioning to normal mode
	 * and if in normal mode, then there is no need to trigger a tcpc reset.
	 */
	if (cable_det && !reset_n)
		task_set_event(TASK_ID_PD_C0, PD_EVENT_TCPC_RESET, 0);
}
DECLARE_DEFERRED(anx74xx_cable_det_handler);

void anx74xx_cable_det_interrupt(enum gpio_signal signal)
{
	/* debounce for 2 msec */
	hook_call_deferred(&anx74xx_cable_det_handler_data, (2 * MSEC));
}
#endif

#include "gpio_list.h"

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used =  ARRAY_SIZE(hibernate_wake_pins);

const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_CHARGER] = {
		"CHARGER", NPCX_ADC_CH0, ADC_MAX_VOLT, ADC_READ_MAX+1, 0
	},
	[ADC_TEMP_SENSOR_SOC] = {
		"SOC", NPCX_ADC_CH1, ADC_MAX_VOLT, ADC_READ_MAX+1, 0
	},
};

/* Power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_PCH_SLP_S3_L,     POWER_SIGNAL_ACTIVE_HIGH, "SLP_S3_DEASSERTED"},
	{GPIO_PCH_SLP_S5_L,     POWER_SIGNAL_ACTIVE_HIGH, "SLP_S5_DEASSERTED"},
	{GPIO_VGATE,            POWER_SIGNAL_ACTIVE_HIGH, "VGATE"},
	{GPIO_SPOK,             POWER_SIGNAL_ACTIVE_HIGH, "SPOK"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* I2C port map. */
const struct i2c_port_t i2c_ports[] = {
	{"power",   I2C_PORT_POWER,   100, GPIO_I2C0_SCL, GPIO_I2C0_SDA},
	{"tcpc0",   I2C_PORT_TCPC0,   400, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"tcpc1",   I2C_PORT_TCPC1,   400, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
	{"thermal", I2C_PORT_THERMAL, 400, GPIO_I2C3_SCL, GPIO_I2C3_SDA},
	{"sensor",  I2C_PORT_SENSOR,  400, GPIO_I2C7_SCL, GPIO_I2C7_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

#define USB_PD_PORT_ANX74XX	0
#define USB_PD_PORT_PS8751	1

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	[USB_PD_PORT_ANX74XX] = {
		.i2c_host_port = I2C_PORT_TCPC0,
		.i2c_slave_addr = 0x50,
		.drv = &anx74xx_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},
	[USB_PD_PORT_PS8751] = {
		.i2c_host_port = I2C_PORT_TCPC1,
		.i2c_slave_addr = 0x16,
		.drv = &ps8xxx_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},
};

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C0_PD_RST_L))
			status |= PD_STATUS_TCPC_ALERT_0;
	}

	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C1_PD_RST_L))
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.port_addr = USB_PD_PORT_ANX74XX,  /* don't care / unused */
		.driver = &anx74xx_tcpm_usb_mux_driver,
		.hpd_update = &anx74xx_tcpc_update_hpd_status,
	},
	{
		.port_addr = USB_PD_PORT_PS8751,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
		/* TODO(ecgh): ps8751_tune_mux needed? */
	}
};

const struct ppc_config_t ppc_chips[] = {
	{
		.i2c_port = I2C_PORT_TCPC0,
		.i2c_addr = SN5S330_ADDR0,
		.drv = &sn5s330_drv
	},
	{
		.i2c_port = I2C_PORT_TCPC1,
		.i2c_addr = SN5S330_ADDR0,
		.drv = &sn5s330_drv
	},
};
const unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

const int usb_port_enable[CONFIG_USB_PORT_POWER_SMART_PORT_COUNT] = {
	GPIO_EN_USB_A0_5V,
	GPIO_EN_USB_A1_5V,
};

static void board_init(void)
{
	/* Enable Gyro interrupts */
	gpio_enable_interrupt(GPIO_6AXIS_INT_L);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_chipset_resume(void)
{
	/* Turn on display backlight (active low GPIO). */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT_L, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

static void board_chipset_suspend(void)
{
	/* Turn off display backlight (active low GPIO). */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT_L, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

/**
 * Power on (or off) a single TCPC.
 * minimum on/off delays are included.
 *
 * @param port	Port number of TCPC.
 * @param mode	0: power off, 1: power on.
 */
void board_set_tcpc_power_mode(int port, int mode)
{
	if (port != USB_PD_PORT_ANX74XX)
		return;

	switch (mode) {
	case ANX74XX_NORMAL_MODE:
		gpio_set_level(GPIO_EN_USB_C0_TCPC_PWR, 1);
		msleep(ANX74XX_PWR_H_RST_H_DELAY_MS);
		gpio_set_level(GPIO_USB_C0_PD_RST_L, 1);
		break;
	case ANX74XX_STANDBY_MODE:
		gpio_set_level(GPIO_USB_C0_PD_RST_L, 0);
		msleep(ANX74XX_RST_L_PWR_L_DELAY_MS);
		gpio_set_level(GPIO_EN_USB_C0_TCPC_PWR, 0);
		msleep(ANX74XX_PWR_L_PWR_H_DELAY_MS);
		break;
	default:
		break;
	}
}

void board_reset_pd_mcu(void)
{
	/* Assert reset to TCPC1 (ps8751) */
	gpio_set_level(GPIO_USB_C1_PD_RST_L, 0);

	/* Assert reset to TCPC0 (anx3429) */
	gpio_set_level(GPIO_USB_C0_PD_RST_L, 0);

	/* TCPC1 (ps8751) requires 1ms reset down assertion */
	msleep(MAX(1, ANX74XX_RST_L_PWR_L_DELAY_MS));

	/* Deassert reset to TCPC1 */
	gpio_set_level(GPIO_USB_C1_PD_RST_L, 1);
	/* Disable TCPC0 power */
	gpio_set_level(GPIO_EN_USB_C0_TCPC_PWR, 0);

	/*
	 * anx3429 requires 10ms reset/power down assertion
	 */
	msleep(ANX74XX_PWR_L_PWR_H_DELAY_MS);
	board_set_tcpc_power_mode(USB_PD_PORT_ANX74XX, 1);
}

void board_tcpc_init(void)
{
	int port;

	/* TODO(ecgh): need to wait for disconnected battery? */

	/* Only reset TCPC if not sysjump */
	if (!system_jumped_to_this_image())
		board_reset_pd_mcu();

	/* Enable TCPC0 interrupt */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);

	/* Enable TCPC1 interrupt */
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	/* Enable CABLE_DET interrupt for ANX3429 wake from standby */
	gpio_enable_interrupt(GPIO_USB_C0_CABLE_DET);
#endif
	/*
	 * Initialize HPD to low; after sysjump SOC needs to see
	 * HPD pulse to enable video path
	 */
	for (port = 0; port < CONFIG_USB_PD_PORT_COUNT; port++) {
		const struct usb_mux *mux = &usb_muxes[port];

		mux->hpd_update(port, 0, 0);
	}
}

void board_overcurrent_event(int port)
{
	/* TODO(ecgh): assert GPIO to notify SOC */
}

int board_set_active_charge_port(int port)
{
	int i;
	int rv;

	CPRINTS("New chg p%d", port);

	if (port == CHARGE_PORT_NONE) {
		/* Disable all ports. */
		for (i = 0; i < ppc_cnt; i++) {
			rv = ppc_vbus_sink_enable(i, 0);
			if (rv) {
				CPRINTS("Disabling p%d sink path failed.", i);
				return rv;
			}
		}

		return EC_SUCCESS;
	}

	/* Check if the port is sourcing VBUS. */
	if (ppc_is_sourcing_vbus(port)) {
		CPRINTF("Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < ppc_cnt; i++) {
		if (i == port)
			continue;

		if (ppc_vbus_sink_enable(i, 0))
			CPRINTS("p%d: sink path disable failed.", i);
	}

	/* Enable requested charge port. */
	if (ppc_vbus_sink_enable(port, 1)) {
		CPRINTS("p%d: sink path enable failed.");
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	/* TODO(ecgh): check limits */
	/*
	 * To protect the charge inductor, at voltages above 18V we should
	 * set the current limit to 2.7A.
	 */
	if (charge_mv > 18000)
		charge_ma = MIN(2700, charge_ma);

	charge_set_input_current_limit(MAX(charge_ma,
					   CONFIG_CHARGER_INPUT_CURRENT),
				       charge_mv);
}

/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
	/* Extra delay when KSO2 is tied to Cr50. */
	.output_settle_us = 60,
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

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = { 5, 0, 100 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/*
 * We use 11 as the scaling factor so that the maximum mV value below (2761)
 * can be compressed to fit in a uint8_t.
 */
#define THERMISTOR_SCALING_FACTOR 11

/*
 * Values are calculated from the "Resistance VS. Temperature" table on the
 * Murata page for part NCP15WB473F03RC. Vdd=3.3V, R=30.9Kohm.
 */
static const struct thermistor_data_pair thermistor_data[] = {
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

static const struct thermistor_info thermistor_info = {
	.scaling_factor = THERMISTOR_SCALING_FACTOR,
	.num_pairs = ARRAY_SIZE(thermistor_data),
	.data = thermistor_data,
};

static int board_get_temp(int idx, int *temp_k)
{
	int mv = adc_read_channel(idx ? NPCX_ADC_CH1 : NPCX_ADC_CH0);
	int temp_c;

	if (mv < 0)
		return -1;

	temp_c = thermistor_linear_interpolate(mv, &thermistor_info);
	*temp_k = C_TO_K(temp_c);
	return 0;
}

const struct temp_sensor_t temp_sensors[] = {
	{"Charger", TEMP_SENSOR_TYPE_BOARD, board_get_temp, 0, 1},
	{"SOC", TEMP_SENSOR_TYPE_BOARD, board_get_temp, 1, 5},
	{"CPU", TEMP_SENSOR_TYPE_CPU, sb_tsi_get_val, 0, 4},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* Motion sensors */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/*
 * Matrix to rotate accelerator into standard reference frame
 *
 * TODO(teravest): Update this when we can physically test a Grunt.
 */
const matrix_3x3_t base_standard_ref = {
	{ 0, FLOAT_TO_FP(-1), 0},
	{ FLOAT_TO_FP(1), 0,  0},
	{ 0, 0,  FLOAT_TO_FP(1)}
};

/* sensor private data */
static struct kionix_accel_data g_kx022_data;
static struct bmi160_drv_data_t g_bmi160_data;

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
	 .addr = KX022_ADDR1,
	 .rot_standard_ref = NULL, /* Identity matrix. */
	 .default_range = 2, /* g, enough for laptop. */
	 .min_frequency = KX022_ACCEL_MIN_FREQ,
	 .max_frequency = KX022_ACCEL_MAX_FREQ,
	 .config = {
		/* AP: by default use EC settings */
		[SENSOR_CONFIG_AP] = {
			.odr = 0,
			.ec_rate = 0,
		},
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 0,
		},
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 0,
		},
		/* Sensor off in S5. */
		[SENSOR_CONFIG_EC_S5] = {
			.odr = 0,
			.ec_rate = 0,
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
	 .addr = BMI160_ADDR0,
	 .default_range = 2, /* g, enough for laptop */
	 .rot_standard_ref = &base_standard_ref,
	 .min_frequency = BMI160_ACCEL_MIN_FREQ,
	 .max_frequency = BMI160_ACCEL_MAX_FREQ,
	 .config = {
		 /* AP: by default shutdown all sensors */
		 [SENSOR_CONFIG_AP] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100,
		 },
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 0,
		 },
		 /* Sensor off in S5 */
		 [SENSOR_CONFIG_EC_S5] = {
			.odr = 0,
			.ec_rate = 0,
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
	 .addr = BMI160_ADDR0,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = &base_standard_ref,
	 .min_frequency = BMI160_GYRO_MIN_FREQ,
	 .max_frequency = BMI160_GYRO_MAX_FREQ,
	 .config = {
		 /* AP: by default shutdown all sensors */
		 [SENSOR_CONFIG_AP] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* EC does not need in S0 */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = 0,
			.ec_rate = 0,
		 },
		 /* Sensor off in S5 */
		 [SENSOR_CONFIG_EC_S5] = {
			.odr = 0,
			.ec_rate = 0,
		 },
	 },
	},
};

const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

#ifndef TEST_BUILD
void lid_angle_peripheral_enable(int enable)
{
	keyboard_scan_enable(enable, KB_SCAN_DISABLE_LID_ANGLE);
}
#endif
