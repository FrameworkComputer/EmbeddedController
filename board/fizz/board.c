/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fizz board-specific configuration */

#include "adc.h"
#include "adc_chip.h"
#include "als.h"
#include "battery.h"
#include "bd99992gw.h"
#include "board_config.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charge_ramp.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "driver/pmic_tps650830.h"
#include "driver/temp_sensor/tmp432.h"
#include "driver/tcpm/ps8751.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "math_util.h"
#include "pi3usb9281.h"
#include "power.h"
#include "power_button.h"
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
#include "espi.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static void tcpc_alert_event(enum gpio_signal signal)
{
	if (!gpio_get_level(GPIO_USB_C0_PD_RST_ODL))
		return;

#ifdef HAS_TASK_PDCMD
	/* Exchange status with TCPCs */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
#endif
}

void vbus0_evt(enum gpio_signal signal)
{
	task_wake(TASK_ID_PD_C0);
}

#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_PCH_SLP_S0_L,	1, "SLP_S0_DEASSERTED"},
#ifdef CONFIG_ESPI_VW_SIGNALS
	{VW_SLP_S3_L,		1, "SLP_S3_DEASSERTED"},
	{VW_SLP_S4_L,		1, "SLP_S4_DEASSERTED"},
#else
	{GPIO_PCH_SLP_S3_L,	1, "SLP_S3_DEASSERTED"},
	{GPIO_PCH_SLP_S4_L,	1, "SLP_S4_DEASSERTED"},
#endif
	{GPIO_PCH_SLP_SUS_L,	1, "SLP_SUS_DEASSERTED"},
	{GPIO_RSMRST_L_PGOOD,	1, "RSMRST_L_PGOOD"},
	{GPIO_PMIC_DPWROK,	1, "PMIC_DPWROK"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* Hibernate wake configuration */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vbus sensing (1/10 voltage divider). */
	[ADC_VBUS] = {"VBUS", NPCX_ADC_CH2, ADC_MAX_VOLT*10, ADC_READ_MAX+1, 0},
	/*
	 * Adapter current output or battery charging/discharging current (uV)
	 * 18x amplification on charger side.
	 */
	[ADC_AMON_BMON] = {"AMON_BMON", NPCX_ADC_CH1, ADC_MAX_VOLT*1000/18,
			   ADC_READ_MAX+1, 0},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* I2C port map */
const struct i2c_port_t i2c_ports[]  = {
	{"tcpc", NPCX_I2C_PORT0_0, 400, GPIO_I2C0_0_SCL, GPIO_I2C0_0_SDA},
	{"eeprom", NPCX_I2C_PORT0_1, 400, GPIO_I2C0_1_SCL, GPIO_I2C0_1_SDA},
	{"charger", NPCX_I2C_PORT1, 100, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"pmic", NPCX_I2C_PORT2, 400, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
	{"thermal", NPCX_I2C_PORT3, 400, GPIO_I2C3_SCL, GPIO_I2C3_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* TCPC mux configuration */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	{NPCX_I2C_PORT0_0, I2C_ADDR_TCPC0, &tcpci_tcpm_drv,
			TCPC_ALERT_ACTIVE_LOW},
};

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.port_addr = 0,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8751_tcpc_update_hpd_status,
	}
};

const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_USB1_ENABLE,
	GPIO_USB2_ENABLE,
	GPIO_USB3_ENABLE,
	GPIO_USB4_ENABLE,
	GPIO_USB5_ENABLE,
};

void board_reset_pd_mcu(void)
{
	gpio_set_level(GPIO_USB_C0_PD_RST_ODL, 0);
	msleep(1);
	gpio_set_level(GPIO_USB_C0_PD_RST_ODL, 1);
}

