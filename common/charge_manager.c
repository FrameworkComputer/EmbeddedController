/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "atomic.h"
#include "battery.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charger.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"
#include "tcpm.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

#define POWER(charge_port) ((charge_port.current) * (charge_port.voltage))

/* Timeout for delayed override power swap, allow for 500ms extra */
#define POWER_SWAP_TIMEOUT (PD_T_SRC_RECOVER_MAX + PD_T_SRC_TURN_ON + \
			    PD_T_SAFE_0V + 500 * MSEC)

/* Charge supplier priority: lower number indicates higher priority. */
test_mockable const int supplier_priority[] = {
	[CHARGE_SUPPLIER_PD] = 0,
	[CHARGE_SUPPLIER_TYPEC] = 1,
	[CHARGE_SUPPLIER_PROPRIETARY] = 1,
	[CHARGE_SUPPLIER_BC12_DCP] = 1,
	[CHARGE_SUPPLIER_BC12_CDP] = 2,
	[CHARGE_SUPPLIER_BC12_SDP] = 3,
	[CHARGE_SUPPLIER_OTHER] = 3,
	[CHARGE_SUPPLIER_VBUS] = 4
};
BUILD_ASSERT(ARRAY_SIZE(supplier_priority) == CHARGE_SUPPLIER_COUNT);

/* Keep track of available charge for each charge port. */
static struct charge_port_info available_charge[CHARGE_SUPPLIER_COUNT]
					       [CONFIG_USB_PD_PORT_COUNT];

/* Keep track of when the supplier on each port is registered. */
static timestamp_t registration_time[CONFIG_USB_PD_PORT_COUNT];

/*
 * Charge current ceiling (mA) for ports. This can be set to temporarily limit
 * the charge pulled from a port, without influencing the port selection logic.
 * The ceiling can be set independently from several requestors, with the
 * minimum ceiling taking effect.
 */
static int charge_ceil[CONFIG_USB_PD_PORT_COUNT][CEIL_REQUESTOR_COUNT];

/* Dual-role capability of attached partner port */
static enum dualrole_capabilities dualrole_capability[CONFIG_USB_PD_PORT_COUNT];

#ifdef CONFIG_USB_PD_LOGGING
/* Mark port as dirty when making changes, for later logging */
static int save_log[CONFIG_USB_PD_PORT_COUNT];
#endif

/* Store current state of port enable / charge current. */
static int charge_port = CHARGE_PORT_NONE;
static int charge_current = CHARGE_CURRENT_UNINITIALIZED;
static int charge_current_uncapped = CHARGE_CURRENT_UNINITIALIZED;
static int charge_voltage;
static int charge_supplier = CHARGE_SUPPLIER_NONE;
static int override_port = OVERRIDE_OFF;

static int delayed_override_port = OVERRIDE_OFF;
static timestamp_t delayed_override_deadline;

/* Bitmap of ports used as power source */
static volatile uint32_t source_port_bitmap;
BUILD_ASSERT(sizeof(source_port_bitmap)*8 >= CONFIG_USB_PD_PORT_COUNT);
static uint8_t source_port_last_rp[CONFIG_USB_PD_PORT_COUNT];

enum charge_manager_change_type {
	CHANGE_CHARGE,
	CHANGE_DUALROLE,
};

/**
 * In certain cases we need to override the default behavior of not charging
 * from non-dedicated chargers. If the system is in RO and locked, we have no
 * way of determining the actual dualrole capability of the charger because
 * PD communication is not allowed, so we must assume that it is dedicated.
 * Also, if no battery is present, the charger may be our only source of power,
 * so again we must assume that the charger is dedicated.
 */
static int charge_manager_spoof_dualrole_capability(void)
{
	int spoof_dualrole =  (system_get_image_copy() == SYSTEM_IMAGE_RO &&
			       system_is_locked()) ||
			       (battery_is_present() != BP_YES);
#ifdef CONFIG_BATTERY_REVIVE_DISCONNECT
	spoof_dualrole |= (battery_get_disconnect_state() !=
			   BATTERY_NOT_DISCONNECTED);
#endif
	return spoof_dualrole;
}

/**
 * Initialize available charge. Run before board init, so board init can
 * initialize data, if needed.
 */
static void charge_manager_init(void)
{
	int i, j;
	int spoof_capability = charge_manager_spoof_dualrole_capability();

	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; ++i) {
		for (j = 0; j < CHARGE_SUPPLIER_COUNT; ++j) {
			available_charge[j][i].current =
				CHARGE_CURRENT_UNINITIALIZED;
			available_charge[j][i].voltage =
				CHARGE_VOLTAGE_UNINITIALIZED;
		}
		for (j = 0; j < CEIL_REQUESTOR_COUNT; ++j)
			charge_ceil[i][j] = CHARGE_CEIL_NONE;
		dualrole_capability[i] = spoof_capability ? CAP_DEDICATED :
							    CAP_UNKNOWN;
		source_port_last_rp[i] = CONFIG_USB_PD_PULLUP;
	}
}
DECLARE_HOOK(HOOK_INIT, charge_manager_init, HOOK_PRIO_CHARGE_MANAGER_INIT);

