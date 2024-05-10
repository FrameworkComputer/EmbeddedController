/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "backlight.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/battery/max17055.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/charger/isl923x.h"
#include "driver/sync.h"
#include "driver/tcpm/fusb302.h"
#include "driver/usb_mux/it5205.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "i2c_bitbang.h"
#include "it8801.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "panic.h"
#include "power.h"
#include "power_button.h"
#include "registers.h"
#include "spi.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "tcpm/tcpm.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

static void tcpc_alert_event(enum gpio_signal signal)
{
	schedule_deferred_pd_interrupt(0 /* port */);
}

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/******************************************************************************/
/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	[ADC_BOARD_ID] = { "BOARD_ID", 3300, 4096, 0, STM32_AIN(10) },
	[ADC_EC_SKU_ID] = { "EC_SKU_ID", 3300, 4096, 0, STM32_AIN(8) },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************/
/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "typec",
	  .port = 0,
	  .kbps = 400,
	  .scl = GPIO_I2C1_SCL,
	  .sda = GPIO_I2C1_SDA },
	{ .name = "other",
	  .port = 1,
	  .kbps = 100,
	  .scl = GPIO_I2C2_SCL,
	  .sda = GPIO_I2C2_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

const struct i2c_port_t i2c_bitbang_ports[] = {
	{ .name = "battery",
	  .port = 2,
	  .kbps = 100,
	  .scl = GPIO_I2C3_SCL,
	  .sda = GPIO_I2C3_SDA,
	  .drv = &bitbang_drv },
};
const unsigned int i2c_bitbang_ports_used = ARRAY_SIZE(i2c_bitbang_ports);

#define BC12_I2C_ADDR PI3USB9201_I2C_ADDR_3

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{ GPIO_AP_IN_SLEEP_L, POWER_SIGNAL_ACTIVE_LOW, "AP_IN_S3_L" },
	{ GPIO_PMIC_EC_RESETB, POWER_SIGNAL_ACTIVE_HIGH, "PMIC_PWR_GOOD" },
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	/*
	 * TODO(b/133200075): Tune this once we have the final performance
	 * out of the driver and the i2c bus.
	 */
	.output_settle_us = 35,
	.debounce_down_us = 5 * MSEC,
	.debounce_up_us = 40 * MSEC,
	.scan_period_us = 10 * MSEC,
	.min_post_scan_delay_us = 10 * MSEC,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

struct ioexpander_config_t ioex_config[CONFIG_IO_EXPANDER_PORT_COUNT] = {
	[0] = {
		.i2c_host_port = I2C_PORT_KB_DISCRETE,
		.i2c_addr_flags = IT8801_I2C_ADDR1,
		.drv = &it8801_ioexpander_drv,
	},
};

/******************************************************************************/
/* SPI devices */
/* TODO: to be added once sensors land via CL:1714436 */
const struct spi_device_t spi_devices[] = {};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	{
		.i2c_port = I2C_PORT_BC12,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
};

/******************************************************************************/
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC0,
			.addr_flags = FUSB302_I2C_ADDR_FLAGS,
		},
		.drv = &fusb302_tcpm_drv,
	},
};

static void board_hpd_status(const struct usb_mux *me, mux_state_t mux_state,
			     bool *ack_required)
{
	/* This driver does not use host command ACKs */
	*ack_required = false;

	/*
	 * svdm_dp_attention() did most of the work, we only need to notify
	 * host here.
	 */
	host_set_single_event(EC_HOST_EVENT_USB_MUX);
}

const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.usb_port = 0,
				.i2c_port = I2C_PORT_USB_MUX,
				.i2c_addr_flags = IT5205_I2C_ADDR1_FLAGS,
				.driver = &it5205_usb_mux_driver,
				.hpd_update = &board_hpd_status,
			},
	},
};

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL))
		status |= PD_STATUS_TCPC_ALERT_0;

	return status;
}

static int force_discharge;

int board_set_active_charge_port(int charge_port)
{
	CPRINTS("New chg p%d", charge_port);

	/* ignore all request when discharge mode is on */
	if (force_discharge && charge_port != CHARGE_PORT_NONE)
		return EC_SUCCESS;

	switch (charge_port) {
	case CHARGE_PORT_USB_C:
		/* Don't charge from a source port */
		if (board_vbus_source_enabled(charge_port))
			return -1;
		break;
	default:
		/*
		 * To ensure the fuel gauge (max17055) is always powered
		 * even when battery is disconnected, keep VBAT rail on but
		 * set the charging current to minimum.
		 */
		charger_set_current(CHARGER_SOLO, 0);
		break;
	}

	return EC_SUCCESS;
}

int board_discharge_on_ac(int enable)
{
	int ret, port;

	if (enable) {
		port = CHARGE_PORT_NONE;
	} else {
		/* restore the charge port state */
		port = charge_manager_get_override();
		if (port == OVERRIDE_OFF)
			port = charge_manager_get_active_charge_port();
	}

	ret = charger_discharge_on_ac(enable);
	if (ret)
		return ret;
	force_discharge = enable;

	return board_set_active_charge_port(port);
}

int pd_snk_is_vbus_provided(int port)
{
	/* TODO(b:138352732): read IT8801 GPIO EN_USBC_CHARGE_L */
	return EC_ERROR_UNIMPLEMENTED;
}

void bc12_interrupt(enum gpio_signal signal)
{
	usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
}

static void board_init(void)
{
	/* If the reset cause is external, pulse PMIC force reset. */
	if (system_get_reset_flags() == EC_RESET_FLAG_RESET_PIN) {
		gpio_set_level(GPIO_PMIC_FORCE_RESET_ODL, 0);
		crec_msleep(100);
		gpio_set_level(GPIO_PMIC_FORCE_RESET_ODL, 1);
	}

	/* Enable TCPC alert interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);

	/* Enable interrupt from PMIC. */
	gpio_enable_interrupt(GPIO_PMIC_EC_RESETB);

	/* Enable BC12 interrupt */
	gpio_enable_interrupt(GPIO_BC12_EC_INT_ODL);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* Called on AP S5 -> S3 transition */
static void board_chipset_startup(void)
{
	gpio_set_level(GPIO_EN_USBA_5V, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void board_chipset_shutdown(void)
{
	gpio_set_level(GPIO_EN_USBA_5V, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);
