/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Reef board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "als.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "driver/als_opt3001.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/charger/bd99955.h"
#include "driver/tcpm/anx74xx.h"
#include "driver/tcpm/tcpci.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "math_util.h"
#include "motion_sense.h"
#include "motion_lid.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
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

#define IN_ALL_SYS_PG	POWER_SIGNAL_MASK(X86_ALL_SYS_PG)
#define IN_PGOOD_PP3300	POWER_SIGNAL_MASK(X86_PGOOD_PP3300)
#define IN_PGOOD_PP5000	POWER_SIGNAL_MASK(X86_PGOOD_PP5000)

static void tcpc_alert_event(enum gpio_signal signal)
{
	if (!gpio_get_level(GPIO_USB_PD_RST_ODL))
		return;

#ifdef HAS_TASK_PDCMD
	/* Exchange status with TCPCs */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
#endif
}


/* Exchange status with PD MCU. */
static void pd_mcu_interrupt(enum gpio_signal signal)
{
#ifdef HAS_TASK_PDCMD
	/* Exchange status with PD MCU to determine interrupt cause */
	host_command_pd_send_status(0);
#endif
}

/*
 * enable_input_devices() is called by the tablet_mode ISR, but changes the
 * state of GPIOs, so its definition must reside after including gpio_list.
 * Use DECLARE_DEFERRED to generate enable_input_devices_data.
 */
static void enable_input_devices(void);
DECLARE_DEFERRED(enable_input_devices);

void tablet_mode_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&enable_input_devices_data, 0);
}

#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_RSMRST_L_PGOOD,     1, "RSMRST_L"},
	{GPIO_PCH_SLP_S0_L,       1, "PMU_SLP_S0_N"},
	{GPIO_PCH_SLP_S3_L,       1, "SLP_S3_DEASSERTED"},
	{GPIO_PCH_SLP_S4_L,       1, "SLP_S4_DEASSERTED"},
	{GPIO_SUSPWRNACK,         1, "SUSPWRNACK_DEASSERTED"},

	{GPIO_ALL_SYS_PGOOD,      1, "ALL_SYS_PGOOD"},
	{GPIO_PP3300_PG,          1, "PP3300_PG"},
	{GPIO_PP5000_PG,          1, "PP5000_PG"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_BOARD_ID] = {"BOARD_ID", NPCX_ADC_CH2, 1, 1, 0},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_LED_GREEN] = { 2, PWM_CONFIG_DSLEEP, 100 },
	[PWM_CH_LED_RED] =   { 3, PWM_CONFIG_DSLEEP, 100 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

const struct i2c_port_t i2c_ports[]  = {
	{"tcpc0",     NPCX_I2C_PORT0_0, 400,
		GPIO_EC_I2C_USB_C0_PD_SCL, GPIO_EC_I2C_USB_C0_PD_SDA},
	{"tcpc1",     NPCX_I2C_PORT0_1, 400,
		GPIO_EC_I2C_USB_C1_PD_SCL, GPIO_EC_I2C_USB_C1_PD_SDA},
	{"gyro",      I2C_PORT_GYRO,   400,
		GPIO_EC_I2C_GYRO_SCL,      GPIO_EC_I2C_GYRO_SDA},
	{"sensors",   NPCX_I2C_PORT2,   400,
		GPIO_EC_I2C_SENSOR_SCL,    GPIO_EC_I2C_SENSOR_SDA},
	{"batt",      NPCX_I2C_PORT3,   100,
		GPIO_EC_I2C_POWER_SCL,     GPIO_EC_I2C_POWER_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	{NPCX_I2C_PORT0_0, 0x50, &anx74xx_tcpm_drv},
	{NPCX_I2C_PORT0_1, 0x16, &tcpci_tcpm_drv},
};

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (gpio_get_level(GPIO_USB_C0_PD_INT))
		status |= PD_STATUS_TCPC_ALERT_0;
	if (!gpio_get_level(GPIO_USB_C1_PD_INT_ODL))
		status |= PD_STATUS_TCPC_ALERT_1;

	return status;
}

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
};

