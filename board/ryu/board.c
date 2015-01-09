/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* ryu board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "battery.h"
#include "case_closed_debug.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "inductive_charging.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "registers.h"
#include "task.h"
#include "usb.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "usb-stm32f3.h"
#include "util.h"
#include "pi3usb9281.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

void vbus_evt(enum gpio_signal signal)
{
	ccprintf("VBUS %d, %d!\n", signal, gpio_get_level(signal));
	task_wake(TASK_ID_PD);
}

void unhandled_evt(enum gpio_signal signal)
{
	ccprintf("Unhandled INT %d,%d!\n", signal, gpio_get_level(signal));
}

/* Wait 200ms after a charger is detected to debounce pin contact order */
#define USB_CHG_DEBOUNCE_DELAY_MS 200
/*
 * Wait 100ms after reset, before re-enabling attach interrupt, so that the
 * spurious attach interrupt from certain ports is ignored.
 */
#define USB_CHG_RESET_DELAY_MS 100

void usb_charger_task(void)
{
	int device_type, charger_status;
	struct charge_port_info charge;
	int type;
	charge.voltage = USB_BC12_CHARGE_VOLTAGE;

	while (1) {
		/* Read interrupt register to clear */
		pi3usb9281_get_interrupts(0);

		/* Set device type */
		device_type = pi3usb9281_get_device_type(0);
		charger_status = pi3usb9281_get_charger_status(0);

		/* Debounce pin plug order if we detect a charger */
		if (device_type || PI3USB9281_CHG_STATUS_ANY(charger_status)) {
			msleep(USB_CHG_DEBOUNCE_DELAY_MS);

			/* Trigger chip reset to refresh detection registers */
			pi3usb9281_reset(0);
			/* Clear possible disconnect interrupt */
			pi3usb9281_get_interrupts(0);
			/* Mask attach interrupt */
			pi3usb9281_set_interrupt_mask(0,
						      0xff &
						      ~PI3USB9281_INT_ATTACH);
			/* Re-enable interrupts */
			pi3usb9281_enable_interrupts(0);
			msleep(USB_CHG_RESET_DELAY_MS);

			/* Clear possible attach interrupt */
			pi3usb9281_get_interrupts(0);
			/* Re-enable attach interrupt */
			pi3usb9281_set_interrupt_mask(0, 0xff);

			/* Re-read ID registers */
			device_type = pi3usb9281_get_device_type(0);
			charger_status = pi3usb9281_get_charger_status(0);
		}

		if (PI3USB9281_CHG_STATUS_ANY(charger_status))
			type = CHARGE_SUPPLIER_PROPRIETARY;
		else if (device_type & PI3USB9281_TYPE_CDP)
			type = CHARGE_SUPPLIER_BC12_CDP;
		else if (device_type & PI3USB9281_TYPE_DCP)
			type = CHARGE_SUPPLIER_BC12_DCP;
		else if (device_type & PI3USB9281_TYPE_SDP)
			type = CHARGE_SUPPLIER_BC12_SDP;
		else
			type = CHARGE_SUPPLIER_OTHER;

		/* Attachment: decode + update available charge */
		if (device_type || PI3USB9281_CHG_STATUS_ANY(charger_status)) {
			charge.current = pi3usb9281_get_ilim(device_type,
							     charger_status);
			charge_manager_update(type, 0, &charge);
		} else { /* Detachment: update available charge to 0 */
			charge.current = 0;
			charge_manager_update(CHARGE_SUPPLIER_PROPRIETARY, 0,
					      &charge);
			charge_manager_update(CHARGE_SUPPLIER_BC12_CDP, 0,
					      &charge);
			charge_manager_update(CHARGE_SUPPLIER_BC12_DCP, 0,
					      &charge);
			charge_manager_update(CHARGE_SUPPLIER_BC12_SDP, 0,
					      &charge);
			charge_manager_update(CHARGE_SUPPLIER_OTHER, 0,
					      &charge);
		}

		/* notify host of power info change */
		/* pd_send_host_event(PD_EVENT_POWER_CHANGE); */

		/* Wait for interrupt */
		task_wait_event(-1);
	}
}

