/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Skylake Chrome Reference Design board-specific configuration */

#include "adc_chip.h"
#include "als.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "console.h"
#include "driver/accel_kxcj9.h"
#include "driver/als_opt3001.h"
#include "driver/temp_sensor/tmp432.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "lid_switch.h"
#include "math_util.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "pi3usb9281.h"
#include "power.h"
#include "power_button.h"
#include "spi.h"
#include "switch.h"
#include "task.h"
#include "temp_sensor.h"
#include "temp_sensor_chip.h"
#include "thermal.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define GPIO_KB_INPUT (GPIO_INPUT | GPIO_PULL_UP)
#define GPIO_KB_OUTPUT (GPIO_ODR_HIGH)
#define GPIO_KB_OUTPUT_COL2 (GPIO_OUT_LOW)

/* Exchange status with PD MCU. */
static void pd_mcu_interrupt(enum gpio_signal signal)
{
	/* Exchange status with PD MCU to determine interrupt cause */
	host_command_pd_send_status(0);
}

static void update_vbus_supplier(int port, int vbus_level)
{
	struct charge_port_info charge;

	/*
	 * If VBUS is low, or VBUS is high and we are not outputting VBUS
	 * ourselves, then update the VBUS supplier.
	 */
	if (!vbus_level || !usb_charger_port_is_sourcing_vbus(port)) {
		charge.voltage = USB_CHARGER_VOLTAGE_MV;
		charge.current = vbus_level ? USB_CHARGER_MIN_CURR_MA : 0;
		charge_manager_update_charge(CHARGE_SUPPLIER_VBUS,
					     port,
					     &charge);
	}
}

void vbus0_evt(enum gpio_signal signal)
{
	/* VBUS present GPIO is inverted */
	int vbus_level = !gpio_get_level(signal);

	CPRINTF("VBUS C0, %d\n", vbus_level);
	update_vbus_supplier(0, vbus_level);
	task_wake(TASK_ID_PD_C0);
}

void vbus1_evt(enum gpio_signal signal)
{
	/* VBUS present GPIO is inverted */
	int vbus_level = !gpio_get_level(signal);

	CPRINTF("VBUS C1, %d\n", vbus_level);
	update_vbus_supplier(0, vbus_level);
	task_wake(TASK_ID_PD_C1);
}

void usb0_evt(enum gpio_signal signal)
{
	task_wake(TASK_ID_USB_CHG_P0);
}

void usb1_evt(enum gpio_signal signal)
{
	task_wake(TASK_ID_USB_CHG_P1);
}

#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_RSMRST_L_PGOOD,    1, "RSMRST_N_PWRGD"},
	{GPIO_PCH_SLP_S0_L,      1, "SLP_S0_DEASSERTED"},
	{GPIO_PCH_SLP_S3_L,      1, "SLP_S3_DEASSERTED"},
	{GPIO_PCH_SLP_S4_L,      1, "SLP_S4_DEASSERTED"},
	{GPIO_PCH_SLP_SUS_L,     1, "SLP_SUS_DEASSERTED"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vbus sensing. Converted to mV, full ADC is equivalent to 33V. */
	[ADC_VBUS] = {"VBUS", 33000, 1024, 0, 1},
	/* Adapter current output or battery discharging current */
	[ADC_AMON_BMON] = {"AMON_BMON", 1, 1, 0, 3},
	/* System current consumption */
	[ADC_PSYS] = {"PSYS", 1, 1, 0, 4},

};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

const struct i2c_port_t i2c_ports[]  = {
	{"batt",     MEC1322_I2C0_0, 100,  GPIO_I2C0_0_SCL, GPIO_I2C0_0_SDA},
	{"muxes",    MEC1322_I2C0_1, 100,  GPIO_I2C0_1_SCL, GPIO_I2C0_1_SDA},
	{"pd_mcu",   MEC1322_I2C1,  1000,  GPIO_I2C1_SCL,   GPIO_I2C1_SDA},
	{"sensors",  MEC1322_I2C2,   400,  GPIO_I2C2_SCL,   GPIO_I2C2_SDA  },
	{"pmic",     MEC1322_I2C3,   400,  GPIO_I2C3_SCL,   GPIO_I2C3_SDA  },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	{ CONFIG_SPI_FLASH_PORT, 0, GPIO_PVT_CS0},
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_AC_PRESENT,
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
};

const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

