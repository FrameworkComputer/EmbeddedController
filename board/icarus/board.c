/* Copyright 2021 The ChromiumOS Authors
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
#include "driver/battery/max17055.h"
#include "driver/bc12/pi3usb9201.h"
#include "driver/charger/isl923x.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/usb_mux/it5205.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "panic.h"
#include "power.h"
#include "power_button.h"
#include "registers.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "tcpm/tcpm.h"
#include "timer.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/*
 * Map keyboard connector pins to EC GPIO pins for factory test.
 * Pins mapped to {-1, -1} are skipped.
 * The connector has 24 pins total, and there is no pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ -1, -1 },	   { GPIO_KSO_H, 4 }, { GPIO_KSO_H, 0 },
	{ GPIO_KSO_H, 1 }, { GPIO_KSO_H, 3 }, { GPIO_KSO_H, 2 },
	{ -1, -1 },	   { -1, -1 },	      { GPIO_KSO_L, 5 },
	{ GPIO_KSO_L, 6 }, { -1, -1 },	      { GPIO_KSO_L, 3 },
	{ GPIO_KSO_L, 2 }, { GPIO_KSI, 0 },   { GPIO_KSO_L, 1 },
	{ GPIO_KSO_L, 4 }, { GPIO_KSI, 3 },   { GPIO_KSI, 2 },
	{ GPIO_KSO_L, 0 }, { GPIO_KSI, 5 },   { GPIO_KSI, 4 },
	{ GPIO_KSO_L, 7 }, { GPIO_KSI, 6 },   { GPIO_KSI, 7 },
	{ GPIO_KSI, 1 },   { -1, -1 },	      { GPIO_KSO_H, 5 },
	{ -1, -1 },	   { GPIO_KSO_H, 6 }, { -1, -1 },
	{ -1, -1 },
};

const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);

/* Wake-up pins for hibernate */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_AC_PRESENT,
	GPIO_LID_OPEN,
	GPIO_POWER_BUTTON_L,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/******************************************************************************/
/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	[ADC_BOARD_ID] = { "BOARD_ID", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0,
			   CHIP_ADC_CH1 },
	[ADC_EC_SKU_ID] = { "EC_SKU_ID", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0,
			    CHIP_ADC_CH2 },
	[ADC_VBUS] = { "VBUS", ADC_MAX_MVOLT * 10, ADC_READ_MAX + 1, 0,
		       CHIP_ADC_CH0 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************/
/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "typec",
	  .port = IT83XX_I2C_CH_C,
	  .kbps = 400,
	  .scl = GPIO_I2C_C_SCL,
	  .sda = GPIO_I2C_C_SDA },
	{ .name = "other",
	  .port = IT83XX_I2C_CH_B,
	  .kbps = 100,
	  .scl = GPIO_I2C_B_SCL,
	  .sda = GPIO_I2C_B_SDA },
	{ .name = "battery",
	  .port = IT83XX_I2C_CH_A,
	  .kbps = 100,
	  .scl = GPIO_I2C_A_SCL,
	  .sda = GPIO_I2C_A_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

#define BC12_I2C_ADDR PI3USB9201_I2C_ADDR_3

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{ GPIO_AP_IN_SLEEP_L, POWER_SIGNAL_ACTIVE_LOW, "AP_IN_S3_L" },
	{ GPIO_PMIC_EC_RESETB, POWER_SIGNAL_ACTIVE_HIGH, "PMIC_PWR_GOOD" },
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/******************************************************************************/
const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	{
		.i2c_port = I2C_PORT_BC12,
		.i2c_addr_flags = PI3USB9201_I2C_ADDR_3_FLAGS,
	},
};

/******************************************************************************/
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it8xxx2_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
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

/* Charger config.  Start i2c address at 1, update during runtime */
struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER,
		.i2c_addr_flags = ISL923X_ADDR_FLAGS,
		.drv = &isl923x_drv,
	},
};

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

#define VBUS_THRESHOLD_MV 4200
int pd_snk_is_vbus_provided(int port)
{
	/* This board has only one port. */
	if (!port)
		return adc_read_channel(ADC_VBUS) > VBUS_THRESHOLD_MV ? 1 : 0;
	else
		return 0;
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

	/* Enable interrupts from BMI160 sensor. */
	gpio_enable_interrupt(GPIO_ACCEL_INT_ODL);

	/* Enable interrupt from PMIC. */
	gpio_enable_interrupt(GPIO_PMIC_EC_RESETB);

	/* Enable BC12 interrupt */
	gpio_enable_interrupt(GPIO_BC12_EC_INT_ODL);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* Vconn control for integrated ITE TCPC */
void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
	/* Vconn control is only for port 0 */
	if (port)
		return;

	if (cc_pin == USBPD_CC_PIN_1)
		gpio_set_level(GPIO_EN_USB_C0_CC1_VCONN, !!enabled);
	else
		gpio_set_level(GPIO_EN_USB_C0_CC2_VCONN, !!enabled);
}

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
