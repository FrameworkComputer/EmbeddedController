/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "adc_chip.h"
#include "backlight.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "driver/accel_bma2x2.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/charger/bd99955.h"
#include "driver/tcpm/fusb302.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "led_common.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "shi_chip.h"
#include "spi.h"
#include "switch.h"
#include "task.h"
#include "tcpm.h"
#include "timer.h"
#include "thermal.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static void tcpc_alert_event(enum gpio_signal signal)
{
#ifdef HAS_TASK_PDCMD
	/* Exchange status with TCPCs */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
#endif
}

static void overtemp_interrupt(enum gpio_signal signal)
{
	CPRINTS("AP_OVERTEMP asserted.  Shutting down AP!");
	chipset_force_shutdown();
}

static void warm_reset_request_interrupt(enum gpio_signal signal)
{
	CPRINTS("WARM_RESET_REQ asserted.");
	chipset_reset(0);
}

#include "gpio_list.h"

/******************************************************************************/
/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	[ADC_BOARD_ID] = {
		"BOARD_ID", NPCX_ADC_CH0, ADC_MAX_VOLT, ADC_READ_MAX+1, 0 },
	[ADC_PP900_AP] = {
		"PP900_AP", NPCX_ADC_CH1, ADC_MAX_VOLT, ADC_READ_MAX+1, 0 },
	[ADC_PP1200_LPDDR] = {
		"PP1200_LPDDR", NPCX_ADC_CH2, ADC_MAX_VOLT, ADC_READ_MAX+1, 0 },
	[ADC_PPVAR_CLOGIC] = {
		"PPVAR_CLOGIC",
		NPCX_ADC_CH3, ADC_MAX_VOLT, ADC_READ_MAX+1, 0 },
	[ADC_PPVAR_LOGIC] = {
		"PPVAR_LOGIC", NPCX_ADC_CH4, ADC_MAX_VOLT, ADC_READ_MAX+1, 0 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************/
/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
#ifdef BOARD_KEVIN
	[PWM_CH_LED_GREEN] = { 0, PWM_CONFIG_DSLEEP, 100 },
#endif
	[PWM_CH_DISPLIGHT] = { 2, 0, 210 },
	[PWM_CH_LED_RED] =   { 3, PWM_CONFIG_DSLEEP, 100 },
#ifdef BOARD_KEVIN
	[PWM_CH_LED_BLUE] =  { 4, PWM_CONFIG_DSLEEP, 100 },
#endif
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/******************************************************************************/
/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"tcpc0",   NPCX_I2C_PORT0_0, 1000, GPIO_I2C0_SCL0, GPIO_I2C0_SDA0},
	{"tcpc1",   NPCX_I2C_PORT0_1, 1000, GPIO_I2C0_SCL1, GPIO_I2C0_SDA1},
	{"sensors", NPCX_I2C_PORT1,    400, GPIO_I2C1_SCL,  GPIO_I2C1_SDA},
	{"charger", NPCX_I2C_PORT2,    400, GPIO_I2C2_SCL,  GPIO_I2C2_SDA},
	{"battery", NPCX_I2C_PORT3,    100, GPIO_I2C3_SCL,  GPIO_I2C3_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_PP5000_PG,         1, "PP5000_PWR_GOOD"},
	{GPIO_TPS65261_PG,       1, "SYS_PWR_GOOD"},
	{GPIO_AP_CORE_PG,        1, "AP_PWR_GOOD"},
	{GPIO_AP_EC_S3_S0_L,     0, "SUSPEND_DEASSERTED"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/******************************************************************************/
/* SPI devices */
const struct spi_device_t spi_devices[] = {
	{ CONFIG_SPI_ACCEL_PORT, 1, GPIO_SPI_SENSOR_CS_L }
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/******************************************************************************/
/* Wake-up pins for hibernate */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_POWER_BUTTON_L, GPIO_CHARGER_INT_L, GPIO_LID_OPEN
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/******************************************************************************/
/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
	.output_settle_us = 40,
	.debounce_down_us = 6 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 1500,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = SECOND,
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xc8  /* full set with lock key */
	},
};

