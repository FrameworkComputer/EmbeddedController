/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <logging/log.h>

#include "charge_state_v2.h"
#include "chipset.h"
#include "hooks.h"
#include "usb_mux.h"
#include "system.h"
#include "driver/charger/sm5803.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "driver/tcpm/tcpci.h"

#include "sub_board.h"

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_EMBEDDED,
		/* TCPC is embedded within EC so no i2c config needed */
		.drv = &it8xxx2_tcpm_drv,
		/* Alert is active-low, push-pull */
		.flags = 0,
	},
	{
		/*
		 * Sub-board: optional PS8745 TCPC+redriver. Works like other
		 * PS8xxx chips but TCPCI-only; no mux.
		 */
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C1_TCPC,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &tcpci_tcpm_drv,
		/* PS8745 implements TCPCI 2.0 */
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
};

/* Vconn control for integrated ITE TCPC */
void board_pd_vconn_ctrl(int port, enum usbpd_cc_pin cc_pin, int enabled)
{
	/* Vconn control is only for port 0 */
	if (port)
		return;

	if (cc_pin == USBPD_CC_PIN_1)
		gpio_pin_set_dt(
			GPIO_DT_FROM_NODELABEL(gpio_en_usb_c0_cc1_vconn),
			!!enabled);
	else
		gpio_pin_set_dt(
			GPIO_DT_FROM_NODELABEL(gpio_en_usb_c0_cc2_vconn),
			!!enabled);
}

__override bool pd_check_vbus_level(int port, enum vbus_level level)
{
	/*
	 * While the charger can differentiate SAFE0V from REMOVED, doing so
	 * requires doing a I2C read of the VBUS analog level. Because this
	 * function can be polled by the USB state machines and doing the I2C
	 * read is relatively costly, we only check the cached VBUS presence
	 * (for which interrupts record transitions).
	 */
	switch (level) {
	case VBUS_PRESENT:
		return sm5803_is_vbus_present(port);
	case VBUS_SAFE0V:	/* Less than vSafe0V */
	case VBUS_REMOVED:	/* Less than vSinkDisconnect */
		return !sm5803_is_vbus_present(port);
	}
	LOG_WRN("Unrecognized vbus_level value: %d", level);
	return false;
}

int board_set_active_charge_port(int port)
{
	return EC_SUCCESS;
}

uint16_t tcpc_get_alert_status(void)
{
	/*
	 * TCPC 0 is embedded in the EC and processes interrupts in the chip
	 * code (it83xx/intc.c). This function only needs to poll port C1 if
	 * present.
	 */
	uint16_t status = 0;
	int regval;

	/* Is the C1 port present and its IRQ line asserted? */
	if (board_get_usb_pd_port_count() == 2 &&
	    !gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_usb_c1_int_odl))) {
		/*
		 * C1 IRQ is shared between BC1.2 and TCPC; poll TCPC to see if
		 * it asserted the IRQ.
		 */
		if (!tcpc_read16(1, TCPC_REG_ALERT, &regval)) {
			if (regval)
				status = PD_STATUS_TCPC_ALERT_1;
		}
	}

	return status;
}

/*
 * TODO(b/201000844): Fill in missing functions.
 */

void pd_power_supply_reset(int port)
{
	int prev_en;

	if (port < 0 || port >= board_get_usb_pd_port_count())
		return;

	prev_en = charger_is_sourcing_otg_power(port);

	/* Disable Vbus */
	charger_enable_otg_power(port, 0);

	/* Discharge Vbus if previously enabled */
	if (prev_en)
		sm5803_set_vbus_disch(port, 1);

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_set_power_supply_ready(int port)
{
	return EC_SUCCESS;
}

void board_reset_pd_mcu(void)
{
}

#define INT_RECHECK_US	5000

/* C0 interrupt line shared by BC 1.2 and charger */

static void check_c0_line(void);
DECLARE_DEFERRED(check_c0_line);

static void notify_c0_chips(void)
{
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12);
	sm5803_interrupt(0);
}

static void check_c0_line(void)
{
	/*
	 * If line is still being held low, see if there's more to process from
	 * one of the chips
	 */
	if (!gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_usb_c0_int_odl))) {
		notify_c0_chips();
		hook_call_deferred(&check_c0_line_data, INT_RECHECK_US);
	}
}

void usb_c0_interrupt(enum gpio_signal s)
{
	/* Cancel any previous calls to check the interrupt line */
	hook_call_deferred(&check_c0_line_data, -1);

	/* Notify all chips using this line that an interrupt came in */
	notify_c0_chips();

	/* Check the line again in 5ms */
	hook_call_deferred(&check_c0_line_data, INT_RECHECK_US);
}

/* C1 interrupt line shared by BC 1.2, TCPC, and charger */
static void check_c1_line(void);
DECLARE_DEFERRED(check_c1_line);

static void notify_c1_chips(void)
{
	schedule_deferred_pd_interrupt(1);
	task_set_event(TASK_ID_USB_CHG_P1, USB_CHG_EVENT_BC12);
}

static void check_c1_line(void)
{
	/*
	 * If line is still being held low, see if there's more to process from
	 * one of the chips.
	 */
	if (!gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_usb_c1_int_odl))) {
		notify_c1_chips();
		hook_call_deferred(&check_c1_line_data, INT_RECHECK_US);
	}
}

void usb_c1_interrupt(enum gpio_signal s)
{
	/* Cancel any previous calls to check the interrupt line */
	hook_call_deferred(&check_c1_line_data, -1);

	/* Notify all chips using this line that an interrupt came in */
	notify_c1_chips();

	/* Check the line again in 5ms */
	hook_call_deferred(&check_c1_line_data, INT_RECHECK_US);
}

int pd_snk_is_vbus_provided(int port)
{
	int chg_det = 0;

	sm5803_get_chg_det(port, &chg_det);

	return chg_det;
}
