/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Kahlee board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "als.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "driver/als_al3010.h"
#include "driver/accel_kionix.h"
#include "driver/charger/isl923x.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "driver/temp_sensor/g78x.h"
#include "pi3usb9281.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_angle.h"
#include "lid_switch.h"
#include "math_util.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "fan.h"
#include "fan_chip.h"
#include "pwm_chip.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#include "timer.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static void tcpc_alert_event(enum gpio_signal signal)
{
	if ((signal == GPIO_USB_C0_PD_INT_ODL) &&
			!gpio_get_level(GPIO_USB_C0_PD_RST_ODL))
		return;

	if ((signal == GPIO_USB_C1_PD_INT_ODL) &&
			!gpio_get_level(GPIO_USB_C1_PD_RST_ODL))
		return;

#ifdef HAS_TASK_PDCMD
	/* Exchange status with TCPCs */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
#endif
}

void usb0_evt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12, 0);
}

void usb1_evt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P1, USB_CHG_EVENT_BC12, 0);
}

#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_PCH_SLP_S3_L, POWER_SIGNAL_ACTIVE_HIGH, "SLP_S3_DEASSERTED"},
	{GPIO_PCH_SLP_S5_L, POWER_SIGNAL_ACTIVE_HIGH, "SLP_S5_DEASSERTED"},
	{GPIO_S5_PGOOD,	    POWER_SIGNAL_ACTIVE_HIGH, "S5_PGOOD_DEASSERTED"},
	{GPIO_P095VALW_PG,  POWER_SIGNAL_ACTIVE_HIGH, "0.95VALW_DEASSERTED"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* Temperature sensors data */
const struct temp_sensor_t temp_sensors[] = {
	{"G781_Internal", TEMP_SENSOR_TYPE_BOARD, g78x_get_val,
		G78X_TEMP_LOCAL, 4},
	{"G781_Sensor_1", TEMP_SENSOR_TYPE_BOARD, g78x_get_val,
		G78X_TEMP_REMOTE1, 4},
	{"Battery", TEMP_SENSOR_TYPE_BATTERY, charge_get_battery_temp,
		0, 4},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* ALS instances. Must be in same order as enum als_id. */
struct als_t als[] = {
	{"ISL", al3010_init, al3010_read_lux, 5},
};
BUILD_ASSERT(ARRAY_SIZE(als) == ALS_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vfs = Vref = 2.816V, 10-bit unsigned reading */
	[ADC_IMON1] = {
		"PD1", NPCX_ADC_CH0, ADC_MAX_VOLT, ADC_READ_MAX + 1, 0
	},
	[ADC_IMON2] = {
		"PD2", NPCX_ADC_CH1, ADC_MAX_VOLT, ADC_READ_MAX + 1, 0
	},
	[ADC_BOARD_ID] = {
		"BRD_ID", NPCX_ADC_CH2, ADC_MAX_VOLT, ADC_READ_MAX + 1, 0
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_FAN] = { 0, 0, 25000 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/******************************************************************************/
/* Physical fans. These are logically separate from pwm_channels. */
const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = 0,	/* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};

const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 1000,
	.rpm_start = 1000,
	.rpm_max = 4300,
};

struct fan_t fans[] = {
	[FAN_CH_0] = { .conf = &fan_conf_0, .rpm = &fan_rpm_0, },
};
BUILD_ASSERT(ARRAY_SIZE(fans) == FAN_CH_COUNT);

/* MFT channels. These are logically separate from mft_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = { NPCX_MFT_MODULE_1, TCKC_LFCLK, PWM_CH_FAN},
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);
/******************************************************************************/

const struct i2c_port_t i2c_ports[]  = {
	{"tcpc0",     NPCX_I2C_PORT0_0, 400,
		GPIO_EC_I2C_USB_C0_PD_SCL, GPIO_EC_I2C_USB_C0_PD_SDA},
	{"tcpc1",     NPCX_I2C_PORT0_1, 400,
		GPIO_EC_I2C_USB_C1_PD_SCL, GPIO_EC_I2C_USB_C1_PD_SDA},
	{"thermal",     I2C_PORT_THERMAL,   400,
		GPIO_EC_I2C_THERMAL_SCL,      GPIO_EC_I2C_THERMAL_SDA},
	{"accelgyro",   NPCX_I2C_PORT2,   400,
		GPIO_EC_I2C_SENSOR_SCL,    GPIO_EC_I2C_SENSOR_SDA},
	{"batt",      NPCX_I2C_PORT3,   100,
		GPIO_EC_I2C_POWER_SCL,     GPIO_EC_I2C_POWER_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

struct pi3usb9281_config pi3usb9281_chips[] = {
	{
		.i2c_port = NPCX_I2C_PORT0_0,
		.mux_lock = NULL,
	},
	{
		.i2c_port = NPCX_I2C_PORT0_1,
		.mux_lock = NULL,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9281_chips) ==
	     CONFIG_BC12_DETECT_PI3USB9281_CHIP_COUNT);

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	[0] = {
		.i2c_host_port = NPCX_I2C_PORT0_0,
		.i2c_slave_addr = PS8751_I2C_ADDR1,
		.drv = &ps8xxx_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},
	[1] = {
		.i2c_host_port = NPCX_I2C_PORT0_1,
		.i2c_slave_addr = PS8751_I2C_ADDR1,
		.drv = &ps8xxx_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},
};

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C0_PD_RST_ODL))
			status |= PD_STATUS_TCPC_ALERT_0;
	}

	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C1_PD_RST_ODL))
			status |= PD_STATUS_TCPC_ALERT_1;
	}

	return status;
}

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_AC_PRESENT,
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
};