void usb_evt(enum gpio_signal signal)
{
	task_wake(TASK_ID_USB_CHG);
}

#include "gpio_list.h"

const void *const usb_strings[] = {
	[USB_STR_DESC]         = usb_string_desc,
	[USB_STR_VENDOR]       = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT]      = USB_STRING_DESC("Ryu debug"),
	[USB_STR_VERSION]      = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("EC_PD"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/* Initialize board. */
static void board_init(void)
{
	struct charge_port_info charge;

	/* Initialize all pericom charge suppliers to 0 */
	charge.voltage = USB_BC12_CHARGE_VOLTAGE;
	charge.current = 0;
	charge_manager_update(CHARGE_SUPPLIER_PROPRIETARY, 0,
			      &charge);
	charge_manager_update(CHARGE_SUPPLIER_BC12_CDP, 0, &charge);
	charge_manager_update(CHARGE_SUPPLIER_BC12_DCP, 0, &charge);
	charge_manager_update(CHARGE_SUPPLIER_BC12_SDP, 0, &charge);
	charge_manager_update(CHARGE_SUPPLIER_OTHER, 0, &charge);

	/* Enable pericom BC1.2 interrupts. */
	gpio_enable_interrupt(GPIO_USBC_BC12_INT_L);
	pi3usb9281_set_interrupt_mask(0, 0xff);
	pi3usb9281_enable_interrupts(0);

	/*
	 * Determine recovery mode is requested by the power, volup, and
	 * voldown buttons being pressed.
	 */
	if (power_button_signal_asserted() &&
	    !gpio_get_level(GPIO_BTN_VOLD_L) &&
	    !gpio_get_level(GPIO_BTN_VOLU_L))
		host_set_single_event(EC_HOST_EVENT_KEYBOARD_RECOVERY);

	/*
	 * Enable CC lines after all GPIO have been initialized. Note, it is
	 * important that this is enabled after the CC_DEVICE_ODL lines are
	 * set low to specify device mode.
	 */
	gpio_set_level(GPIO_USBC_CC_EN, 1);

	/* Enable interrupts on VBUS transitions. */
	gpio_enable_interrupt(GPIO_CHGR_ACOK);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_AP_HOLD, 1, "AP_HOLD"},
	{GPIO_AP_IN_SUSPEND,  1, "SUSPEND_ASSERTED"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vbus sensing. Converted to mV, /10 voltage divider. */
	[ADC_VBUS] = {"VBUS",  30000, 4096, 0, STM32_AIN(0)},
	/* USB PD CC lines sensing. Converted to mV (3000mV/4096). */
	[ADC_CC1_PD] = {"CC1_PD", 3000, 4096, 0, STM32_AIN(1)},
	[ADC_CC2_PD] = {"CC2_PD", 3000, 4096, 0, STM32_AIN(3)},
	/* Charger current sensing. Converted to mA. */
	[ADC_IADP] = {"IADP",  7500, 4096, 0, STM32_AIN(8)},
	[ADC_IBAT] = {"IBAT", 37500, 4096, 0, STM32_AIN(13)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Charge supplier priority: lower number indicates higher priority. */
const int supplier_priority[] = {
	[CHARGE_SUPPLIER_PD] = 0,
	[CHARGE_SUPPLIER_TYPEC] = 1,
	[CHARGE_SUPPLIER_PROPRIETARY] = 1,
	[CHARGE_SUPPLIER_BC12_DCP] = 1,
	[CHARGE_SUPPLIER_BC12_CDP] = 2,
	[CHARGE_SUPPLIER_BC12_SDP] = 3,
	[CHARGE_SUPPLIER_OTHER] = 3
};
BUILD_ASSERT(ARRAY_SIZE(supplier_priority) == CHARGE_SUPPLIER_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
	{"slave",  I2C_PORT_SLAVE, 100,
		GPIO_SLAVE_I2C_SCL, GPIO_SLAVE_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

void board_set_usb_mux(int port, enum typec_mux mux, int polarity)
{
	/* reset everything */
	gpio_set_level(GPIO_USBC_SS_EN_L, 1);
	gpio_set_level(GPIO_USBC_DP_MODE_L, 1);
	gpio_set_level(GPIO_USBC_DP_POLARITY, 1);
	gpio_set_level(GPIO_USBC_SS1_USB_MODE_L, 1);
	gpio_set_level(GPIO_USBC_SS2_USB_MODE_L, 1);

	if (mux == TYPEC_MUX_NONE)
		/* everything is already disabled, we can return */
		return;

	if (mux == TYPEC_MUX_USB || mux == TYPEC_MUX_DOCK) {
		/* USB 3.0 uses 2 superspeed lanes */
		gpio_set_level(polarity ? GPIO_USBC_SS2_USB_MODE_L :
					  GPIO_USBC_SS1_USB_MODE_L, 0);
	}

	if (mux == TYPEC_MUX_DP || mux == TYPEC_MUX_DOCK) {
		/* DP uses available superspeed lanes (x2 or x4) */
		gpio_set_level(GPIO_USBC_DP_POLARITY, polarity);
		gpio_set_level(GPIO_USBC_DP_MODE_L, 0);
	}
	/* switch on superspeed lanes */
	gpio_set_level(GPIO_USBC_SS_EN_L, 0);
}

int board_get_usb_mux(int port, const char **dp_str, const char **usb_str)
{
	int has_ss = !gpio_get_level(GPIO_USBC_SS_EN_L);
	int has_usb = !gpio_get_level(GPIO_USBC_SS1_USB_MODE_L) ||
		      !gpio_get_level(GPIO_USBC_SS2_USB_MODE_L);
	int has_dp = !gpio_get_level(GPIO_USBC_DP_MODE_L);

	if (has_dp)
		*dp_str = gpio_get_level(GPIO_USBC_DP_POLARITY) ? "DP2" : "DP1";
	else
		*dp_str = NULL;

	if (has_usb)
		*usb_str = gpio_get_level(GPIO_USBC_SS1_USB_MODE_L) ?
				"USB2" : "USB1";
	else
		*usb_str = NULL;

	return has_ss;
}

/**
 * Discharge battery when on AC power for factory test.
 */
int board_discharge_on_ac(int enable)
{
	return charger_discharge_on_ac(enable);
}

int extpower_is_present(void)
{
	return gpio_get_level(GPIO_CHGR_ACOK);
}

/*
 * Disconnect the USB lines from the AP, this enables manual control of the
 * Pericom polarity switch and disconnects the USB 2.0 lines
 */
void ccd_board_connect(void)
{
	pi3usb9281_set_pins(0, 0x00);
	pi3usb9281_set_switch_manual(0, 0);
}

/*
 * Reconnect the USB lines to the AP re-enabling automatic switching
 */
void ccd_board_disconnect(void)
{
	pi3usb9281_set_switch_manual(0, 1);
}

void usb_board_connect(void)
{
	gpio_set_level(GPIO_USB_PU_EN_L, 0);
}

void usb_board_disconnect(void)
{
	gpio_set_level(GPIO_USB_PU_EN_L, 1);
}

/* Charge manager callback function, called on delayed override timeout */
void board_charge_manager_override_timeout(void)
{
	/* TODO: Implement me! */
}
DECLARE_DEFERRED(board_charge_manager_override_timeout);

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
	int ret = EC_SUCCESS;

	if (charge_port >= 0 && charge_port < PD_PORT_COUNT &&
	    pd_get_role(charge_port) != PD_ROLE_SINK) {
		CPRINTS("Port %d is not a sink, skipping enable", charge_port);
		charge_port = CHARGE_PORT_NONE;
		ret = EC_ERROR_INVAL;
	}
	if (charge_port == CHARGE_PORT_NONE) {
		/* Disable charging */
		charge_set_input_current_limit(0);
	}

	return ret;
}

/**
 * Set the charge limit based upon desired maximum.
 *
 * @param charge_ma     Desired charge limit (mA).
 */
void board_set_charge_limit(int charge_ma)
{
	int rv = charge_set_input_current_limit(MAX(charge_ma,
					CONFIG_CHARGER_INPUT_CURRENT));
	if (rv < 0)
		CPRINTS("Failed to set input current limit for PD");
}