/**
 * Returns 1 if all ports + suppliers have reported in with some initial charge,
 * 0 otherwise.
 */
static int charge_manager_is_seeded(void)
{
	/* Once we're seeded, we don't need to check again. */
	static int is_seeded;
	int i, j;

	if (is_seeded)
		return 1;

	for (i = 0; i < CHARGE_SUPPLIER_COUNT; ++i)
		for (j = 0; j < CONFIG_USB_PD_PORT_COUNT; ++j)
			if (available_charge[i][j].current ==
			    CHARGE_CURRENT_UNINITIALIZED ||
			    available_charge[i][j].voltage ==
			    CHARGE_VOLTAGE_UNINITIALIZED)
				return 0;

	is_seeded = 1;
	return 1;
}

#ifndef TEST_BUILD
static int charge_manager_get_source_current(int port)
{
	switch (source_port_last_rp[port]) {
	case TYPEC_RP_3A0:
		return 3000;
	case TYPEC_RP_1A5:
		return 1500;
	case TYPEC_RP_USB:
	default:
		return 500;
	}
}

/**
 * Fills passed power_info structure with current info about the passed port.
 */
static void charge_manager_fill_power_info(int port,
	struct ec_response_usb_pd_power_info *r)
{
	int sup = CHARGE_SUPPLIER_NONE;
	int i;

	/* Determine supplier information to show. */
	if (port == charge_port)
		sup = charge_supplier;
	else
		/* Find highest priority supplier */
		for (i = 0; i < CHARGE_SUPPLIER_COUNT; ++i)
			if (available_charge[i][port].current > 0 &&
			    available_charge[i][port].voltage > 0 &&
			    (sup == CHARGE_SUPPLIER_NONE ||
			     supplier_priority[i] <
			     supplier_priority[sup] ||
			    (supplier_priority[i] ==
			     supplier_priority[sup] &&
			     POWER(available_charge[i][port]) >
			     POWER(available_charge[sup]
						   [port]))))
				sup = i;

	/* Fill in power role */
	if (charge_port == port)
		r->role = USB_PD_PORT_POWER_SINK;
	else if (pd_is_connected(port) && pd_get_role(port) == PD_ROLE_SOURCE)
		r->role = USB_PD_PORT_POWER_SOURCE;
	else if (sup != CHARGE_SUPPLIER_NONE)
		r->role = USB_PD_PORT_POWER_SINK_NOT_CHARGING;
	else
		r->role = USB_PD_PORT_POWER_DISCONNECTED;

	/* Is port partner dual-role capable */
	r->dualrole = (dualrole_capability[port] == CAP_DUALROLE);

