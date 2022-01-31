/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state_v2.h"
#include "chipset.h"
#include "hooks.h"
#include "usb_mux.h"
#include "system.h"
#include "driver/charger/isl923x_public.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/raa489000.h"

#include "sub_board.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C0_TCPC,
			.addr_flags = RAA489000_TCPC0_I2C_FLAGS,
		},
		.drv = &raa489000_tcpm_drv,
		/* RAA489000 implements TCPCI 2.0 */
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
	{ /* sub-board */
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C1_TCPC,
			.addr_flags = RAA489000_TCPC0_I2C_FLAGS,
		},
		.drv = &raa489000_tcpm_drv,
		/* RAA489000 implements TCPCI 2.0 */
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
};

/*
 * Board specific hibernate functions.
 */
__override void board_hibernate(void)
{
	/* Shut down the chargers */
	if (board_get_usb_pd_port_count() == 2)
		raa489000_hibernate(CHARGER_SECONDARY, true);
	raa489000_hibernate(CHARGER_PRIMARY, true);
	CPRINTS("Charger(s) hibernated");
	cflush();
}

__override void board_hibernate_late(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_slp_z), 1);
	/*
	 * The system should hibernate, but there may be
	 * a small delay, so return.
	 */
}

int board_is_sourcing_vbus(int port)
{
	int regval;

	tcpc_read(port, TCPC_REG_POWER_STATUS, &regval);
	return !!(regval & TCPC_REG_POWER_STATUS_SOURCING_VBUS);
}

int board_set_active_charge_port(int port)
{
	int is_real_port = (port >= 0 &&
			    port < CONFIG_USB_PD_PORT_MAX_COUNT);
	int i;
	int old_port;

	if (!is_real_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	old_port = charge_manager_get_active_charge_port();

	CPRINTS("New chg p%d", port);

	/* Disable all ports. */
	if (port == CHARGE_PORT_NONE) {
		for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
			tcpc_write(i, TCPC_REG_COMMAND,
				   TCPC_REG_COMMAND_SNK_CTRL_LOW);

		return EC_SUCCESS;
	}

	/* Check if port is sourcing VBUS. */
	if (board_is_sourcing_vbus(port)) {
		CPRINTS("Skip enable p%d", port);
		return EC_ERROR_INVAL;
	}

	/*
	 * Turn off the other ports' sink path FETs, before enabling the
	 * requested charge port.
	 */
	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		if (i == port)
			continue;

		if (tcpc_write(i, TCPC_REG_COMMAND,
			       TCPC_REG_COMMAND_SNK_CTRL_LOW))
			CPRINTS("p%d: sink path disable failed.", i);
	}

	/*
	 * Stop the charger IC from switching while changing ports.  Otherwise,
	 * we can overcurrent the adapter we're switching to. (crbug.com/926056)
	 */
	if (old_port != CHARGE_PORT_NONE)
		charger_discharge_on_ac(1);

	/* Enable requested charge port. */
	if (tcpc_write(port, TCPC_REG_COMMAND,
		       TCPC_REG_COMMAND_SNK_CTRL_HIGH)) {
		CPRINTS("p%d: sink path enable failed.", port);
		charger_discharge_on_ac(0);
		return EC_ERROR_UNKNOWN;
	}

	/* Allow the charger IC to begin/continue switching. */
	charger_discharge_on_ac(0);

	return EC_SUCCESS;
}

uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;
	int regval;

	/*
	 * The interrupt line is shared between the TCPC and BC1.2 detector IC.
	 * Therefore, go out and actually read the alert registers to report the
	 * alert status.
	 */
	if (!gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_usb_c0_int_odl))) {
		if (!tcpc_read16(0, TCPC_REG_ALERT, &regval)) {
			/* The TCPCI Rev 1.0 spec says to ignore bits 14:12. */
			if (!(tcpc_config[0].flags & TCPC_FLAGS_TCPCI_REV2_0))
				regval &= ~((1 << 14) | (1 << 13) | (1 << 12));

			if (regval)
				status |= PD_STATUS_TCPC_ALERT_0;
		}
	}

	if (board_get_usb_pd_port_count() == 2 &&
	    !gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_usb_c1_int_odl))) {
		if (!tcpc_read16(1, TCPC_REG_ALERT, &regval)) {
			/* TCPCI spec Rev 1.0 says to ignore bits 14:12. */
			if (!(tcpc_config[1].flags & TCPC_FLAGS_TCPCI_REV2_0))
				regval &= ~((1 << 14) | (1 << 13) | (1 << 12));

			if (regval)
				status |= PD_STATUS_TCPC_ALERT_1;
		}
	}

	return status;
}