const struct button_config buttons[CONFIG_BUTTON_COUNT] = {
	{"Volume Down", KEYBOARD_BUTTON_VOLUME_DOWN, GPIO_VOLUME_DOWN_L,
	 30 * MSEC, 0},
	{"Volume Up", KEYBOARD_BUTTON_VOLUME_UP, GPIO_VOLUME_UP_L,
	 30 * MSEC, 0},
};

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	{I2C_PORT_TCPC0, FUSB302_I2C_SLAVE_ADDR, &fusb302_tcpm_drv},
	{I2C_PORT_TCPC1, FUSB302_I2C_SLAVE_ADDR, &fusb302_tcpm_drv},
};

static const enum bd99955_charge_port
	pd_port_to_bd99955_port[CONFIG_USB_PD_PORT_COUNT] = {
	[0] = BD99955_CHARGE_PORT_VBUS,
	[1] = BD99955_CHARGE_PORT_VCC,
};

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.port_addr = 0,
		.driver = &virtual_usb_mux_driver,
	},
	{
		.port_addr = 1,
		.driver = &virtual_usb_mux_driver,
	},
};

void board_reset_pd_mcu(void)
{
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_L))
		status |= PD_STATUS_TCPC_ALERT_0;
	if (!gpio_get_level(GPIO_USB_C1_PD_INT_L))
		status |= PD_STATUS_TCPC_ALERT_1;

	return status;
}

int board_set_active_charge_port(int charge_port)
{
	enum bd99955_charge_port bd99955_port;
	static int initialized;

	/*
	 * Reject charge port disable if our battery is critical and we
	 * have yet to initialize a charge port - continue to charge using
	 * charger ROM / POR settings.
	 */
	if (!initialized &&
	    charge_port == CHARGE_PORT_NONE &&
	    charge_get_percent() < 2) {
		CPRINTS("Battery critical, don't disable charging");
		return -1;
	}

	CPRINTS("New chg p%d", charge_port);

	switch (charge_port) {
	case 0: case 1:
		bd99955_port = pd_port_to_bd99955_port[charge_port];
		break;
	case CHARGE_PORT_NONE:
		bd99955_port = BD99955_CHARGE_PORT_NONE;
		break;
	default:
		panic("Invalid charge port\n");
		break;
	}

	initialized = 1;
	return bd99955_select_input_port(bd99955_port);
}

void board_set_charge_limit(int port, int supplier, int charge_ma)
{
	charge_set_input_current_limit(MAX(charge_ma,
				       CONFIG_CHARGER_INPUT_CURRENT));
}

int extpower_is_present(void)
{
	/* Check VBUS on either port */
	return bd99955_is_vbus_provided(BD99955_CHARGE_PORT_BOTH);
}

int pd_snk_is_vbus_provided(int port)
{
	return bd99955_is_vbus_provided(pd_port_to_bd99955_port[port]);
}