const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.port_addr = 0,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	},
	{
		.port_addr = 1,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	}
};

const int usb_port_enable[CONFIG_USB_PORT_POWER_SMART_PORT_COUNT] = {
	GPIO_USB1_ENABLE,
};

/**
 * Reset PD MCU -- currently only called from handle_pending_reboot() in
 * common/power.c just before hard resetting the system. This logic is likely
 * not needed as the PP3300_A rail should be dropped on EC reset.
 */
void board_reset_pd_mcu(void)
{
	/* Assert reset to TCPC1 */
	gpio_set_level(GPIO_USB_C1_PD_RST_ODL, 0);

	/* Assert reset to TCPC0 */
	gpio_set_level(GPIO_USB_C0_PD_RST_ODL, 0);

	/* TCPC0 requires 10ms reset/power down assertion */
	msleep(10);

	/* Deassert reset to TCPC1 */
	gpio_set_level(GPIO_USB_C1_PD_RST_ODL, 1);

	/* Deassert reset to TCPC0 */
	gpio_set_level(GPIO_USB_C0_PD_RST_ODL, 1);
}

void board_tcpc_init(void)
{
	int port;

	/* Only reset TCPC if not sysjump */
	if (!system_jumped_to_this_image())
		board_reset_pd_mcu();

	/* Enable TCPC0 interrupt */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);

	/* Enable TCPC1 interrupt */
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);

	/*
	* Initialize HPD to low; after sysjump SOC needs to see
	* HPD pulse to enable video path
	*/
	for (port = 0; port < CONFIG_USB_PD_PORT_COUNT; port++) {
		const struct usb_mux *mux = &usb_muxes[port];

		mux->hpd_update(port, 0, 0);
	}
}
DECLARE_HOOK(HOOK_INIT, board_tcpc_init, HOOK_PRIO_INIT_I2C+1);

