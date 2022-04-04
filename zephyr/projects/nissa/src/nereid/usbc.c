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

#include "nissa_common.h"

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
		 * Sub-board: optional PS8745 TCPC+redriver. Behaves the same
		 * as PS8815.
		 */
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_USB_C1_TCPC,
			.addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		},
		.drv = &ps8xxx_tcpm_drv,
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
	int is_real_port = (port >= 0 && port < board_get_usb_pd_port_count());
	int i;
	int old_port;
	int rv;

	if (!is_real_port && port != CHARGE_PORT_NONE)
		return EC_ERROR_INVAL;

	old_port = charge_manager_get_active_charge_port();
	LOG_INF("Charge update: p%d -> p%d", old_port, port);

	/* Check if port is sourcing VBUS. */
	if (port != CHARGE_PORT_NONE && charger_is_sourcing_otg_power(port)) {
		LOG_WRN("Skip enable p%d: already sourcing", port);
		return EC_ERROR_INVAL;
	}

	/* Disable sinking on all ports except the desired one */
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (i == port)
			continue;

		if (sm5803_vbus_sink_enable(i, 0))
			/*
			 * Do not early-return because this can fail during
			 * power-on which would put us into a loop.
			 */
			LOG_WRN("p%d: sink path disable failed.", i);
	}

	/* Don't enable anything (stop here) if no ports were requested */
	if (port == CHARGE_PORT_NONE)
		return EC_SUCCESS;

	/*
	 * Stop the charger IC from switching while changing ports.  Otherwise,
	 * we can overcurrent the adapter we're switching to. (crbug.com/926056)
	 */
	if (old_port != CHARGE_PORT_NONE)
		charger_discharge_on_ac(1);

	/* Enable requested charge port. */
	rv = sm5803_vbus_sink_enable(port, 1);
	if (rv)
		LOG_WRN("p%d: sink path enable failed: code %d", port, rv);

	/* Allow the charger IC to begin/continue switching. */
	charger_discharge_on_ac(0);

	return rv;
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
	enum ec_error_list rv;

	if (port < 0 || port > board_get_usb_pd_port_count()) {
		LOG_WRN("Port C%d does not exist, cannot enable VBUS", port);
		return EC_ERROR_INVAL;
	}

	/* Disable sinking */
	rv = sm5803_vbus_sink_enable(port, 0);
	if (rv) {
		LOG_WRN("C%d failed to disable sinking: %d", port, rv);
		return rv;
	}

	/* Disable Vbus discharge */
	rv = sm5803_set_vbus_disch(port, 0);
	if (rv) {
		LOG_WRN("C%d failed to clear VBUS discharge: %d", port, rv);
		return rv;
	}

	/* Provide Vbus */
	rv = charger_enable_otg_power(port, 1);
	if (rv) {
		LOG_WRN("C%d failed to enable VBUS sourcing: %d", port, rv);
		return rv;
	}

	/* Notify host of power info change. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);

	return EC_SUCCESS;
}

__override void typec_set_source_current_limit(int port, enum tcpc_rp_value rp)
{
	int rv;
	const int current = rp == TYPEC_RP_3A0 ? 3000 : 1500;

	rv = charger_set_otg_current_voltage(port, current, 5000);
	if (rv != EC_SUCCESS) {
		LOG_WRN("Failed to set source ilimit on port %d to %d: %d",
			port, current, rv);
	}
}

void board_reset_pd_mcu(void)
{
	/*
	 * Do nothing. The integrated TCPC for C0 lacks a dedicated reset
	 * command, and C1 (if present) doesn't have a reset pin connected
	 * to the EC.
	 */
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
	/* Charger is handled in board_process_pd_alert */
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

/*
 * Check state of IRQ lines at startup, ensuring an IRQ that happened before
 * the EC started up won't get lost (leaving the IRQ line asserted and blocking
 * any further interrupts on the port).
 *
 * Although the PD task will check for pending TCPC interrupts on startup,
 * the charger sharing the IRQ will not be polled automatically.
 */
void board_handle_initial_typec_irq(void)
{
	check_c0_line();
	check_c1_line();
}
/*
 * This must run after sub-board detection (which happens in EC main()),
 * but isn't depended on by anything else either.
 */
DECLARE_HOOK(HOOK_INIT, board_handle_initial_typec_irq, HOOK_PRIO_LAST);

/*
 * Handle charger interrupts in the PD task. Not doing so can lead to a priority
 * inversion where we fail to respond to TCPC alerts quickly enough because we
 * don't get another edge on a shared IRQ until the charger interrupt is cleared
 * (or the IRQ is polled again), which happens in the low-priority charger task:
 * the high-priority type-C handler is thus blocked on the lower-priority
 * charger.
 *
 * To avoid that, we run charger interrupts at the same priority.
 */
void board_process_pd_alert(int port)
{
	/*
	 * Port 0 doesn't use an external TCPC, so its interrupts don't need
	 * this special handling.
	 */
	if (port == 1 &&
	    !gpio_pin_get_dt(GPIO_DT_FROM_ALIAS(gpio_usb_c1_int_odl))) {
		sm5803_handle_interrupt(port);
	}
}

int pd_snk_is_vbus_provided(int port)
{
	int chg_det = 0;

	sm5803_get_chg_det(port, &chg_det);

	return chg_det;
}


const struct usb_mux *nissa_get_c1_sb_mux(void)
{
	/*
	 * Use TCPC-integrated mux via CONFIG_STANDARD_OUTPUT register
	 * in PS8745.
	 */
	static const struct usb_mux usbc1_tcpc_mux = {
		.usb_port = 1,
		.i2c_port = I2C_PORT_USB_C1_TCPC,
		.i2c_addr_flags = PS8XXX_I2C_ADDR1_FLAGS,
		.driver = &tcpci_tcpm_usb_mux_driver,
		.hpd_update = &ps8xxx_tcpc_update_hpd_status,
	};

	return &usbc1_tcpc_mux;
}