static void board_init(void)
{
	/* Enable charger interrupt for BC1.2 detection on attach / detach */
	gpio_enable_interrupt(GPIO_CHARGER_INT_L);

	/* Sensor Init */
	gpio_config_module(MODULE_SPI_MASTER, 1);
	spi_enable(CONFIG_SPI_ACCEL_PORT, 1);
	CPRINTS("Board using SPI sensors");
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

enum kevin_board_version {
	BOARD_VERSION_UNKNOWN = -1,
	BOARD_VERSION_REV0 = 0,
	BOARD_VERSION_REV1 = 1,
	BOARD_VERSION_REV2 = 2,
	BOARD_VERSION_REV3 = 3,
	BOARD_VERSION_REV4 = 4,
	BOARD_VERSION_REV5 = 5,
	BOARD_VERSION_REV6 = 6,
	BOARD_VERSION_REV7 = 7,
	BOARD_VERSION_REV8 = 8,
	BOARD_VERSION_REV9 = 9,
	BOARD_VERSION_REV10 = 10,
	BOARD_VERSION_REV11 = 11,
	BOARD_VERSION_REV12 = 12,
	BOARD_VERSION_REV13 = 13,
	BOARD_VERSION_REV14 = 14,
	BOARD_VERSION_REV15 = 15,
	BOARD_VERSION_COUNT,
};

struct {
	enum kevin_board_version version;
	int expect_mv;
} const kevin_boards[] = {
	{ BOARD_VERSION_REV0, 109 },   /* 51.1K , 2.2K(gru 3.3K) ohm */
	{ BOARD_VERSION_REV1, 211 },   /* 51.1k , 6.8K ohm */
	{ BOARD_VERSION_REV2, 319 },   /* 51.1K , 11K ohm */
	{ BOARD_VERSION_REV3, 427 },   /* 56K   , 17.4K ohm */
	{ BOARD_VERSION_REV4, 542 },   /* 51.1K , 22K ohm */
	{ BOARD_VERSION_REV5, 666 },   /* 51.1K , 30K ohm */
	{ BOARD_VERSION_REV6, 781 },   /* 51.1K , 39.2K ohm */
	{ BOARD_VERSION_REV7, 900 },   /* 56K   , 56K ohm */
	{ BOARD_VERSION_REV8, 1023 },  /* 47K   , 61.9K ohm */
	{ BOARD_VERSION_REV9, 1137 },  /* 47K   , 80.6K ohm */
	{ BOARD_VERSION_REV10, 1240 }, /* 56K   , 124K ohm */
	{ BOARD_VERSION_REV11, 1343 }, /* 51.1K , 150K ohm */
	{ BOARD_VERSION_REV12, 1457 }, /* 47K   , 200K ohm */
	{ BOARD_VERSION_REV13, 1576 }, /* 47K   , 330K ohm */
	{ BOARD_VERSION_REV14, 1684 }, /* 47K   , 680K ohm */
	{ BOARD_VERSION_REV15, 1800 }, /* 56K   , NC */
};
BUILD_ASSERT(ARRAY_SIZE(kevin_boards) == BOARD_VERSION_COUNT);

#define THRESHHOLD_MV 56 /* Simply assume 1800/16/2 */

int board_get_version(void)
{
	static int version = BOARD_VERSION_UNKNOWN;
	int mv;
	int i;

	if (version != BOARD_VERSION_UNKNOWN)
		return version;

	gpio_set_level(GPIO_EC_BOARD_ID_EN_L, 0);
	/* Wait to allow cap charge */
	msleep(10);
	mv = adc_read_channel(ADC_BOARD_ID);

	/* TODO(crosbug.com/p/54971): Fix failure on first ADC conversion. */
	if (mv == ADC_READ_ERROR)
		mv = adc_read_channel(ADC_BOARD_ID);

	gpio_set_level(GPIO_EC_BOARD_ID_EN_L, 1);

	for (i = 0; i < BOARD_VERSION_COUNT; ++i) {
		if (mv < kevin_boards[i].expect_mv + THRESHHOLD_MV) {
			version = kevin_boards[i].version;
			break;
		}
	}

	return version;
}

/*
 * Detect 'old' boards which are incompatible with our new GPIO configuration
 * and warn the user about the incompatibility by spamming the EC console and
 * blinking our red LED.
 *
 * TODO(crosbug.com/p/55561): Remove version checking / warning prints once
 * old boards are obsoleted.
 */
#ifdef BOARD_KEVIN
#define BOARD_VERSION_NEW_GPIO_CFG BOARD_VERSION_REV3
#else
#define BOARD_VERSION_NEW_GPIO_CFG BOARD_VERSION_REV1
#endif

/* CONFIG removed in CL:351151. */
#ifndef CONFIG_USB_PD_5V_EN_ACTIVE_LOW
static void board_config_warning(void);
DECLARE_DEFERRED(board_config_warning);

static void board_config_warning(void)
{
	static int led_toggle;

	ccprintf("WARNING: Invalid GPIO config detected.\n"
		 "PLEASE REVERT CL:351151 in local EC source:\n"
		 "`git revert d1138722`\n");

	/* Flash red LED as warning */
	led_auto_control(EC_LED_ID_POWER_LED, 0);
	led_auto_control(EC_LED_ID_BATTERY_LED, 0);
	pwm_set_duty(PWM_CH_LED_RED, led_toggle ? 0 : 100);
	led_toggle  = !led_toggle;

	hook_call_deferred(&board_config_warning_data, SECOND);
}

static void board_config_check(void)
{
	int board_ver = board_get_version();

	if (board_ver < BOARD_VERSION_NEW_GPIO_CFG)
		hook_call_deferred(&board_config_warning_data, 0);
}
DECLARE_HOOK(HOOK_INIT, board_config_check, HOOK_PRIO_LAST);
#endif /* ifndef CONFIG_USB_PD_5V_EN_ACTIVE_LOW */

static void overtemp_interrupt_enable(void)
{
	gpio_enable_interrupt(GPIO_AP_OVERTEMP);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, overtemp_interrupt_enable,
	     HOOK_PRIO_DEFAULT);
static void overtemp_interrupt_disable(void)
{
	gpio_disable_interrupt(GPIO_AP_OVERTEMP);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, overtemp_interrupt_disable,
	     HOOK_PRIO_DEFAULT);

/* Motion sensors */
#ifdef HAS_TASK_MOTIONSENSE
/* Mutexes */
static struct mutex g_base_mutex;
#ifdef BOARD_KEVIN
static struct mutex g_lid_mutex;

/* BMA255 private data */
struct bma2x2_accel_data g_bma255_data = {
	.variant = BMA255,
};

/* Matrix to rotate accelrator into standard reference frame */
const matrix_3x3_t base_standard_ref = {
	{ 0, FLOAT_TO_FP(1),  0},
	{ FLOAT_TO_FP(1),  0, 0},
	{ 0,  0, FLOAT_TO_FP(-1)}
};

const matrix_3x3_t lid_standard_ref = {
	{ 0,  FLOAT_TO_FP(1), 0},
	{ FLOAT_TO_FP(-1),  0,  0},
	{ 0,  0, FLOAT_TO_FP(1)}
};
#endif

struct motion_sensor_t motion_sensors[] = {
	/*
	 * Note: bmi160: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	{.name = "Base Accel",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = CONFIG_SPI_ACCEL_PORT,
	 .addr = BMI160_SET_SPI_ADDRESS(CONFIG_SPI_ACCEL_PORT),
#ifdef BOARD_KEVIN
	 .rot_standard_ref = &base_standard_ref,
#else
	 .rot_standard_ref = NULL, /* Identity matrix. */
#endif
	 .default_range = 2,  /* g, enough for laptop. */
	 .config = {
		 /* AP: by default use EC settings */
		 [SENSOR_CONFIG_AP] = {
			 .odr = 0,
			 .ec_rate = 0,
		 },
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			 .odr = 10000 | ROUND_UP_FLAG,
			 .ec_rate = 100,
		 },
		 /* Sensor off in S3/S5 */
		 [SENSOR_CONFIG_EC_S3] = {
			 .odr = 0,
			 .ec_rate = 0
		 },
		 /* Sensor off in S3/S5 */
		 [SENSOR_CONFIG_EC_S5] = {
			 .odr = 0,
			 .ec_rate = 0
		 },
	 },
	},

	{.name = "Base Gyro",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &bmi160_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = CONFIG_SPI_ACCEL_PORT,
	 .addr = BMI160_SET_SPI_ADDRESS(CONFIG_SPI_ACCEL_PORT),
	 .default_range = 1000, /* dps */
#ifdef BOARD_KEVIN
	 .rot_standard_ref = &base_standard_ref, /* Identity Matrix. */
#else
	 .rot_standard_ref = NULL, /* Identity matrix. */
#endif
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
		 /* Sensor off in S3/S5 */
		 [SENSOR_CONFIG_EC_S3] = {
			 .odr = 0,
			 .ec_rate = 0,
		 },
		 /* Sensor off in S3/S5 */
		 [SENSOR_CONFIG_EC_S5] = {
			 .odr = 0,
			 .ec_rate = 0,
		 },
	 },
	},

