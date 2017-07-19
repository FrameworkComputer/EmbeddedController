/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Zoombini board-specific configuration */

#include "adc_chip.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charge_state_v2.h"
#include "common.h"
#include "console.h"
#include "compile_time_macros.h"
#include "driver/tcpm/ps8xxx.h"
#include "ec_commands.h"
#ifdef CONFIG_ESPI_VW_SIGNALS
#include "espi.h"
#endif /* defined(CONFIG_ESPI_VW_SIGNALS) */
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "switch.h"
#include "tcpci.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static void tcpc_alert_event(enum gpio_signal s)
{
#ifdef HAS_TASK_PDCMD
	/* Exchange status with TCPCs */
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
#endif
}

#include "gpio_list.h"

/* TODO(aaboagye): Handle PSL? */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used =  ARRAY_SIZE(hibernate_wake_pins);

const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_SOC] = {
		"SOC", NPCX_ADC_CH0, ADC_MAX_VOLT, ADC_READ_MAX+1, 0
	},

	[ADC_TEMP_SENSOR_CHARGER] = {
		"CHARGER", NPCX_ADC_CH1, ADC_MAX_VOLT, ADC_READ_MAX+1, 0
	},
};

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_LED_GREEN] = { 0, PWM_CONFIG_DSLEEP, 100 },
	[PWM_CH_LED_RED] =   { 2, PWM_CONFIG_DSLEEP, 100 },
	[PWM_CH_KB_BL] =     { 3, 0, 100},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_PCH_SLP_S0_L,   1, "SLP_S0_DEASSERTED"},
#ifdef CONFIG_ESPI_VW_SIGNALS
	{VW_SLP_S3_L,         1, "SLP_S3_DEASSERTED"},
	{VW_SLP_S4_L,         1, "SLP_S4_DEASSERTED"},
#else
	{GPIO_PCH_SLP_S3_L,   1, "SLP_S3_DEASSERTED"},
	{GPIO_PCH_SLP_S4_L,   1, "SLP_S4_DEASSERTED"},
#endif /* defined(CONFIG_ESPI_VW_SIGNALS) */
	{GPIO_PCH_SLP_SUS_L,  1, "SLP_SUS_DEASSERTED"},
	{GPIO_RSMRST_L_PGOOD, 1, "RSMRST_L_PGOOD"},
	{GPIO_PMIC_DPWROK,    1, "PMIC_DPWROK"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* I2C port map. */
const struct i2c_port_t i2c_ports[] = {
	{"power",   I2C_PORT_POWER,  400, GPIO_I2C0_SCL,  GPIO_I2C0_SDA},
	{"pmic",    I2C_PORT_PMIC,   400, GPIO_I2C3_SCL,  GPIO_I2C3_SDA},
	{"tcpc0",   I2C_PORT_TCPC0, 1000, GPIO_I2C5_SCL,  GPIO_I2C5_SDA},
	{"tcpc1",   I2C_PORT_TCPC1, 1000, GPIO_I2C1_SCL,  GPIO_I2C1_SDA},
	{"tcpc2",   I2C_PORT_TCPC2, 1000, GPIO_I2C2_SCL,  GPIO_I2C2_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* Keyboard scan setting */
struct keyboard_scan_config keyscan_config = {
	/* Extra delay when KSO2 is tied to Cr50. */
	.output_settle_us = 60, /* TODO(aaboagye): verify this is sufficient. */
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

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.i2c_host_port = I2C_PORT_TCPC0,
		.i2c_slave_addr = 0x16,
		.drv = &tcpci_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},

	{
		.i2c_host_port = I2C_PORT_TCPC1,
		.i2c_slave_addr = 0x16,
		.drv = &tcpci_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},

	{
		.i2c_host_port = I2C_PORT_TCPC2,
		.i2c_slave_addr = 0x16,
		.drv = &tcpci_tcpm_drv,
		.pol = TCPC_ALERT_ACTIVE_LOW,
	},
};

/* The port_addr members are PD port numbers, not I2C port numbers. */
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
	},

	{
		.port_addr = 2,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	},
};

static void board_chipset_startup(void)
{
	/* Enable trackpad. */
	gpio_set_level(GPIO_EN_PP3300_TRACKPAD, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

static void board_chipset_shutdown(void)
{
	/* Disable trackpad. */
	gpio_set_level(GPIO_EN_PP3300_TRACKPAD, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

void board_reset_pd_mcu(void)
{
	/* GPIO_USB_PD_RST_L resets all the TCPCs. */
	gpio_set_level(GPIO_USB_PD_RST_L, 0);
	msleep(10); /* TODO(aaboagye): Verify min hold time. */
	gpio_set_level(GPIO_USB_PD_RST_L, 1);
}

int board_set_active_charge_port(int port)
{
	int is_real_port = (port >= 0 &&
			    port < CONFIG_USB_PD_PORT_COUNT);

	if (!is_real_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	CPRINTS("New chg p%d", port);

	if (port == CHARGE_PORT_NONE) {
		/* Disable all ports. */
		gpio_set_level(GPIO_USB_C0_CHARGE_EN_L, 1);
		gpio_set_level(GPIO_USB_C1_CHARGE_EN_L, 1);
		gpio_set_level(GPIO_USB_C2_CHARGE_EN_L, 1);
		return EC_SUCCESS;
	}

	/* Check if the port is sourcing VBUS. */
	if (((port == 0) && gpio_get_level(GPIO_USB_C0_5V_EN)) ||
	    ((port == 1) && gpio_get_level(GPIO_USB_C1_5V_EN)) ||
	    ((port == 2) && gpio_get_level(GPIO_USB_C2_5V_EN))) {
		CPRINTF("Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}

	/* Disable other charge ports and enable requested port. */
	gpio_set_level(GPIO_USB_C0_CHARGE_EN_L, port != 0);
	gpio_set_level(GPIO_USB_C1_CHARGE_EN_L, port != 1);
	gpio_set_level(GPIO_USB_C2_CHARGE_EN_L, port != 2);

	return EC_SUCCESS;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	/*
	 * Ignore lower charge ceiling on PD transition if our battery is
	 * critical, as we may brownout.
	 */
	if (supplier == CHARGE_SUPPLIER_PD &&
	    charge_ma < 1500 &&
	    charge_get_percent() < CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON) {
		CPRINTS("Using max ilim %d", max_ma);
		charge_ma = max_ma;
	}

	charge_set_input_current_limit(MAX(charge_ma,
					   CONFIG_CHARGER_INPUT_CURRENT),
				       charge_mv);
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_L))
		status |= PD_STATUS_TCPC_ALERT_0;
	if (!gpio_get_level(GPIO_USB_C1_PD_INT_L))
		status |= PD_STATUS_TCPC_ALERT_1;
	if (!gpio_get_level(GPIO_USB_C2_PD_INT_L))
		status |= PD_STATUS_TCPC_ALERT_2;

	return status;
}