/* Initialize board. */
static void board_init(void)
{
	/* Enable pericom BC1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_L);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_L);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_FIRST);

/*
 * TODO(b/63514169)
 * There is no VBUS detect pin in proto phase, EC needs to
 * get VBUS information from BC1.2 chip. HW will add VBUS
 * detect pin in EVT phase and EC can get VBUS status from
 * GPIO.
*/
int check_vbus_status(int port)
{
	int reg;

	i2c_read8(pi3usb9281_chips[port].i2c_port,
		0x4A, PI3USB9281_REG_VBUS, &reg);

	return ((reg & 0x02) >> 1);
}

void update_vbus_status(void)
{
	uint8_t port, vbus;

	for (port = 0; port < CONFIG_USB_PD_PORT_COUNT; port++) {
		vbus = check_vbus_status(port);
		usb_charger_vbus_change(port, vbus);
		task_wake(port ? TASK_ID_PD_C1 : TASK_ID_PD_C0);
	}
}

/*
 * TODO(b/63514169)
 * Check VBUS status when AC changes to update the charge source information.
 * We will modify it to gpio interrupt control when HW adds vbus status pin
 * in EVT phase.
*/
static void board_extpower(void)
{
	update_vbus_status();
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_extpower, HOOK_PRIO_DEFAULT);

int pd_snk_is_vbus_provided(int port)
{
	return check_vbus_status(port);
}

/**
 * Set active charge port -- only one port can be active at a time.
 *
 * @param charge_port   Charge port to enable.
 *
 * Returns EC_SUCCESS if charge port is accepted and made active,
 * EC_ERROR_* otherwise.
 */
int board_set_active_charge_port(int charge_port)
{
	switch (charge_port) {
	case 0:
		/* Don't charge from a source port */
		if (board_vbus_source_enabled(charge_port))
			return -1;

		gpio_set_level(GPIO_USB_C0_5V_EN, 0);
		gpio_set_level(GPIO_USB_C0_20V_EN, 1);
		break;
	case 1:
		/* Don't charge from a source port */
		if (board_vbus_source_enabled(charge_port))
			return -1;

		gpio_set_level(GPIO_USB_C1_5V_EN, 0);
		gpio_set_level(GPIO_USB_C1_20V_EN, 1);
		break;
	case CHARGE_PORT_NONE:
		gpio_set_level(GPIO_USB_C0_20V_EN, 0);
		gpio_set_level(GPIO_USB_C1_20V_EN, 0);
		break;
	default:
		panic("Invalid charge port\n");
		break;
	}

	CPRINTS("New chg p%d", charge_port);

	return EC_SUCCESS;
}

/**
 * Set the charge limit based upon desired maximum.
 *
 * @param port          Port number.
 * @param supplier      Charge supplier type.
 * @param charge_ma     Desired charge limit (mA).
 * @param charge_mv     Negotiated charge voltage (mV).
 */
void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	charge_ma = (charge_ma * 95) / 100;
	charge_set_input_current_limit(MAX(charge_ma,
				   CONFIG_CHARGER_INPUT_CURRENT), charge_mv);
}

/**
 * Return if VBUS is sagging too low
 */
int board_is_vbus_too_low(int port, enum chg_ramp_vbus_state ramp_state)
{
	return 0;
}