struct pi3usb9281_config pi3usb9281_chips[] = {
	{
		.i2c_port = I2C_PORT_USB_CHARGER_1,
		.mux_lock = NULL,
	},
	{
		.i2c_port = I2C_PORT_USB_CHARGER_2,
		.mux_lock = NULL,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pi3usb9281_chips) ==
	     CONFIG_USB_SWITCH_PI3USB9281_CHIP_COUNT);

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.port_addr = 0xa8,
		.driver = &pi3usb30532_usb_mux_driver,
	},
	{
		.port_addr = 0xaa,
		.driver = &pi3usb30532_usb_mux_driver,
	}
};

void board_set_usb_switches(int port, enum usb_switch setting)
{
	/* TODO: Set open / close USB switches based on param */
}

/**
 * Reset PD MCU
 */
void board_reset_pd_mcu(void)
{
	gpio_set_level(GPIO_PD_RST_L, 0);
	usleep(100);
	gpio_set_level(GPIO_PD_RST_L, 1);
}

/* Four Motion sensors */
/* kxcj9 mutex and local/private data*/
static struct mutex g_kxcj9_mutex[2];
struct kxcj9_data g_kxcj9_data[2];

/* Matrix to rotate accelrator into standard reference frame */
const matrix_3x3_t base_standard_ref = {
	{ 0,  FLOAT_TO_FP(1),  0},
	{FLOAT_TO_FP(-1),  0,  0},
	{ 0,  0,  FLOAT_TO_FP(1)}
};

const matrix_3x3_t lid_standard_ref = {
	{FLOAT_TO_FP(-1),  0,  0},
	{ 0, FLOAT_TO_FP(-1),  0},
	{ 0,  0, FLOAT_TO_FP(-1)}
};