void pd_power_supply_reset(int port)
{
	/* Disable VBUS */
	tcpc_write(port, TCPC_REG_COMMAND, TCPC_REG_COMMAND_SRC_CTRL_LOW);

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}

int pd_set_power_supply_ready(int port)
{
	int rv;

	if (port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return EC_ERROR_INVAL;

	/* Disable charging. */
	rv = tcpc_write(port, TCPC_REG_COMMAND, TCPC_REG_COMMAND_SNK_CTRL_LOW);
	if (rv)
		return rv;

	/* Our policy is not to source VBUS when the AP is off. */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return EC_ERROR_NOT_POWERED;

	/* Provide Vbus. */
	rv = tcpc_write(port, TCPC_REG_COMMAND, TCPC_REG_COMMAND_SRC_CTRL_HIGH);
	if (rv)
		return rv;

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

void board_reset_pd_mcu(void)
{
	/*
	 * TODO(b:147316511): could send a reset command to the TCPC here
	 * if needed.
	 */
}

/*
 * Because the TCPCs and BC1.2 chips share interrupt lines, it's possible
 * for an interrupt to be lost if one asserts the IRQ, the other does the same
 * then the first releases it: there will only be one falling edge to trigger
 * the interrupt, and the line will be held low. We handle this by running a
 * deferred check after a falling edge to see whether the IRQ is still being
 * asserted. If it is, we assume an interrupt may have been lost and we need
 * to poll each chip for events again.
 */
#define USBC_INT_POLL_DELAY_US 5000

static void poll_c0_int(void);
DECLARE_DEFERRED(poll_c0_int);
static void poll_c1_int(void);
DECLARE_DEFERRED(poll_c1_int);

static void usbc_interrupt_trigger(int port)
{
	schedule_deferred_pd_interrupt(port);
	task_set_event(USB_CHG_PORT_TO_TASK_ID(port), USB_CHG_EVENT_BC12);
}

static inline void poll_usb_gpio(int port,
				 const struct gpio_dt_spec *gpio,
				 const struct deferred_data *ud)
{
	if (!gpio_pin_get_dt(gpio)) {
		usbc_interrupt_trigger(port);
		hook_call_deferred(ud, USBC_INT_POLL_DELAY_US);
	}
}

static void poll_c0_int (void)
{
	poll_usb_gpio(0,
		      GPIO_DT_FROM_NODELABEL(gpio_usb_c0_int_odl),
		      &poll_c0_int_data);
}

static void poll_c1_int (void)
{
	poll_usb_gpio(1,
		      GPIO_DT_FROM_ALIAS(gpio_usb_c1_int_odl),
		      &poll_c1_int_data);
}

void usb_interrupt(enum gpio_signal signal)
{
	int port;
	const struct deferred_data *ud;

	if (signal == GPIO_SIGNAL(DT_NODELABEL(gpio_usb_c0_int_odl))) {
		port = 0;
		ud = &poll_c0_int_data;
	} else {
		port = 1;
		ud = &poll_c1_int_data;
	}
	/*
	 * We've just been called from a falling edge, so there's definitely
	 * no lost IRQ right now. Cancel any pending check.
	 */
	hook_call_deferred(ud, -1);
	/* Trigger polling of TCPC and BC1.2 in respective tasks */
	usbc_interrupt_trigger(port);
	/* Check for lost interrupts in a bit */
	hook_call_deferred(ud, USBC_INT_POLL_DELAY_US);
}