/* Called on AP S5 -> S3 transition */
static void board_chipset_startup(void)
{
	/* Enable USB-A port. */
	gpio_set_level(GPIO_USB1_ENABLE, 1);

	/* Enable Trackpad */
	gpio_set_level(GPIO_EN_TRACKPAD, 1);

	/* Enable Touchscreen */
	gpio_set_level(GPIO_EN_TOUCHSCREEN, 1);

	/* Enable Codec */
	gpio_set_level(GPIO_EN_ALC_CLK, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void board_chipset_shutdown(void)
{
	/* Disable USB-A port. */
	gpio_set_level(GPIO_USB1_ENABLE, 0);

	/* Disable Trackpad */
	gpio_set_level(GPIO_EN_TRACKPAD, 0);

	/* Disable Touchscreen */
	gpio_set_level(GPIO_EN_TOUCHSCREEN, 0);

	/* Disable Codec */
	gpio_set_level(GPIO_EN_ALC_CLK, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

static void board_chipset_resume(void)
{
	/* Turn on display backlight. */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_chipset_resume, HOOK_PRIO_DEFAULT);

static void board_chipset_suspend(void)
{
	/* Turn off display backlight. */
	gpio_set_level(GPIO_ENABLE_BACKLIGHT, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_chipset_suspend, HOOK_PRIO_DEFAULT);

void chipset_do_shutdown(void)
{

}

void board_hibernate_late(void)
{
	int i;
	const uint32_t hibernate_pins[][2] = {
		/* Turn off LEDs in hibernate */
		{GPIO_BAT_LED_GREEN, GPIO_INPUT | GPIO_PULL_UP},
		{GPIO_BAT_LED_AMBER, GPIO_INPUT | GPIO_PULL_UP},
		{GPIO_PWR_LED_GREEN, GPIO_INPUT | GPIO_PULL_UP},
		{GPIO_LID_OPEN, GPIO_INT_RISING | GPIO_PULL_DOWN},

		{GPIO_USB_C0_5V_EN,       GPIO_INPUT  | GPIO_PULL_DOWN},
		{GPIO_USB_C1_5V_EN,       GPIO_INPUT  | GPIO_PULL_DOWN},
	};

	/* Change GPIOs' state in hibernate for better power consumption */
	for (i = 0; i < ARRAY_SIZE(hibernate_pins); ++i)
		gpio_set_flags(hibernate_pins[i][0], hibernate_pins[i][1]);

	gpio_config_module(MODULE_KEYBOARD_SCAN, 0);

	/*
	 * Calling gpio_config_module sets disabled alternate function pins to
	 * GPIO_INPUT.  But to prevent keypresses causing leakage currents
	 * while hibernating we want to enable GPIO_PULL_UP as well.
	 */
	gpio_set_flags_by_mask(0x2, 0x03, GPIO_INPUT | GPIO_PULL_UP);
	gpio_set_flags_by_mask(0x1, 0x7F, GPIO_INPUT | GPIO_PULL_UP);
	gpio_set_flags_by_mask(0x0, 0xE0, GPIO_INPUT | GPIO_PULL_UP);
	/* KBD_KSO2 needs to have a pull-down enabled instead of pull-up */
	gpio_set_flags_by_mask(0x1, 0x80, GPIO_INPUT | GPIO_PULL_DOWN);
}

/* Motion sensors */
/* Mutexes */
static struct mutex g_lid_mutex;
struct kionix_accel_data g_kxcj9_data;

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
	 .name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_KXCJ9,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &kionix_accel_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_kxcj9_data,
	 .port = I2C_PORT_LID_ACCEL,
	 .addr = KXCJ9_ADDR1,
	 .rot_standard_ref = NULL, /* Identity matrix. */
	 .default_range = 2, /* g, enough for laptop. */
	 .min_frequency = KXCJ9_ACCEL_MIN_FREQ,
	 .max_frequency = KXCJ9_ACCEL_MAX_FREQ,
	 .config = {
		/* Setup for AP for rotation detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	 },
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

void board_hibernate(void)
{
	/*
	 * To support hibernate called from console commands, ectool commands
	 * and key sequence, shutdown the AP before hibernating.
	 */
	chipset_do_shutdown();

	/* Added delay to allow AP to settle down */
	msleep(100);
}

struct {
	enum board_version version;
	int thresh_mv;
} const board_versions[] = {
	/* Vin = 3.3V, R1 = 46.4K, R2 values listed below */
	{ BOARD_VERSION_1, 328 * 1.03 },  /* 5.11 Kohm */
	{ BOARD_VERSION_2, 670 * 1.03 },  /* 11.8 Kohm */
	{ BOARD_VERSION_3, 1012 * 1.03 }, /* 20.5 Kohm */
	{ BOARD_VERSION_4, 1357 * 1.03 }, /* 32.4 Kohm */
	{ BOARD_VERSION_5, 1690 * 1.03 }, /* 48.7 Kohm */
	{ BOARD_VERSION_6, 2020 * 1.03 }, /* 73.2 Kohm */
	{ BOARD_VERSION_7, 2352 * 1.03 }, /* 115 Kohm */
	{ BOARD_VERSION_8, 2802 * 1.03 }, /* 261 Kohm */
};
BUILD_ASSERT(ARRAY_SIZE(board_versions) == BOARD_VERSION_COUNT);

int board_get_version(void)
{
	static int version = BOARD_VERSION_UNKNOWN;
	int mv, i;

	if (version != BOARD_VERSION_UNKNOWN)
		return version;

	/* FIXME(dhendrix): enable ADC */
	gpio_set_flags(GPIO_EC_BRD_ID_EN_ODL, GPIO_ODR_HIGH);
	gpio_set_level(GPIO_EC_BRD_ID_EN_ODL, 0);
	/* Wait to allow cap charge */
	msleep(1);
	mv = adc_read_channel(ADC_BOARD_ID);
	/* FIXME(dhendrix): disable ADC */
	gpio_set_level(GPIO_EC_BRD_ID_EN_ODL, 1);
	gpio_set_flags(GPIO_EC_BRD_ID_EN_ODL, GPIO_INPUT);

	if (mv == ADC_READ_ERROR) {
		version = BOARD_VERSION_UNKNOWN;
		return version;
	}

	for (i = 0; i < BOARD_VERSION_COUNT; i++) {
		if (mv < board_versions[i].thresh_mv) {
			version = board_versions[i].version;
			break;
		}
	}

	CPRINTS("Board version: %d\n", version);
	return version;
}

/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
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
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};