void board_tcpc_init(void)
{
	int port, reg;

	/* Only reset TCPC if not sysjump */
	if (!system_jumped_to_this_image()) {
		/* Power on PS8751 */
		gpio_set_level(GPIO_PP3300_USB_PD, 1);
		/* TODO(crosbug.com/p/61098): How long do we need to wait? */
		msleep(10);
		board_reset_pd_mcu();
	}

	/*
	 * Wake up PS8751. If PS8751 remains in low power mode after sysjump,
	 * TCPM_INIT will fail due to not able to access PS8751.
	 * Note PS8751 A3 will wake on any I2C access.
	 */
	i2c_read8(I2C_PORT_TCPC0, I2C_ADDR_TCPC0, 0xA0, &reg);

	/* Enable TCPC interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);

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

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL)) {
		if (gpio_get_level(GPIO_USB_C0_PD_RST_ODL))
			status |= PD_STATUS_TCPC_ALERT_0;
	}

	return status;
}

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
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * Thermal limits for each temp sensor.  All temps are in degrees K.  Must be in
 * same order as enum temp_sensor_id.  To always ignore any temp, use 0.
 */
struct ec_thermal_config thermal_params[] = {
	/* {Twarn, Thigh, Thalt}, fan_off, fan_max */
	{{0, 0, 0}, C_TO_K(35), C_TO_K(68)},	/* TMP432_Internal */
	{{0, 0, 0}, 0, 0},	/* TMP432_Sensor_1 */
	{{0, 0, 0}, 0, 0},	/* TMP432_Sensor_2 */
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

/* Initialize PMIC */
#define I2C_PMIC_READ(reg, data) \
		i2c_read8(I2C_PORT_PMIC, TPS650830_I2C_ADDR1, (reg), (data))

#define I2C_PMIC_WRITE(reg, data) \
		i2c_write8(I2C_PORT_PMIC, TPS650830_I2C_ADDR1, (reg), (data))

static void board_pmic_init(void)
{
	int err;
	int error_count = 0;

	/* No need to re-init PMIC since settings are sticky across sysjump */
	if (system_jumped_to_this_image())
		return;

	/* Read vendor ID */
	while (1) {
		int data;
		err = I2C_PMIC_READ(TPS650830_REG_VENDORID, &data);
		if (!err && data == TPS650830_VENDOR_ID)
			break;
		else if (error_count > 5)
			goto pmic_error;
		error_count++;
	}

	/*
	 * VCCIOCNT register setting
	 * [6] : CSDECAYEN
	 * otherbits: default
	 */
	err = I2C_PMIC_WRITE(TPS650830_REG_VCCIOCNT, 0x4A);
	if (err)
		goto pmic_error;

	/*
	 * VRMODECTRL:
	 * [4] : VCCIOLPM clear
	 * otherbits: default
	 */
	err = I2C_PMIC_WRITE(TPS650830_REG_VRMODECTRL, 0x2F);
	if (err)
		goto pmic_error;

	/*
	 * PGMASK1 : Exclude VCCIO from Power Good Tree
	 * [7] : MVCCIOPG clear
	 * otherbits: default
	 */
	err = I2C_PMIC_WRITE(TPS650830_REG_PGMASK1, 0x80);
	if (err)
		goto pmic_error;

	/*
	 * PWFAULT_MASK1 Register settings
	 * [7] : 1b V4 Power Fault Masked
	 * [4] : 1b V7 Power Fault Masked
	 * [2] : 1b V9 Power Fault Masked
	 * [0] : 1b V13 Power Fault Masked
	 */
	err = I2C_PMIC_WRITE(TPS650830_REG_PWFAULT_MASK1, 0x95);
	if (err)
		goto pmic_error;

	/*
	 * Discharge control 4 register configuration
	 * [7:6] : 00b Reserved
	 * [5:4] : 01b V3.3S discharge resistance (V6S), 100 Ohm
	 * [3:2] : 01b V18S discharge resistance (V8S), 100 Ohm
	 * [1:0] : 01b V100S discharge resistance (V11S), 100 Ohm
	 */
	err = I2C_PMIC_WRITE(TPS650830_REG_DISCHCNT4, 0x15);
	if (err)
		goto pmic_error;

	/*
	 * Discharge control 3 register configuration
	 * [7:6] : 01b V1.8U_2.5U discharge resistance (V9), 100 Ohm
	 * [5:4] : 01b V1.2U discharge resistance (V10), 100 Ohm
	 * [3:2] : 01b V100A discharge resistance (V11), 100 Ohm
	 * [1:0] : 01b V085A discharge resistance (V12), 100 Ohm
	 */
	err = I2C_PMIC_WRITE(TPS650830_REG_DISCHCNT3, 0x55);
	if (err)
		goto pmic_error;

	/*
	 * Discharge control 2 register configuration
	 * [7:6] : 01b V5ADS3 discharge resistance (V5), 100 Ohm
	 * [5:4] : 01b V33A_DSW discharge resistance (V6), 100 Ohm
	 * [3:2] : 01b V33PCH discharge resistance (V7), 100 Ohm
	 * [1:0] : 01b V18A discharge resistance (V8), 100 Ohm
	 */
	err = I2C_PMIC_WRITE(TPS650830_REG_DISCHCNT2, 0x55);
	if (err)
		goto pmic_error;

	/*
	 * Discharge control 1 register configuration
	 * [7:2] : 00b Reserved
	 * [1:0] : 01b VCCIO discharge resistance (V4), 100 Ohm
	 */
	err = I2C_PMIC_WRITE(TPS650830_REG_DISCHCNT1, 0x01);
	if (err)
		goto pmic_error;

	/*
	 * Increase Voltage
	 *  [7:0] : 0x2a default
	 *  [5:4] : 10b default
	 *  [5:4] : 01b 5.1V (0x1a)
	 */
	err = I2C_PMIC_WRITE(TPS650830_REG_V5ADS3CNT, 0x1a);
	if (err)
		goto pmic_error;

	/*
	 * PBCONFIG Register configuration
	 *   [7] :      1b Power button debounce, 0ms (no debounce)
	 *   [6] :      0b Power button reset timer logic, no action (default)
	 * [5:0] : 011111b Force an Emergency reset time, 31s (default)
	 */
	err = I2C_PMIC_WRITE(TPS650830_REG_PBCONFIG, 0x9F);
	if (err)
		goto pmic_error;

	CPRINTS("PMIC init done");
	return;

pmic_error:
	CPRINTS("PMIC init failed");
}
DECLARE_HOOK(HOOK_INIT, board_pmic_init, HOOK_PRIO_INIT_I2C + 1);

/**
 * Notify the AC presence GPIO to the PCH.
 */
static void board_extpower(void)
{
	gpio_set_level(GPIO_PCH_ACPRESENT, extpower_is_present());
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_extpower, HOOK_PRIO_DEFAULT);

/* Initialize board. */
static void board_init(void)
{
	/* Provide AC status to the PCH */
	board_extpower();

	gpio_enable_interrupt(GPIO_USB_C0_VBUS_WAKE_L);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	charger_set_input_current(charge_ma);
}

/**
 * Return whether ramping is allowed for given supplier
 */
int board_is_ramp_allowed(int supplier)
{
	/* Don't allow ramping in RO when write protected */
	if (system_get_image_copy() != SYSTEM_IMAGE_RW
		&& system_is_locked())
		return 0;
	else
		return (supplier == CHARGE_SUPPLIER_BC12_DCP ||
			supplier == CHARGE_SUPPLIER_BC12_SDP ||
			supplier == CHARGE_SUPPLIER_BC12_CDP ||
			supplier == CHARGE_SUPPLIER_OTHER);
}

/**
 * Return the maximum allowed input current
 */
int board_get_ramp_current_limit(int supplier, int sup_curr)
{
	switch (supplier) {
	case CHARGE_SUPPLIER_BC12_DCP:
		return 2000;
	case CHARGE_SUPPLIER_BC12_SDP:
		return 1000;
	case CHARGE_SUPPLIER_BC12_CDP:
	case CHARGE_SUPPLIER_PROPRIETARY:
		return sup_curr;
	default:
		return 500;
	}
}

/**
 * Return if board is consuming full amount of input current
 */
int board_is_consuming_full_charge(void)
{
	int chg_perc = charge_get_percent();

	return chg_perc > 2 && chg_perc < 95;
}

const struct button_config buttons[CONFIG_BUTTON_COUNT] = {
	[BUTTON_RECOVERY] = {
		.name = "Recovery",
		.type = KEYBOARD_BUTTON_RECOVERY,
		.gpio = GPIO_RECOVERY_L,
		.debounce_us = 30 * MSEC,
		.flags = 0,
	},
};

const struct button_config *recovery_buttons[] = {
	&buttons[BUTTON_RECOVERY],
};
const int recovery_buttons_count = ARRAY_SIZE(recovery_buttons);

enum battery_present battery_is_present(void)
{
	/* The GPIO is low when the battery is present */
	return BP_NO;
}
