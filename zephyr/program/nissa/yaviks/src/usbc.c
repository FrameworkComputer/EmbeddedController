/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "chipset.h"
#include "driver/charger/sm5803.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "driver/tcpm/tcpci.h"
#include "hooks.h"
#include "system.h"
#include "usb_mux.h"
#include "watchdog.h"

#include <zephyr/logging/log.h>

#include <ap_power/ap_power.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

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
	return sm5803_check_vbus_level(port, level);
}

/*
 * Putting chargers into LPM when in suspend reduces power draw by about 8mW
 * per charger, but also seems critical to correct operation in source mode:
 * if chargers are not in LPM when a sink is first connected, VBUS sourcing
 * works even if the partner is later removed (causing LPM entry) and
 * reconnected (causing LPM exit). If in LPM initially, sourcing VBUS
 * consistently causes the charger to report (apparently spurious) overcurrent
 * failures.
 *
 * In short, this is important to making things work correctly but we don't
 * understand why.
 */
static void board_chargers_suspend(struct ap_power_ev_callback *const cb,
				   const struct ap_power_ev_data data)
{
	void (*fn)(int chgnum);

	switch (data.event) {
	case AP_POWER_SUSPEND:
		fn = sm5803_enable_low_power_mode;
		break;
	case AP_POWER_RESUME:
		fn = sm5803_disable_low_power_mode;
		break;
	default:
		LOG_WRN("%s: power event %d is not recognized", __func__,
			data.event);
		return;
	}

	fn(CHARGER_PRIMARY);
	if (board_get_charger_chip_count() > 1)
		fn(CHARGER_SECONDARY);
}

static int board_chargers_suspend_init(void)
{
	static struct ap_power_ev_callback cb = {
		.handler = board_chargers_suspend,
		.events = AP_POWER_SUSPEND | AP_POWER_RESUME,
	};
	ap_power_ev_add_callback(&cb);
	return 0;
}
SYS_INIT(board_chargers_suspend_init, APPLICATION, 0);

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
	if ((port == CHARGE_PORT_NONE) || (old_port == port))
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
	    !gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_int_odl))) {
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

__override void board_set_charge_limit(int port, int supplier, int charge_ma,
				       int max_ma, int charge_mv)
{
	/*
	 * b:213937755: Yaviks C1 port is OCPC (One Charger IC Per Type-C)
	 * architecture, The charging current is controlled by increasing Vsys.
	 * However, the charger SM5803 is not limit current while Vsys
	 * increasing, we can see the current overshoot to ~3.6A to cause
	 * C1 port brownout with low power charger (5V). To avoid C1 port
	 * brownout at low power charger connected. Limit charge current to 2A.
	 */
	if (charge_mv <= 5000 && port == 1)
		charge_ma = MIN(charge_ma, 2000);
	else
		charge_ma = charge_ma * 96 / 100;

	charge_set_input_current_limit(charge_ma, charge_mv);
}

void board_reset_pd_mcu(void)
{
	/*
	 * Do nothing. The integrated TCPC for C0 lacks a dedicated reset
	 * command, and C1 (if present) doesn't have a reset pin connected
	 * to the EC.
	 */
}

#define INT_RECHECK_US 5000

/* C0 interrupt line shared by BC 1.2 and charger */

static void check_c0_line(void);
DECLARE_DEFERRED(check_c0_line);

static void notify_c0_chips(void)
{
	usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
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
void usb_c1_interrupt(enum gpio_signal s)
{
	/* Charger and BC1.2 are handled in board_process_pd_alert */
	schedule_deferred_pd_interrupt(1);
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
	/*
	 * C1 port IRQ already handled by board_process_pd_alert(), we don't
	 * need check IRQ here at initial.
	 */
}
/*
 * This must run after sub-board detection (which happens in EC main()),
 * but isn't depended on by anything else either.
 */
DECLARE_HOOK(HOOK_INIT, board_handle_initial_typec_irq, HOOK_PRIO_LAST);

/*
 * Handle charger interrupts in the PD task. Not doing so can lead to a priority
 * inversion where we fail to respond to TCPC alerts quickly enough because we
 * don't get another edge on a shared IRQ until the other interrupt is cleared
 * (or the IRQ is polled again), which happens in lower-priority tasks: the
 * high-priority type-C handler is thus blocked on the lower-priority one(s).
 *
 * To avoid that, we run charger and BC1.2 interrupts synchronously alongside
 * PD interrupts so they have the same priority.
 */
void board_process_pd_alert(int port)
{
	/*
	 * Port 0 doesn't use an external TCPC, so its interrupts don't need
	 * this special handling.
	 */
	if (port != 1)
		return;

	if (!gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_int_odl))) {
		sm5803_handle_interrupt(port);
		usb_charger_task_set_event_sync(1, USB_CHG_EVENT_BC12);
	}
	/*
	 * Immediately schedule another TCPC interrupt if it seems we haven't
	 * cleared all pending interrupts.
	 */
	if (!gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_usb_c1_int_odl)))
		schedule_deferred_pd_interrupt(port);

	/*
	 * b:273208597: There are some peripheral display docks will
	 * issue HPDs in the short time. TCPM must wake up pd_task
	 * continually to service the events. They may cause the
	 * watchdog to reset. This patch placates watchdog after
	 * receiving dp_attention.
	 */
	watchdog_reload();
}

int pd_snk_is_vbus_provided(int port)
{
	int chg_det = 0;

	sm5803_get_chg_det(port, &chg_det);

	return chg_det;
}