const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.port_addr = 0,	/* don't care / unused */
		.driver = &anx74xx_tcpm_usb_mux_driver,
	},
	{
		.port_addr = 1,
		.driver = &tcpci_tcpm_usb_mux_driver,
	}
};

/* called from anx74xx_set_power_mode() */
void board_set_tcpc_power_mode(int port, int mode)
{
	gpio_set_level(GPIO_EN_USB_TCPC_PWR, mode);
	msleep(1);

	/* FIXME(dhendrix): This is also connected to the PS8751 which
	 * we might not want to reset just because something happened
	 * on the ANX3429. */
	gpio_set_level(GPIO_USB_PD_RST_ODL, mode);
	msleep(10);
}

/**
 * Reset PD MCU -- currently only called from handle_pending_reboot() in
 * common/power.c just before hard resetting the system. This logic is likely
 * not needed as the PP3300_A rail should be dropped on EC reset.
 */
void board_reset_pd_mcu(void)
{
	gpio_set_level(GPIO_USB_PD_RST_ODL, 0);
	msleep(1);
	gpio_set_level(GPIO_EN_USB_TCPC_PWR, 0);
	msleep(10);

	gpio_set_level(GPIO_EN_USB_TCPC_PWR, 1);
	msleep(1);
	gpio_set_level(GPIO_USB_PD_RST_ODL, 1);
	/*
	 * ANX7688 needed 50ms to release RESET_N, but the ANX7428 datasheet
	 * does not indicate such a long delay is necessary. Leave it in due
	 * to paranoia.
	 */
	msleep(50);
}

int board_get_battery_temp(int idx, int *temp_ptr)
{
	/* FIXME(dhendrix): Read THERM_VAL from BD99956 and convert
	   Celsius to Kelvin */
	*temp_ptr = 0;
	return 0;

}

int board_get_charger_temp(int idx, int *temp_ptr)
{
	int raw_val = adc_read_channel(NPCX_ADC_CH0);

	if (raw_val < 0)
		return -1;

	/* FIXME(dhendrix): Add data points and calculate using CL:344781 */
	*temp_ptr = 0;
	return 0;
}

int board_get_ambient_temp(int idx, int *temp_ptr)
{
	int raw_val = adc_read_channel(NPCX_ADC_CH1);

	if (raw_val < 0)
		return -1;

	/* FIXME(dhendrix): Add data points and calculate using CL:344781 */
	*temp_ptr = 0;
	return 0;
}


