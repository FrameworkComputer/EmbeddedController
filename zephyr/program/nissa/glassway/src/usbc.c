/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "chipset.h"
#include "driver/charger/isl923x_public.h"
#include "driver/retimer/anx7483_public.h"
#include "driver/tcpm/raa489000.h"
#include "driver/tcpm/tcpci.h"
#include "hooks.h"
#include "system.h"
#include "usb_mux.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

int board_is_sourcing_vbus(int port)
{
	int regval;

	tcpc_read(port, TCPC_REG_POWER_STATUS, &regval);
	return !!(regval & TCPC_REG_POWER_STATUS_SOURCING_VBUS);
}

int board_set_active_charge_port(int port)
{
	int is_real_port = (port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);
	int i;
	int old_port;

	if (!is_real_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	old_port = charge_manager_get_active_charge_port();

	LOG_INF("New chg p%d", port);

	/* Disable all ports. */
	if (port == CHARGE_PORT_NONE) {
		for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
			tcpc_write(i, TCPC_REG_COMMAND,
				   TCPC_REG_COMMAND_SNK_CTRL_LOW);
			raa489000_enable_asgate(i, false);
		}

		return EC_SUCCESS;
	}

	/* Check if port is sourcing VBUS. */
	if (board_is_sourcing_vbus(port)) {
		LOG_WRN("Skip enable p%d", port);
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
			LOG_WRN("p%d: sink path disable failed.", i);
		raa489000_enable_asgate(i, false);
	}

	/*
	 * Stop the charger IC from switching while changing ports.  Otherwise,
	 * we can overcurrent the adapter we're switching to. (crbug.com/926056)
	 */
	if (old_port != CHARGE_PORT_NONE)
		charger_discharge_on_ac(1);

	/* Enable requested charge port. */
	if (raa489000_enable_asgate(port, true) ||
	    tcpc_write(port, TCPC_REG_COMMAND,
		       TCPC_REG_COMMAND_SNK_CTRL_HIGH)) {
		LOG_WRN("p%d: sink path enable failed.", port);
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

__override void typec_set_source_current_limit(int port, enum tcpc_rp_value rp)
{
	if (port < 0 || port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return;

	raa489000_set_output_current(port, rp);
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

	rv = raa489000_enable_asgate(port, true);
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
 * the interrupt, and the line will be held low. We handle this by polling the
 * IRQ GPIO on the USB-PD task after processing TCPC interrupts, synchronously
 * running the BC1.2 interrupt handler to ensure we continue processing
 * interrupts as long as either source is asserting the IRQ.
 */
void board_process_pd_alert(int port)
{
	const struct gpio_dt_spec *gpio;

	if (port == 0) {
		gpio = GPIO_DT_FROM_NODELABEL(gpio_usb_c0_int_odl);
	} else {
		gpio = GPIO_DT_FROM_ALIAS(gpio_usb_c1_int_odl);
	}

	/*
	 * Immediately schedule another TCPC interrupt if it seems we haven't
	 * cleared all pending interrupts.
	 */
	if (!gpio_pin_get_dt(gpio))
		schedule_deferred_pd_interrupt(port);
}

/*
 * LCOV_EXCL_START schedule_deferred_pd_interrupt() can't be verified in tests,
 * but type-C will be obviously broken if this function doesn't work.
 */
void usb_interrupt(enum gpio_signal signal)
{
	int port;

	if (signal == GPIO_SIGNAL(DT_NODELABEL(gpio_usb_c0_int_odl))) {
		port = 0;
	} else {
		port = 1;
	}
	/* Trigger polling of TCPC and BC1.2 in USB-PD task */
	schedule_deferred_pd_interrupt(port);
}
/* LCOV_EXCL_STOP */