	if (sup == CHARGE_SUPPLIER_NONE ||
	    r->role == USB_PD_PORT_POWER_SOURCE) {
		r->type = USB_CHG_TYPE_NONE;
		r->meas.voltage_max = 0;
		r->meas.voltage_now = r->role == USB_PD_PORT_POWER_SOURCE ? 5000
									  : 0;
		r->meas.current_max = charge_manager_get_source_current(port);
		r->max_power = 0;
	} else {
#if defined(HAS_TASK_CHG_RAMP) || defined(CONFIG_CHARGE_RAMP_HW)
		/* Read ramped current if active charging port */
		int use_ramp_current = (charge_port == port);
#else
		const int use_ramp_current = 0;
#endif

		switch (sup) {
		case CHARGE_SUPPLIER_PD:
			r->type = USB_CHG_TYPE_PD;
			break;
		case CHARGE_SUPPLIER_TYPEC:
			r->type = USB_CHG_TYPE_C;
			break;
		case CHARGE_SUPPLIER_PROPRIETARY:
			r->type = USB_CHG_TYPE_PROPRIETARY;
			break;
		case CHARGE_SUPPLIER_BC12_DCP:
			r->type = USB_CHG_TYPE_BC12_DCP;
			break;
		case CHARGE_SUPPLIER_BC12_CDP:
			r->type = USB_CHG_TYPE_BC12_CDP;
			break;
		case CHARGE_SUPPLIER_BC12_SDP:
			r->type = USB_CHG_TYPE_BC12_SDP;
			break;
		case CHARGE_SUPPLIER_VBUS:
			r->type = USB_CHG_TYPE_VBUS;
			break;
		default:
			r->type = USB_CHG_TYPE_OTHER;
		}
		r->meas.voltage_max = available_charge[sup][port].voltage;

		/*
		 * Report unknown charger CHARGE_DETECT_DELAY after supplier
		 * change since PD negotiation may take time.
		 */
		if (get_time().val < registration_time[port].val +
				     CHARGE_DETECT_DELAY)
			r->type = USB_CHG_TYPE_UNKNOWN;

		if (use_ramp_current) {
			/* Current limit is output of ramp module */
			r->meas.current_lim = chg_ramp_get_current_limit();

			/*
			 * If ramp is allowed, then the max current depends
			 * on if ramp is stable. If ramp is stable, then
			 * max current is same as input current limit. If
			 * ramp is not stable, then we report the maximum
			 * current we could ramp up to for this supplier.
			 * If ramp is not allowed, max current is just the
			 * available charge current.
			 */
			if (board_is_ramp_allowed(sup)) {
				r->meas.current_max = chg_ramp_is_stable() ?
					r->meas.current_lim :
					board_get_ramp_current_limit(
					  sup,
					  available_charge[sup][port].current);
			} else {
				r->meas.current_max =
					available_charge[sup][port].current;
			}

			r->max_power =
				r->meas.current_max * r->meas.voltage_max;
		} else {
			r->meas.current_max = r->meas.current_lim =
				available_charge[sup][port].current;
			r->max_power = POWER(available_charge[sup][port]);
		}

		/*
		 * If we are sourcing power, or sinking but not charging, then
		 * VBUS must be 5V. If we are charging, then read VBUS ADC.
		 */
		if (r->role == USB_PD_PORT_POWER_SINK_NOT_CHARGING)
			r->meas.voltage_now = 5000;
		else {
#ifdef CONFIG_USB_PD_VBUS_DETECT_CHARGER
			r->meas.voltage_now = charger_get_vbus_level();
#else
			if (ADC_VBUS >= 0)
				r->meas.voltage_now =
					adc_read_channel(ADC_VBUS);
			else
				/* No VBUS ADC channel - voltage is unknown */
				r->meas.voltage_now = 0;
#endif
		}
	}
}
#endif /* TEST_BUILD */

#ifdef CONFIG_USB_PD_LOGGING
/**
 * Saves a power state log entry with the current info about the passed port.
 */
void charge_manager_save_log(int port)
{
	uint16_t flags = 0;
	struct ec_response_usb_pd_power_info pinfo;

	if (port < 0 || port >= CONFIG_USB_PD_PORT_COUNT)
		return;

	save_log[port] = 0;
	charge_manager_fill_power_info(port, &pinfo);

	/* Flags are stored in the data field */
	if (port == override_port)
		flags |= CHARGE_FLAGS_OVERRIDE;
	if (port == delayed_override_port)
		flags |= CHARGE_FLAGS_DELAYED_OVERRIDE;
	flags |= pinfo.role | (pinfo.type << CHARGE_FLAGS_TYPE_SHIFT) |
		 (pinfo.dualrole ? CHARGE_FLAGS_DUAL_ROLE : 0);

	pd_log_event(PD_EVENT_MCU_CHARGE,
		     PD_LOG_PORT_SIZE(port, sizeof(pinfo.meas)),
		     flags, &pinfo.meas);
}
#endif /* CONFIG_USB_PD_LOGGING */

/**
 * Attempt to switch to power source on port if applicable.
 */
static void charge_manager_switch_to_source(int port)
{
	if (port < 0 || port >= CONFIG_USB_PD_PORT_COUNT)
		return;

	/* If connected to dual-role device, then ask for a swap */
	if (dualrole_capability[port] == CAP_DUALROLE &&
	    pd_get_role(port) == PD_ROLE_SINK)
		pd_request_power_swap(port);
}

/**
 * Return the computed charge ceiling for a port, which represents the
 * minimum ceiling among all valid requestors.
 *
 * @param port	Charge port.
 * @return	Charge ceiling (mA) or CHARGE_CEIL_NONE.
 */
static int charge_manager_get_ceil(int port)
{
	int ceil = CHARGE_CEIL_NONE;
	int val, i;

	ASSERT(port >= 0 && port < CONFIG_USB_PD_PORT_COUNT);
	for (i = 0; i < CEIL_REQUESTOR_COUNT; ++i) {
		val = charge_ceil[port][i];
		if (val != CHARGE_CEIL_NONE &&
		    (ceil == CHARGE_CEIL_NONE || val < ceil))
			ceil = val;
	}

	return ceil;
}

/**
 * Select the 'best' charge port, as defined by the supplier heirarchy and the
 * ability of the port to provide power.
 */