const struct temp_sensor_t temp_sensors[] = {
	/* FIXME(dhendrix): tweak action_delay_sec */
	{"Battery", TEMP_SENSOR_TYPE_BATTERY, board_get_battery_temp, 0, 1},
	{"Ambient", TEMP_SENSOR_TYPE_BOARD, board_get_ambient_temp, 0, 5},
	{"Charger", TEMP_SENSOR_TYPE_BOARD, board_get_charger_temp, 0, 1},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * Thermal limits for each temp sensor.  All temps are in degrees K.  Must be in
 * same order as enum temp_sensor_id.  To always ignore any temp, use 0.
 */
struct ec_thermal_config thermal_params[] = {
	/* {Twarn, Thigh, Thalt}, fan_off, fan_max */
	/* FIXME(dhendrix): Implement this... */
	{{0, 0, 0}, 0, 0},	/* Battery */
	{{0, 0, 0}, 0, 0},	/* Ambient */
	{{0, 0, 0}, 0, 0},	/* Charger */
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

/* ALS instances. Must be in same order as enum als_id. */
struct als_t als[] = {
	/* FIXME(dhendrix): verify attenuation_factor */
	{"TI", opt3001_init, opt3001_read_lux, 5},
};
BUILD_ASSERT(ARRAY_SIZE(als) == ALS_COUNT);

const struct button_config buttons[CONFIG_BUTTON_COUNT] = {
	{"Volume Down", KEYBOARD_BUTTON_VOLUME_DOWN, GPIO_EC_VOLDN_BTN_L,
	 30 * MSEC, 0},
	{"Volume Up", KEYBOARD_BUTTON_VOLUME_UP, GPIO_EC_VOLUP_BTN_L,
	 30 * MSEC, 0},
};

/* Called by APL power state machine when transitioning from G3 to S5 */
static void chipset_pre_init(void)
{
#if 0
	/* No need to re-init PMIC since settings are sticky across sysjump */
	/* TODO(dhendrix): Handle sysjump case appropriately */
	if (system_jumped_to_this_image())
		return;
#endif

	/* Enable PP5000 before PP3300 due to NFC: chrome-os-partner:50807 */
	gpio_set_level(GPIO_EN_PP5000, 1);
	udelay(6);	/* Double the PG low to high delay for power supply. */

	/* Enable 3.3V rail */
	gpio_set_level(GPIO_EN_PP3300, 1);
	udelay(1500);	/* Double the PG low to high delay for converter. */

	/* FIXME: for debugging */
	cprintf(CC_HOOK, "PP3300_PG: %d", gpio_get_level(GPIO_PP3300_PG));
	cprintf(CC_HOOK, "PP5000_PG: %d", gpio_get_level(GPIO_PP5000_PG));

	/* (Re-)Enable I2C */
	gpio_config_module(MODULE_I2C, 1);
#if 0
	/* Enable PD interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT);
	gpio_enable_interrupt(GPIO_USB_C1_PD_INT_ODL);

	/* Enable charger interrupts */
	gpio_enable_interrupt(GPIO_PD_MCU_INT);
#endif
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, chipset_pre_init, HOOK_PRIO_DEFAULT);

/* Initialize board. */
static void board_init(void)
{
	/* FIXME: Handle tablet mode */
	/* gpio_enable_interrupt(GPIO_TABLET_MODE_L); */

	struct charge_port_info charge_none;
	int i;

	/* Initialize all BC1.2 charge suppliers to 0 */
	charge_none.voltage = USB_CHARGER_VOLTAGE_MV;
	charge_none.current = 0;

	/* TODO: Implement BC1.2 + VBUS detection */
	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; i++) {
		charge_manager_update_charge(CHARGE_SUPPLIER_PROPRIETARY,
					     i,
					     &charge_none);
		charge_manager_update_charge(CHARGE_SUPPLIER_BC12_CDP,
					     i,
					     &charge_none);
		charge_manager_update_charge(CHARGE_SUPPLIER_BC12_DCP,
					     i,
					     &charge_none);
		charge_manager_update_charge(CHARGE_SUPPLIER_BC12_SDP,
					     i,
					     &charge_none);
		charge_manager_update_charge(CHARGE_SUPPLIER_OTHER,
					     i,
					     &charge_none);
		charge_manager_update_charge(CHARGE_SUPPLIER_VBUS,
					     i,
					     &charge_none);
	}
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

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
	enum bd99955_charge_port bd99955_port;

	/* charge port is a physical port */
	int is_real_port = (charge_port >= 0 &&
			    charge_port < CONFIG_USB_PD_PORT_COUNT);
	/* check if we are source VBUS on the port */
	int source = gpio_get_level(charge_port == 0 ? GPIO_USB_C0_5V_EN :
						       GPIO_USB_C1_5V_EN);

	if (is_real_port && source) {
		CPRINTF("Skip enable p%d", charge_port);
		return EC_ERROR_INVAL;
	}

	CPRINTS("New chg p%d", charge_port);

	switch (charge_port) {
	case 0:
		bd99955_port = BD99955_CHARGE_PORT_VBUS;
		break;
	case 1:
		bd99955_port = BD99955_CHARGE_PORT_VCC;
		break;
	case CHARGE_PORT_NONE:
		bd99955_port = BD99955_CHARGE_PORT_NONE;
		break;
	default:
		panic("Invalid charge port\n");
		break;
	}

	return bd99955_select_input_port(bd99955_port);
}

/**
 * Set the charge limit based upon desired maximum.
 *
 * @param port          Port number.
 * @param supplier      Charge supplier type.
 * @param charge_ma     Desired charge limit (mA).
 */
void board_set_charge_limit(int port, int supplier, int charge_ma)
{
	charge_set_input_current_limit(MAX(charge_ma,
					   CONFIG_CHARGER_INPUT_CURRENT));
}

int extpower_is_present(void)
{
	return bd99955_extpower_is_present();
}

int usb_charger_port_is_sourcing_vbus(int port)
{
	return gpio_get_level(port ? GPIO_USB_C1_5V_EN : GPIO_USB_C0_5V_EN);
}

/* Enable or disable input devices, based upon chipset state and tablet mode */
static void enable_input_devices(void)
{
	int kb_enable = 1;

	/* Disable KB if chipset is off */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		kb_enable = 0;

	keyboard_scan_enable(kb_enable, KB_SCAN_DISABLE_LID_ANGLE);
}

/* Called on AP S5 -> S3 transition */
static void board_chipset_startup(void)
{
	static int need_to_enable_sleep_interrupt = 1;

	/*
	 * SLP_Sn signals may be glitchy before V5A and PMIC are both on
	 * so wait until we're exiting S5 to enable SLP_Sn interrupts.
	 * See chrome-os-partner:51323 for details.
	 */
	if (need_to_enable_sleep_interrupt) {
		gpio_enable_interrupt(GPIO_PCH_SLP_S4_L);
		gpio_enable_interrupt(GPIO_PCH_SLP_S3_L);
		gpio_enable_interrupt(GPIO_PCH_SLP_S0_L);
		need_to_enable_sleep_interrupt = 0;
	}

	/* Enable USB-A port. */
	gpio_set_level(GPIO_EN_USB_A_5V, 1);

	hook_call_deferred(&enable_input_devices_data, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void board_chipset_shutdown(void)
{
	/* Disable USB-A port. */
	gpio_set_level(GPIO_EN_USB_A_5V, 1);

	hook_call_deferred(&enable_input_devices_data, 0);
	/* FIXME(dhendrix): Drive USB_PD_RST_ODL low to prevent
	   leakage? (see comment in schematic) */
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

/* FIXME(dhendrix): Add CHIPSET_RESUME and CHIPSET_SUSPEND
   hooks to enable/disable sensors? */

/*
 * FIXME(dhendrix): Weak symbol hack until we can get a better solution for
 * both Amenia and Reef.
 */
void chipset_do_shutdown(void)
{
	cprintf(CC_CHIPSET, "Doing custom shutdown for Reef\n");

	/* Disable I2C module */
	gpio_config_module(MODULE_I2C, 0);

	gpio_set_level(GPIO_EN_USB_TCPC_PWR, 0);
	/* Disable V5A which de-assert PMIC_EN and causes PMIC to shutdown. */
	gpio_set_level(GPIO_V5A_EN, 0);
	gpio_set_level(GPIO_EN_PP3300, 0);
	gpio_set_level(GPIO_EN_PP5000, 0);
}

void board_set_gpio_hibernate_state(void)
{
	int i;
	const uint32_t hibernate_pins[][2] = {
#if 0
		/* Turn off LEDs in hibernate */
		{GPIO_BAT_LED_BLUE, GPIO_INPUT | GPIO_PULL_UP},
		{GPIO_BAT_LED_AMBER, GPIO_INPUT | GPIO_PULL_UP},
		/*
		 * Set PD wake low so that it toggles high to generate a wake
		 * event once we leave hibernate.
		 */
		{GPIO_USB_PD_WAKE, GPIO_OUTPUT | GPIO_LOW},
#endif
		/*
		 * In hibernate, this pin connected to GND. Set it to output
		 * low to eliminate the current caused by internal pull-up.
		 */
		/* FIXME(dhendrix): What do to with PROCHOT? */
		/* {GPIO_PLATFORM_EC_PROCHOT, GPIO_OUTPUT | GPIO_LOW}, */

		/*
		 * BD99956 handles charge input automatically. We'll disable
		 * charge output in hibernate. Charger will assert ACOK_OD
		 * when VBUS or VCC are plugged in.
		 */
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
	gpio_set_flags_by_mask(0x1, 0xFF, GPIO_INPUT | GPIO_PULL_UP);
	gpio_set_flags_by_mask(0x0, 0xE0, GPIO_INPUT | GPIO_PULL_UP);
}

/* Motion sensors */
/* Mutexes */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* Matrix to rotate accelrator into standard reference frame */
const matrix_3x3_t base_standard_ref = {
	{ 0,  FLOAT_TO_FP(1),  0},
	{ FLOAT_TO_FP(-1), 0,  0},
	{ 0,  0,  FLOAT_TO_FP(1)}
};

/* KX022 private data */
struct kionix_accel_data g_kx022_data = {
	.variant = KX022,
};

/* FIXME(dhendrix): Copied from Amenia, probably need to tweak for Reef */
struct motion_sensor_t motion_sensors[] = {
	/*
	 * Note: bmi160: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	[LID_ACCEL] = {
	 .name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bmi160_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_ACCEL,
	 .addr = BMI160_ADDR0,
	 .rot_standard_ref = NULL, /* Identity matrix. */
	 .default_range = 2,  /* g, enough for laptop. */
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

	[LID_GYRO] = {
	 .name = "Lid Gyro",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bmi160_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_GYRO,
	 .addr = BMI160_ADDR0,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = NULL, /* Identity Matrix. */
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

	[LID_MAG] = {
	 .name = "Lid Mag",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_BMI160,
	 .type = MOTIONSENSE_TYPE_MAG,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &bmi160_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_bmi160_data,
	 .port = I2C_PORT_ACCEL,
	 .addr = BMI160_ADDR0,
	 .default_range = 1 << 11, /* 16LSB / uT, fixed */
	 .rot_standard_ref = NULL, /* Identity Matrix. */
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

	[BASE_ACCEL] = {
	 .name = "Base Accel",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_KXCJ9,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &kionix_accel_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_kx022_data,
	 .port = I2C_PORT_ACCEL,
	 .addr = KXCJ9_ADDR1,
	 .rot_standard_ref = &base_standard_ref, /* Identity matrix. */
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
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

void board_hibernate(void)
{
	CPRINTS("Enter Pseudo G3");

	/*
	 * Clean up the UART buffer and prevent any unwanted garbage characters
	 * before power off and also ensure above debug message is printed.
	 */
	cflush();

	/* FIXME(dhendrix): What to do here? EC is always on so we need to
	 * turn off whatever can be turned off. */
}

enum reef_board_version {
	BOARD_VERSION_UNKNOWN = -1,
	BOARD_VERSION_1,
	BOARD_VERSION_2,
	BOARD_VERSION_3,
	BOARD_VERSION_4,
	BOARD_VERSION_5,
	BOARD_VERSION_6,
	BOARD_VERSION_7,
	BOARD_VERSION_8,
	BOARD_VERSION_COUNT,
};

struct {
	enum reef_board_version version;
	int thresh_mv;
} const reef_board_versions[] = {
	{ BOARD_VERSION_1, 330 },  /* 5.11 Kohm */
	{ BOARD_VERSION_2, 670 },  /* 11.8 Kohm */
	{ BOARD_VERSION_3, 1010 }, /* 20.5 Kohm */
	{ BOARD_VERSION_4, 1390 }, /* 32.4 Kohm */
	{ BOARD_VERSION_5, 1690 }, /* 48.7 Kohm */
	{ BOARD_VERSION_6, 2020 }, /* 73.2 Kohm */
	{ BOARD_VERSION_7, 2350 }, /* 115 Kohm */
	{ BOARD_VERSION_8, 2800 }, /* 261 Kohm */
};
BUILD_ASSERT(ARRAY_SIZE(reef_board_versions) == BOARD_VERSION_COUNT);

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
		if (mv < reef_board_versions[i].thresh_mv) {
			version = reef_board_versions[i].version;
			break;
		}
	}

	return version;
}