struct motion_sensor_t motion_sensors[] = {
	{.name = "Base Accel",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_KXCJ9,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &kxcj9_drv,
	 .mutex = &g_kxcj9_mutex[0],
	 .drv_data = &g_kxcj9_data[0],
	 .addr = KXCJ9_ADDR1,
	 .rot_standard_ref = &base_standard_ref,
	 .default_config = {
		 .odr = 100000,
		 .range = 2,
		 .ec_rate = SUSPEND_SAMPLING_INTERVAL,
	 }
	},
	{.name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_KXCJ9,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &kxcj9_drv,
	 .mutex = &g_kxcj9_mutex[1],
	 .drv_data = &g_kxcj9_data[1],
	 .addr = KXCJ9_ADDR0,
	 .rot_standard_ref = &lid_standard_ref,
	 .default_config = {
		 .odr = 100000,
		 .range = 2,
		 .ec_rate = SUSPEND_SAMPLING_INTERVAL,
	 }
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/* Define the accelerometer orientation matrices. */
const struct accel_orientation acc_orient = {
	/* Hinge aligns with x axis. */
	.rot_hinge_90 = {
		{ FLOAT_TO_FP(1),  0,  0},
		{ 0,  0,  FLOAT_TO_FP(1)},
		{ 0, FLOAT_TO_FP(-1),  0}
	},
	.rot_hinge_180 = {
		{ FLOAT_TO_FP(1),  0,  0},
		{ 0, FLOAT_TO_FP(-1),  0},
		{ 0,  0, FLOAT_TO_FP(-1)}
	},
	.hinge_axis = {1, 0, 0},
};

/*
 * In S3, power rail for sensors (+V3p3S) goes down asynchronous to EC. We need
 * to execute this routine first and set the sensor state to "Not Initialized".
 * This prevents the motion_sense_suspend hook routine from communicating with
 * the sensor.
 */
static void motion_sensors_pre_init(void)
{
	struct motion_sensor_t *sensor;
	int i;

	for (i = 0; i < motion_sensor_count; ++i) {
		sensor = &motion_sensors[i];
		sensor->state = SENSOR_NOT_INITIALIZED;

		sensor->runtime_config.odr = sensor->default_config.odr;
		sensor->runtime_config.range = sensor->default_config.range;
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, motion_sensors_pre_init,
	MOTION_SENSE_HOOK_PRIO - 1);

/*
 * Temperature sensors data; must be in same order as enum temp_sensor_id.
 * Sensor index and name must match those present in coreboot:
 *     src/mainboard/google/${board}/acpi/dptf.asl
 */
const struct temp_sensor_t temp_sensors[] = {
	{"TMP432_Internal", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
		TMP432_IDX_LOCAL, 4},
	{"TMP432_Sensor_1", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
		TMP432_IDX_REMOTE1, 4},
	{"TMP432_Sensor_2", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
		TMP432_IDX_REMOTE2, 4},
	{"Battery", TEMP_SENSOR_TYPE_BATTERY, charge_temp_sensor_get_val,
		0, 4},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * Thermal limits for each temp sensor.  All temps are in degrees K.  Must be in
 * same order as enum temp_sensor_id.  To always ignore any temp, use 0.
 */
struct ec_thermal_config thermal_params[] = {
	/* {Twarn, Thigh, Thalt}, fan_off, fan_max */
	{{0, 0, 0}, 0, 0},	/* TMP432_Internal */
	{{0, 0, 0}, 0, 0},	/* TMP432_Sensor_1 */
	{{0, 0, 0}, 0, 0},	/* TMP432_Sensor_2 */
	{{0, 0, 0}, 0, 0},	/* Battery */
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

/* ALS instances. Must be in same order as enum als_id. */
struct als_t als[] = {
	{"TI", opt3001_read_lux, 5},
};
BUILD_ASSERT(ARRAY_SIZE(als) == ALS_COUNT);

const struct button_config buttons[CONFIG_BUTTON_COUNT] = {
	{"Volume Down", KEYBOARD_BUTTON_VOLUME_DOWN, GPIO_VOLUME_DOWN_L,
		30 * MSEC, 0},
	{"Volume Up", KEYBOARD_BUTTON_VOLUME_UP, GPIO_VOLUME_UP_L,
		30 * MSEC, 0},
};
BUILD_ASSERT(ARRAY_SIZE(buttons) == CONFIG_BUTTON_COUNT);

/* Initialize board. */
static void board_init(void)
{
	int i;
	struct charge_port_info charge_none;

	/* Enable PD MCU interrupt */
	gpio_enable_interrupt(GPIO_PD_MCU_INT);
	/* Enable VBUS interrupt */
	gpio_enable_interrupt(GPIO_USB_C0_VBUS_WAKE_L);
	gpio_enable_interrupt(GPIO_USB_C1_VBUS_WAKE_L);

	/* Initialize all pericom charge suppliers to 0 */
	charge_none.voltage = USB_CHARGER_VOLTAGE_MV;
	charge_none.current = 0;
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
	}

	/* Initialize VBUS supplier based on whether or not VBUS is present */
	update_vbus_supplier(0, !gpio_get_level(GPIO_USB_C0_VBUS_WAKE_L));
	update_vbus_supplier(1, !gpio_get_level(GPIO_USB_C1_VBUS_WAKE_L));

	/* Enable pericom BC1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_L);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_L);
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
	/* charge port is a realy physical port */
	int is_real_port = (charge_port >= 0 &&
			    charge_port < CONFIG_USB_PD_PORT_COUNT);
	/* check if we are source vbus on that port */
	int source = gpio_get_level(charge_port == 0 ? GPIO_USB_C0_5V_EN :
						       GPIO_USB_C1_5V_EN);

	if (is_real_port && source) {
		CPRINTS("Skip enable p%d", charge_port);
		return EC_ERROR_INVAL;
	}

	CPRINTS("New chg p%d", charge_port);

	if (charge_port == CHARGE_PORT_NONE) {
		/*
		 * TODO: currently we only get VBUS knowledge when charge
		 * is enabled. so, when not charging, we need to enable
		 * both ports. but, this is dangerous if you have two
		 * chargers plugged in and you set charge override to -1
		 * then it will enable both sides!
		 */
		gpio_set_level(GPIO_USB_C0_CHARGE_EN_L, 0);
		gpio_set_level(GPIO_USB_C1_CHARGE_EN_L, 0);
	} else {
		/* Make sure non-charging port is disabled */
		gpio_set_level(charge_port ? GPIO_USB_C0_CHARGE_EN_L :
					     GPIO_USB_C1_CHARGE_EN_L, 1);
		/* Enable charging port */
		gpio_set_level(charge_port ? GPIO_USB_C1_CHARGE_EN_L :
					     GPIO_USB_C0_CHARGE_EN_L, 0);
	}

	return EC_SUCCESS;
}

/**
 * Set the charge limit based upon desired maximum.
 *
 * @param charge_ma     Desired charge limit (mA).
 */
void board_set_charge_limit(int charge_ma)
{
	charge_set_input_current_limit(MAX(charge_ma,
					   CONFIG_CHARGER_INPUT_CURRENT));
}