static void charge_manager_get_best_charge_port(int *new_port,
						int *new_supplier)
{
	int supplier = CHARGE_SUPPLIER_NONE;
	int port = CHARGE_PORT_NONE;
	int best_port_power = -1, candidate_port_power;
	int i, j;

	/* Skip port selection on OVERRIDE_DONT_CHARGE. */
	if (override_port != OVERRIDE_DONT_CHARGE) {
		/*
		 * Charge supplier selection logic:
		 * 1. Prefer higher priority supply.
		 * 2. Prefer higher power over lower in case priority is tied.
		 * 3. Prefer current charge port over new port in case (1)
		 *    and (2) are tied.
		 * available_charge can be changed at any time by other tasks,
		 * so make no assumptions about its consistency.
		 */
		for (i = 0; i < CHARGE_SUPPLIER_COUNT; ++i)
			for (j = 0; j < CONFIG_USB_PD_PORT_COUNT; ++j) {
				/*
				 * Skip this supplier if there is no
				 * available charge.
				 */
				if (available_charge[i][j].current == 0 ||
				    available_charge[i][j].voltage == 0)
					continue;

				/*
				 * Don't select this port if we have a
				 * charge on another override port.
				 */
				if (override_port != OVERRIDE_OFF &&
				    override_port == port &&
				    override_port != j)
					continue;

#ifndef CONFIG_CHARGE_MANAGER_DRP_CHARGING
				/*
				 * Don't charge from a dual-role port unless
				 * it is our override port.
				 */
				if (dualrole_capability[j] != CAP_DEDICATED &&
				    override_port != j)
					continue;
#endif

				candidate_port_power =
					POWER(available_charge[i][j]);

				/* Select if no supplier chosen yet. */
				if (supplier == CHARGE_SUPPLIER_NONE ||
				/* ..or if supplier priority is higher. */
				    supplier_priority[i] <
				    supplier_priority[supplier] ||
				/* ..or if this is our override port. */
				   (j == override_port &&
				    port != override_port) ||
				/* ..or if priority is tied and.. */
				   (supplier_priority[i] ==
				    supplier_priority[supplier] &&
				/* candidate port can supply more power or.. */
				   (candidate_port_power > best_port_power ||
				/*
				 * candidate port is the active port and can
				 * supply the same amount of power.
				 */
				   (candidate_port_power == best_port_power &&
				    charge_port == j)))) {
					supplier = i;
					port = j;
					best_port_power = candidate_port_power;
				}
			}

	}

	*new_port = port;
	*new_supplier = supplier;
}

/**
 * Charge manager refresh -- responsible for selecting the active charge port
 * and charge power. Called as a deferred task.
 */
