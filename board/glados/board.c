/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Glados board-specific configuration */

#include "adc_chip.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "task.h"
#include "timer.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define GPIO_KB_INPUT (GPIO_INPUT | GPIO_PULL_UP)
#define GPIO_KB_OUTPUT (GPIO_ODR_HIGH)

#define I2C_ADDR_BD99992 0x60

/* Default input current limit when VBUS is present */
#define DEFAULT_CURR_LIMIT      500  /* mA */
#define USB_BC12_CHARGE_VOLTAGE 5000 /* mV */

/* Exchange status with PD MCU. */
static void pd_mcu_interrupt(enum gpio_signal signal)
{
	/* Exchange status with PD MCU to determine interrupt cause */
	host_command_pd_send_status(0);
}

void vbus0_evt(enum gpio_signal signal)
{
	CPRINTF("VBUS C0, %d\n", !gpio_get_level(signal));
	task_wake(TASK_ID_PD_C0);
}

void vbus1_evt(enum gpio_signal signal)
{
	CPRINTF("VBUS C1, %d\n", !gpio_get_level(signal));
	task_wake(TASK_ID_PD_C1);
}

void usb0_evt(enum gpio_signal signal)
{
}

void usb1_evt(enum gpio_signal signal)
{
}

#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_RSMRST_L_PGOOD,    1, "RSMRST_N_PWRGD"},
	{GPIO_PCH_SLP_S0_L,      1, "SLP_S0_DEASSERTED"},
	{GPIO_PCH_SLP_S3_L,      1, "SLP_S3_DEASSERTED"},
	{GPIO_PCH_SLP_S4_L,      1, "SLP_S4_DEASSERTED"},
	{GPIO_PCH_SLP_SUS_L,     1, "SLP_SUS_DEASSERTED"},
	{GPIO_PMIC_DPWROK,       1, "PMIC_DPWROK"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Battery pack thermisor */
	[ADC_BATT_TEMP] = {"BATT_TEMP", 1, 1, 0, 0},
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

/* Charge supplier priority: lower number indicates higher priority. */
const int supplier_priority[] = {
	[CHARGE_SUPPLIER_PD] = 0,
	[CHARGE_SUPPLIER_TYPEC] = 1,
	[CHARGE_SUPPLIER_PROPRIETARY] = 1,
	[CHARGE_SUPPLIER_BC12_DCP] = 1,
	[CHARGE_SUPPLIER_BC12_CDP] = 2,
	[CHARGE_SUPPLIER_BC12_SDP] = 3,
	[CHARGE_SUPPLIER_OTHER] = 3,
	[CHARGE_SUPPLIER_VBUS] = 4
};
BUILD_ASSERT(ARRAY_SIZE(supplier_priority) == CHARGE_SUPPLIER_COUNT);

/**
 * Discharge battery when on AC power for factory test.
 */
int board_discharge_on_ac(int enable)
{
	return charger_discharge_on_ac(enable);
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

struct motion_sensor_t motion_sensors[] = {

};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

const struct button_config buttons[CONFIG_BUTTON_COUNT] = {
	{ 0 },
	{ 0 },
};

static void pmic_init(void)
{
	/*
	 * Set V085ACNT / V0.85A Control Register:
	 * Lower power mode = 0.7V.
	 * Nominal output = 1.0V.
	 */
	i2c_write8(I2C_PORT_PMIC, I2C_ADDR_BD99992, 0x38, 0x7a);
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, pmic_init, HOOK_PRIO_DEFAULT);

/* Initialize board. */
static void board_init(void)
{
	int i;
	struct charge_port_info charge_none, charge_vbus;

	/* Enable PD MCU interrupt */
	gpio_enable_interrupt(GPIO_PD_MCU_INT);
	/* Enable VBUS interrupt */
	gpio_enable_interrupt(GPIO_USB_C0_VBUS_WAKE_L);
	gpio_enable_interrupt(GPIO_USB_C1_VBUS_WAKE_L);

	/* Initialize all pericom charge suppliers to 0 */
	charge_none.voltage = USB_BC12_CHARGE_VOLTAGE;
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
	charge_vbus.voltage = USB_BC12_CHARGE_VOLTAGE;
	charge_vbus.current = DEFAULT_CURR_LIMIT;
	if (!gpio_get_level(GPIO_USB_C0_VBUS_WAKE_L))
		charge_manager_update_charge(CHARGE_SUPPLIER_VBUS, 0,
					     &charge_vbus);
	else
		charge_manager_update_charge(CHARGE_SUPPLIER_VBUS, 0,
					     &charge_none);

	if (!gpio_get_level(GPIO_USB_C1_VBUS_WAKE_L))
		charge_manager_update_charge(CHARGE_SUPPLIER_VBUS, 1,
					     &charge_vbus);
	else
		charge_manager_update_charge(CHARGE_SUPPLIER_VBUS, 1,
					     &charge_none);
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

/* Charge manager callback function, called on delayed override timeout */
void board_charge_manager_override_timeout(void)
{
	/* TODO: what to do here? */
}
DECLARE_DEFERRED(board_charge_manager_override_timeout);