#ifdef BOARD_KEVIN
	{.name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_BMA255,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bma2x2_accel_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_bma255_data,
	 .port = I2C_PORT_ACCEL,
	 .addr = BMA2x2_I2C_ADDR1,
	 .rot_standard_ref = &lid_standard_ref,
	 .default_range = 2, /* g, enough for laptop. */
	 .config = {
		/* AP: by default use EC settings */
		[SENSOR_CONFIG_AP] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		},
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		},
		/* unused */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 0,
			.ec_rate = 0,
		},
		[SENSOR_CONFIG_EC_S5] = {
			.odr = 0,
			.ec_rate = 0,
		},
	 },
	},
#endif
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);
#endif /* defined(HAS_TASK_MOTIONSENSE) */

#ifdef BOARD_GRU
static void usb_charge_resume(void)
{
	/* Turn on USB-A ports on as we go into S0 from S3. */
	gpio_set_level(GPIO_USB_A_EN, 1);
	gpio_set_level(GPIO_USB_A_CHARGE_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, usb_charge_resume, HOOK_PRIO_DEFAULT);

static void usb_charge_shutdown(void)
{
	/* Turn off USB-A ports as we go back to S5. */
	gpio_set_level(GPIO_USB_A_CHARGE_EN, 0);
	gpio_set_level(GPIO_USB_A_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, usb_charge_shutdown, HOOK_PRIO_DEFAULT);
#endif