static void charge_manager_refresh(void)
{
	/* Always initialize charge port on first pass */
	static int active_charge_port_initialized;
	int new_supplier, new_port;
	int new_charge_current, new_charge_current_uncapped;
	int new_charge_voltage, i;
	int updated_new_port = CHARGE_PORT_NONE;
	int updated_old_port = CHARGE_PORT_NONE;
	int ceil;

	/* Hunt for an acceptable charge port */
	while (1) {
		charge_manager_get_best_charge_port(&new_port, &new_supplier);

		/*
		 * If the port or supplier changed, make an attempt to switch to
		 * the port. We will re-set the active port on a supplier change
		 * to give the board-level function another chance to reject
		 * the port, for example, if the port has become a charge
		 * source.
		 */
		if ((active_charge_port_initialized &&
		     new_port == charge_port &&
		     new_supplier == charge_supplier) ||
		     board_set_active_charge_port(new_port) == EC_SUCCESS)
			break;

		/*
		 * Allow 'Dont charge' request to be rejected only if it
		 * is our initial selection.
		 */
		if (new_port == CHARGE_PORT_NONE) {
			ASSERT(!active_charge_port_initialized);
			return;
		}

		/*
		 * Zero the available charge on the rejected port so that
		 * it is no longer chosen.
		 */
		for (i = 0; i < CHARGE_SUPPLIER_COUNT; ++i)
			available_charge[i][new_port].current = 0;
	}

	active_charge_port_initialized = 1;

	/*
	 * Clear override if it wasn't selected as the 'best' port -- it means
	 * that no charge is available on the port, or the port was rejected.
	 */
	if (override_port >= 0 && override_port != new_port)
		override_port = OVERRIDE_OFF;

	if (new_supplier == CHARGE_SUPPLIER_NONE) {
		new_charge_current = 0;
		new_charge_current_uncapped = 0;
		new_charge_voltage = 0;
	} else {
		new_charge_current_uncapped =
			available_charge[new_supplier][new_port].current;
#ifdef CONFIG_CHARGE_RAMP_HW
		/*
		 * Allow to set the maximum current value, so the hardware can
		 * know the range of acceptable current values for its ramping.
		 */
		if (board_is_ramp_allowed(new_supplier))
			new_charge_current_uncapped =
				board_get_ramp_current_limit(new_supplier,
						 new_charge_current_uncapped);
#endif /* CONFIG_CHARGE_RAMP_HW */
		/* Enforce port charge ceiling. */
		ceil = charge_manager_get_ceil(new_port);
		if (ceil != CHARGE_CEIL_NONE)
			new_charge_current = MIN(ceil,
						 new_charge_current_uncapped);
		else
			new_charge_current = new_charge_current_uncapped;

		new_charge_voltage =
			available_charge[new_supplier][new_port].voltage;
	}

	/* Change the charge limit + charge port/supplier if modified. */
	if (new_port != charge_port || new_charge_current != charge_current ||
	    new_supplier != charge_supplier) {
#ifdef HAS_TASK_CHG_RAMP
		chg_ramp_charge_supplier_change(
				new_port, new_supplier, new_charge_current,
				registration_time[new_port]);
#else
#ifdef CONFIG_CHARGE_RAMP_HW
		/* Enable or disable charge ramp */
		charger_set_hw_ramp(board_is_ramp_allowed(new_supplier));
#endif
		board_set_charge_limit(new_port, new_supplier,
					new_charge_current,
					new_charge_current_uncapped);
#endif /* HAS_TASK_CHG_RAMP */

		/* notify host of power info change */
		pd_send_host_event(PD_EVENT_POWER_CHANGE);

		CPRINTS("CL: p%d s%d i%d v%d", new_port, new_supplier,
			new_charge_current, new_charge_voltage);
	}

	/*
	 * Signal new power request only if the port changed, the voltage
	 * on the same port changed, or the actual uncapped current
	 * on the same port changed (don't consider ceil).
	 */
	if (new_port != CHARGE_PORT_NONE &&
	    (new_port != charge_port ||
	     new_charge_current_uncapped != charge_current_uncapped ||
	     new_charge_voltage != charge_voltage))
		updated_new_port = new_port;

	/* If charge port changed, cleanup old port */
	if (charge_port != new_port && charge_port != CHARGE_PORT_NONE) {
		/* Check if need power swap */
		charge_manager_switch_to_source(charge_port);
		/* Signal new power request on old port */
		updated_old_port = charge_port;
	}

	/* Update globals to reflect current state. */
	charge_current = new_charge_current;
	charge_current_uncapped = new_charge_current_uncapped;
	charge_voltage = new_charge_voltage;
	charge_supplier = new_supplier;
	charge_port = new_port;

#ifdef CONFIG_USB_PD_LOGGING
	/*
	 * Write a log under the following conditions:
	 *  1. A port becomes active or
	 *  2. A port becomes inactive or
	 *  3. The active charge port power limit changes or
	 *  4. Any supplier change on an inactive port
	 */
	if (updated_new_port != CHARGE_PORT_NONE)
		save_log[updated_new_port] = 1;
	/* Don't log non-meaningful changes on charge port */
	else if (charge_port != CHARGE_PORT_NONE)
		save_log[charge_port] = 0;

	if (updated_old_port != CHARGE_PORT_NONE)
		save_log[updated_old_port] = 1;

	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; ++i)
		if (save_log[i])
			charge_manager_save_log(i);
#endif

	/* New power requests must be set only after updating the globals. */
	if (updated_new_port != CHARGE_PORT_NONE)
		pd_set_new_power_request(updated_new_port);
	if (updated_old_port != CHARGE_PORT_NONE)
		pd_set_new_power_request(updated_old_port);
}
DECLARE_DEFERRED(charge_manager_refresh);

/**
 * Called when charge override times out waiting for power swap.
 */
static void charge_override_timeout(void)
{
	delayed_override_port = OVERRIDE_OFF;
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}
DECLARE_DEFERRED(charge_override_timeout);

/**
 * Called CHARGE_DETECT_DELAY after the most recent charge change on a port.
 */
static void charger_detect_debounced(void)
{
	/* Inform host that charger detection is debounced. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}
DECLARE_DEFERRED(charger_detect_debounced);

static void charge_manager_make_change(enum charge_manager_change_type change,
				       int supplier,
				       int port,
				       struct charge_port_info *charge)
{
	int i;
	int clear_override = 0;

	/* Determine if this is a change which can affect charge status */
	switch (change) {
	case CHANGE_CHARGE:
		/* Ignore changes where charge is identical */
		if (available_charge[supplier][port].current ==
		    charge->current &&
		    available_charge[supplier][port].voltage ==
		    charge->voltage)
			return;
		if (charge->current > 0 &&
		    available_charge[supplier][port].current == 0)
			clear_override = 1;
#ifdef CONFIG_USB_PD_LOGGING
		save_log[port] = 1;
#endif
		break;
	case CHANGE_DUALROLE:
		/*
		 * Ignore all except for transition to non-dualrole,
		 * which may occur some time after we see a charge
		 */
#ifndef CONFIG_CHARGE_MANAGER_DRP_CHARGING
		if (dualrole_capability[port] != CAP_DEDICATED)
#endif
			return;
		/* Clear override only if a charge is present on the port */
		for (i = 0; i < CHARGE_SUPPLIER_COUNT; ++i)
			if (available_charge[i][port].current > 0) {
				clear_override = 1;
				break;
			}
		/*
		 * If there is no charge present on the port, the dualrole
		 * change is meaningless to charge_manager.
		 */
		if (!clear_override)
			return;
		break;
	}

	/* Remove override when a charger is plugged */
	if (clear_override && override_port != port
#ifndef CONFIG_CHARGE_MANAGER_DRP_CHARGING
	    /* only remove override when it's a dedicated charger */
	    && dualrole_capability[port] == CAP_DEDICATED
#endif
	    ) {
		override_port = OVERRIDE_OFF;
		if (delayed_override_port != OVERRIDE_OFF) {
			delayed_override_port = OVERRIDE_OFF;
			hook_call_deferred(&charge_override_timeout_data, -1);
		}
	}

	if (change == CHANGE_CHARGE) {
		available_charge[supplier][port].current = charge->current;
		available_charge[supplier][port].voltage = charge->voltage;
		registration_time[port] = get_time();

		/*
		 * After CHARGE_DETECT_DELAY, inform the host that charger
		 * detection has been debounced. Since only one deferred
		 * routine exists for all ports, the deferred call for a given
		 * port may potentially be cancelled. This is mostly harmless
		 * since cancellation implies that PD_EVENT_POWER_CHANGE was
		 * just sent due to the power change on another port.
		 */
		if (charge->current > 0)
			hook_call_deferred(&charger_detect_debounced_data,
					   CHARGE_DETECT_DELAY);

		/*
		 * If we have a charge on our delayed override port within
		 * the deadline, make it our override port.
		*/
		if (port == delayed_override_port && charge->current > 0 &&
		    pd_get_role(delayed_override_port) == PD_ROLE_SINK &&
		    get_time().val < delayed_override_deadline.val) {
			delayed_override_port = OVERRIDE_OFF;
			hook_call_deferred(&charge_override_timeout_data, -1);
			charge_manager_set_override(port);
		}
	}

	/*
	 * Don't call charge_manager_refresh unless all ports +
	 * suppliers have reported in. We don't want to make changes
	 * to our charge port until we are certain we know what is
	 * attached.
	 */
	if (charge_manager_is_seeded())
		hook_call_deferred(&charge_manager_refresh_data, 0);
}

/**
 * Update available charge for a given port / supplier.
 *
 * @param supplier		Charge supplier to update.
 * @param port			Charge port to update.
 * @param charge		Charge port current / voltage.
 */
void charge_manager_update_charge(int supplier,
				  int port,
				  struct charge_port_info *charge)
{
	ASSERT(supplier >= 0 && supplier < CHARGE_SUPPLIER_COUNT);
	ASSERT(port >= 0 && port < CONFIG_USB_PD_PORT_COUNT);
	ASSERT(charge != NULL);

	charge_manager_make_change(CHANGE_CHARGE, supplier, port, charge);
}

/**
 * Notify charge_manager of a partner dualrole capability change.
 *
 * @param port			Charge port which changed.
 * @param cap			New port capability.
 */
void charge_manager_update_dualrole(int port, enum dualrole_capabilities cap)
{
	ASSERT(port >= 0 && port < CONFIG_USB_PD_PORT_COUNT);

	if (charge_manager_spoof_dualrole_capability())
		cap = CAP_DEDICATED;

	/* Ignore when capability is unchanged */
	if (cap != dualrole_capability[port]) {
		dualrole_capability[port] = cap;
		charge_manager_make_change(CHANGE_DUALROLE, 0, port, NULL);
	}
}

/**
 * Update charge ceiling for a given port. The ceiling can be set independently
 * for several requestors, and the min. ceil will be enforced.
 *
 * @param port			Charge port to update.
 * @param requestor		Charge ceiling requestor.
 * @param ceil			Charge ceiling (mA).
 */
void charge_manager_set_ceil(int port, enum ceil_requestor requestor, int ceil)
{
	ASSERT(port >= 0 && port < CONFIG_USB_PD_PORT_COUNT &&
	       requestor >= 0 && requestor < CEIL_REQUESTOR_COUNT);

	if (charge_ceil[port][requestor] != ceil) {
		charge_ceil[port][requestor] = ceil;
		if (port == charge_port && charge_manager_is_seeded())
			hook_call_deferred(&charge_manager_refresh_data, 0);
	}
}

/**
 * Select an 'override port', a port which is always the preferred charge port.
 * Returns EC_SUCCESS on success, ec_error_list status on failure.
 *
 * @param port			Charge port to select as override, or
 *				OVERRIDE_OFF to select no override port,
 *				or OVERRIDE_DONT_CHARGE to specifc that no
 *				charge port should be selected.
 */
int charge_manager_set_override(int port)
{
	int retval = EC_SUCCESS;

	ASSERT(port >= OVERRIDE_DONT_CHARGE && port < CONFIG_USB_PD_PORT_COUNT);

	CPRINTS("Charge Override: %d", port);

	/*
	 * If attempting to change the override port, then return
	 * error. Since we may be in the middle of a power swap on
	 * the original override port, it's too complicated to
	 * guarantee that the original override port is switched back
	 * to source.
	 */
	if (delayed_override_port != OVERRIDE_OFF)
		return EC_ERROR_BUSY;

	/* Set the override port if it's a sink. */
	if (port < 0 || pd_get_role(port) == PD_ROLE_SINK) {
		if (override_port != port) {
			override_port = port;
			if (charge_manager_is_seeded())
				hook_call_deferred(
					&charge_manager_refresh_data, 0);
		}
	}
	/*
	 * If the attached device is capable of being a sink, request a
	 * power swap and set the delayed override for swap completion.
	 */
	else if (pd_get_role(port) != PD_ROLE_SINK &&
		 dualrole_capability[port] == CAP_DUALROLE) {
		delayed_override_deadline.val = get_time().val +
						POWER_SWAP_TIMEOUT;
		delayed_override_port = port;
		hook_call_deferred(&charge_override_timeout_data,
				   POWER_SWAP_TIMEOUT);
		pd_request_power_swap(port);
	/* Can't charge from requested port -- return error. */
	} else
		retval = EC_ERROR_INVAL;

	return retval;
}

/**
 * Get the override port. OVERRIDE_OFF if no override port.
 * OVERRIDE_DONT_CHARGE if override is set for no port.
 *
 * @return override port
 */
int charge_manager_get_override(void)
{
	return override_port;
}

int charge_manager_get_active_charge_port(void)
{
	return charge_port;
}

/**
 * Return the charger current (mA) value.
 */
int charge_manager_get_charger_current(void)
{
	return (charge_current != CHARGE_CURRENT_UNINITIALIZED) ?
		charge_current : 0;
}

/**
 * Return the power limit (uW) set by charge manager.
 */
int charge_manager_get_power_limit_uw(void)
{
	int current_ma = charge_current;
	int voltage_mv = charge_voltage;

	if (current_ma == CHARGE_CURRENT_UNINITIALIZED ||
	    voltage_mv == CHARGE_VOLTAGE_UNINITIALIZED)
		return 0;
	else
		return current_ma * voltage_mv;
}

#ifdef CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT
void charge_manager_source_port(int port, int enable)
{
	uint32_t prev_bitmap = source_port_bitmap;
	int p;

	if (enable)
		atomic_or(&source_port_bitmap, 1 << port);
	else
		atomic_clear(&source_port_bitmap, 1 << port);

	/* No change, exit early. */
	if (prev_bitmap == source_port_bitmap)
		return;

	/* Set port limit according to policy */
	for (p = 0; p < CONFIG_USB_PD_PORT_COUNT; p++) {
		/*
		 * if we are the only active source port or there is none,
		 * advertise all the available power.
		 */
		int rp = (source_port_bitmap & ~(1 << p)) ? CONFIG_USB_PD_PULLUP
			: CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT;

		source_port_last_rp[p] = rp;

#ifdef CONFIG_USB_PD_LOGGING
		if (pd_is_connected(p) &&
		    pd_get_role(p) == PD_ROLE_SOURCE)
			charge_manager_save_log(p);
#endif

		tcpm_select_rp_value(p, rp);
		pd_update_contract(p);
	}
}

int charge_manager_get_source_pdo(const uint32_t **src_pdo)
{
	int p;
	int count = 0;

	/* count the number of connected sinks */
	for (p = 0; p < CONFIG_USB_PD_PORT_COUNT; p++)
		if (source_port_bitmap & (1 << p))
			count++;

	/* send the maximum current if we are sourcing only on one port */
	*src_pdo = count <= 1 ? pd_src_pdo_max : pd_src_pdo;

	return count <= 1 ? pd_src_pdo_cnt : pd_src_pdo_max_cnt;
}
#endif /* CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT */

#ifndef TEST_BUILD
static int hc_pd_power_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_power_info *p = args->params;
	struct ec_response_usb_pd_power_info *r = args->response;
	int port = p->port;

	/* If host is asking for the charging port, set port appropriately */
	if (port == PD_POWER_CHARGING_PORT)
		port = charge_port;

	charge_manager_fill_power_info(port, r);

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_POWER_INFO,
		     hc_pd_power_info,
		     EC_VER_MASK(0));
#endif /* TEST_BUILD */

static int hc_charge_port_override(struct host_cmd_handler_args *args)
{
	const struct ec_params_charge_port_override *p = args->params;
	const int16_t override_port = p->override_port;

	if (override_port < OVERRIDE_DONT_CHARGE ||
	    override_port >= CONFIG_USB_PD_PORT_COUNT)
		return EC_RES_INVALID_PARAM;

	return charge_manager_set_override(override_port) == EC_SUCCESS ?
		EC_RES_SUCCESS : EC_RES_ERROR;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_CHARGE_PORT_OVERRIDE,
		     hc_charge_port_override,
		     EC_VER_MASK(0));

static int command_charge_port_override(int argc, char **argv)
{
	int port = OVERRIDE_OFF;
	int ret = EC_SUCCESS;
	char *e;

	if (argc >= 2) {
		port = strtoi(argv[1], &e, 0);
		if (*e || port < OVERRIDE_DONT_CHARGE ||
		    port >= CONFIG_USB_PD_PORT_COUNT)
			return EC_ERROR_PARAM1;
		ret = charge_manager_set_override(port);
	}

	ccprintf("Override: %d\n", (argc >= 2 && ret == EC_SUCCESS) ?
					port : override_port);
	return ret;
}
DECLARE_CONSOLE_COMMAND(chgoverride, command_charge_port_override,
	"[port | -1 | -2]",
	"Force charging from a given port (-1 = off, -2 = disable charging)");

#ifdef CONFIG_CHARGE_MANAGER_EXTERNAL_POWER_LIMIT
static void charge_manager_set_external_power_limit(int current_lim,
						    int voltage_lim)
{
	int port;

	if (current_lim == EC_POWER_LIMIT_NONE)
		current_lim = CHARGE_CEIL_NONE;
	if (voltage_lim == EC_POWER_LIMIT_NONE)
		voltage_lim = PD_MAX_VOLTAGE_MV;

	for (port = 0; port < CONFIG_USB_PD_PORT_COUNT; ++port) {
		charge_manager_set_ceil(port, CEIL_REQUESTOR_HOST, current_lim);
		pd_set_external_voltage_limit(port, voltage_lim);
	}
}

/*
 * On transition out of S0, disable all external power limits, in case AP
 * failed to clear them.
 */
static void charge_manager_external_power_limit_off(void)
{
	charge_manager_set_external_power_limit(EC_POWER_LIMIT_NONE,
						EC_POWER_LIMIT_NONE);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, charge_manager_external_power_limit_off,
	     HOOK_PRIO_DEFAULT);

static int hc_external_power_limit(struct host_cmd_handler_args *args)
{
	const struct ec_params_external_power_limit_v1 *p = args->params;

	charge_manager_set_external_power_limit(p->current_lim,
						p->voltage_lim);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_EXTERNAL_POWER_LIMIT,
		     hc_external_power_limit,
		     EC_VER_MASK(1));

static int command_external_power_limit(int argc, char **argv)
{
	int max_current;
	int max_voltage;
	char *e;

	if (argc >= 2) {
		max_current = strtoi(argv[1], &e, 10);
		if (*e)
			return EC_ERROR_PARAM1;
	} else
		max_current = EC_POWER_LIMIT_NONE;

	if (argc >= 3) {
		max_voltage = strtoi(argv[2], &e, 10);
		if (*e)
			return EC_ERROR_PARAM1;
	} else
		max_voltage = EC_POWER_LIMIT_NONE;

	charge_manager_set_external_power_limit(max_current, max_voltage);
	ccprintf("max req: %dmA %dmV\n", max_current, max_voltage);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(chglim, command_external_power_limit,
	"[max_current (mA)] [max_voltage (mV)]",
	"Set max charger current / voltage");
#endif /* CONFIG_CHARGE_MANAGER_EXTERNAL_POWER_LIMIT */

#ifdef CONFIG_CMD_CHARGE_SUPPLIER_INFO
static int charge_supplier_info(int argc, char **argv)
{
	ccprintf("port=%d, type=%d, cur=%dmA, vtg=%dmV\n",
			charge_manager_get_active_charge_port(),
			charge_supplier,
			charge_current,
			charge_voltage);

	return 0;
}
DECLARE_CONSOLE_COMMAND(chgsup, charge_supplier_info,
			NULL, "print chg supplier info");
#endif
