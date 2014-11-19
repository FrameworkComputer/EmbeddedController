/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

/* Keep track of available charge for each charge port. */
static struct charge_port_info available_charge[CHARGE_SUPPLIER_COUNT]
					       [PD_PORT_COUNT];

/*
 * Charge ceiling for ports. This can be set to temporarily limit the charge
 * pulled from a port, without influencing the port selection logic.
 */
static int charge_ceil[PD_PORT_COUNT];

/* Store current state of port enable / charge current. */
static int charge_port = CHARGE_PORT_NONE;
static int charge_current = CHARGE_CURRENT_UNINITIALIZED;
static int charge_supplier = CHARGE_SUPPLIER_NONE;
static int override_port = OVERRIDE_OFF;

/**
 * Initialize available charge. Run before board init, so board init can
 * initialize data, if needed.
 */
static void charge_manager_init(void)
{
	int i, j;

	for (i = 0; i < PD_PORT_COUNT; ++i) {
		for (j = 0; j < CHARGE_SUPPLIER_COUNT; ++j) {
			available_charge[j][i].current =
				CHARGE_CURRENT_UNINITIALIZED;
			available_charge[j][i].voltage =
				CHARGE_VOLTAGE_UNINITIALIZED;
		}
		charge_ceil[i] = CHARGE_CEIL_NONE;
	}
}
DECLARE_HOOK(HOOK_INIT, charge_manager_init, HOOK_PRIO_DEFAULT-1);

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
		for (j = 0; j < PD_PORT_COUNT; ++j)
			if (available_charge[i][j].current ==
			    CHARGE_CURRENT_UNINITIALIZED ||
			    available_charge[i][j].voltage ==
			    CHARGE_VOLTAGE_UNINITIALIZED)
				return 0;

	is_seeded = 1;
	return 1;
}

/**
 * Charge manager refresh -- responsible for selecting the active charge port
 * and charge power. Called as a deferred task.
 */
static void charge_manager_refresh(void)
{
	int new_supplier = CHARGE_SUPPLIER_NONE;
	int new_port = CHARGE_PORT_NONE;
	int new_charge_current, new_charge_voltage, i, j, old_port;

	/* Skip port selection on OVERRIDE_DONT_CHARGE. */
	if (override_port != OVERRIDE_DONT_CHARGE) {
		/*
		 * Charge supplier selection logic:
		 * 1. Prefer higher priority supply.
		 * 2. Prefer higher power over lower in case priority is tied.
		 * available_charge can be changed at any time by other tasks,
		 * so make no assumptions about its consistency.
		 */
		for (i = 0; i < CHARGE_SUPPLIER_COUNT; ++i)
			for (j = 0; j < PD_PORT_COUNT; ++j) {
				/*
				 * Don't select this port if we have a
				 * charge on another override port.
				 */
				if (override_port != OVERRIDE_OFF &&
				    override_port == new_port &&
				    override_port != j)
					continue;

				/*
				 * Don't charge from a dual-role port unless
				 * it is our override port.
				 */
				if (pd_get_partner_dualrole_capable(j) &&
				    override_port != j)
					continue;

				if (available_charge[i][j].current > 0 &&
				    available_charge[i][j].voltage > 0 &&
				   (new_supplier == CHARGE_SUPPLIER_NONE ||
				    supplier_priority[i] <
				    supplier_priority[new_supplier] ||
				   (j == override_port &&
				    new_port != override_port) ||
				   (supplier_priority[i] ==
				    supplier_priority[new_supplier] &&
				    POWER(available_charge[i][j]) >
				    POWER(available_charge[new_supplier]
							  [new_port])))) {
					new_supplier = i;
					new_port = j;
				}
			}

		/* Clear override if no charge is available on override port */
		if (override_port != OVERRIDE_OFF &&
		    override_port != new_port)
			override_port = OVERRIDE_OFF;
	}

	if (new_supplier == CHARGE_SUPPLIER_NONE)
		new_charge_current = new_charge_voltage = 0;
	else {
		new_charge_current =
			available_charge[new_supplier][new_port].current;
		/* Enforce port charge ceiling. */
		if (charge_ceil[new_port] != CHARGE_CEIL_NONE &&
		    charge_ceil[new_port] < new_charge_current)
			new_charge_current = charge_ceil[new_port];

		new_charge_voltage =
			available_charge[new_supplier][new_port].voltage;
	}

	/* Change the charge limit + charge port if modified. */
	if (new_port != charge_port || new_charge_current != charge_current) {
		CPRINTS("New charge limit: supplier %d port %d current %d "
			"voltage %d", new_supplier, new_port,
			new_charge_current, new_charge_voltage);
		board_set_charge_limit(new_charge_current);
		board_set_active_charge_port(new_port);

		charge_current = new_charge_current;
		charge_supplier = new_supplier;
		old_port = charge_port;
		charge_port = new_port;

		if (new_port != CHARGE_PORT_NONE)
			pd_set_new_power_request(new_port);
		if (old_port != CHARGE_PORT_NONE)
			pd_set_new_power_request(old_port);
	}
}
DECLARE_DEFERRED(charge_manager_refresh);

/**
 * Update available charge for a given port / supplier.
 *
 * @param supplier		Charge supplier to update.
 * @param port			Charge port to update.
 * @param charge		Charge port current / voltage.
 */
void charge_manager_update(int supplier,
			   int port,
			   struct charge_port_info *charge)
{
	ASSERT(supplier >= 0 && supplier < CHARGE_SUPPLIER_COUNT);
	ASSERT(port >= 0 && port < PD_PORT_COUNT);

	/* Update charge table if needed. */
	if (available_charge[supplier][port].current != charge->current ||
		available_charge[supplier][port].voltage != charge->voltage) {
		/* Remove override when a dedicated charger is plugged */
		if (override_port != OVERRIDE_OFF &&
		    available_charge[supplier][port].current == 0 &&
		    charge->current > 0 &&
		    !pd_get_partner_dualrole_capable(port))
			override_port = OVERRIDE_OFF;


		available_charge[supplier][port].current = charge->current;
		available_charge[supplier][port].voltage = charge->voltage;

		/*
		 * Don't call charge_manager_refresh unless all ports +
		 * suppliers have reported in. We don't want to make changes
		 * to our charge port until we are certain we know what is
		 * attached.
		 */
		if (charge_manager_is_seeded())
			hook_call_deferred(charge_manager_refresh, 0);
	}
}

/**
 * Update charge ceiling for a given port.
 *
 * @param port			Charge port to update.
 * @param ceil			Charge ceiling (mA).
 */
void charge_manager_set_ceil(int port, int ceil)
{
	ASSERT(port >= 0 && port < PD_PORT_COUNT);

	if (charge_ceil[port] != ceil) {
		charge_ceil[port] = ceil;
		if (port == charge_port && charge_manager_is_seeded())
				hook_call_deferred(charge_manager_refresh, 0);
	}
}

/**
 * Select an 'override port', a port which is always the preferred charge port.
 *
 * @param port			Charge port to select as override, or
 *				OVERRIDE_OFF to select no override port,
 *				or OVERRIDE_DONT_CHARGE to specifc that no
 *				charge port should be selected.
 */
void charge_manager_set_override(int port)
{
	ASSERT(port >= OVERRIDE_DONT_CHARGE && port < PD_PORT_COUNT);

	if (override_port != port) {
		override_port = port;
		if (charge_manager_is_seeded())
			hook_call_deferred(charge_manager_refresh, 0);
	}
}

int charge_manager_get_active_charge_port(void)
{
	return charge_port;
}

#ifndef TEST_CHARGE_MANAGER
static int hc_pd_power_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_power_info *p = args->params;
	struct ec_response_usb_pd_power_info *r = args->response;
	int port = p->port;
	int sup = CHARGE_SUPPLIER_NONE;
	int i;

	/* If host is asking for the charging port, set port appropriately */
	if (port == PD_POWER_CHARGING_PORT)
		port = charge_port;

	/* Determine supplier information to show */
	if (port == charge_port) {
		sup = charge_supplier;
	} else {
		/* Find highest priority supplier */
		for (i = 0; i < CHARGE_SUPPLIER_COUNT; ++i) {
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
		}
	}

	/* Fill in power role */
	if (charge_port == port)
		r->role = USB_PD_PORT_POWER_SINK;
	else if (sup != CHARGE_SUPPLIER_NONE)
		r->role = USB_PD_PORT_POWER_SINK_NOT_CHARGING;
	else if (pd_is_connected(port) && pd_get_role(port) == PD_ROLE_SOURCE)
		r->role = USB_PD_PORT_POWER_SOURCE;
	else
		r->role = USB_PD_PORT_POWER_DISCONNECTED;

	/* Is port partner dual-role capable */
	r->dualrole = pd_get_partner_dualrole_capable(port);

	if (sup == CHARGE_SUPPLIER_NONE) {
		r->type = USB_CHG_TYPE_NONE;
		r->voltage_max = 0;
		r->voltage_now = 0;
		r->current_max = 0;
		r->max_power = 0;
	} else {
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
		default:
			r->type = USB_CHG_TYPE_OTHER;
		}
		r->voltage_max = available_charge[sup][port].voltage;
		r->current_max = available_charge[sup][port].current;
		r->max_power = POWER(available_charge[sup][port]);

		/*
		 * If we are sourcing power, or sinking but not charging, then
		 * VBUS must be 5V. If we are charging, then read VBUS ADC.
		 */
		if (r->role == USB_PD_PORT_POWER_SOURCE ||
		    r->role == USB_PD_PORT_POWER_SINK_NOT_CHARGING)
			r->voltage_now = 5000;
		else
			r->voltage_now = adc_read_channel(ADC_BOOSTIN);
	}

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_POWER_INFO,
		     hc_pd_power_info,
		     EC_VER_MASK(0));
#endif /* TEST_CHARGE_MANAGER */

static int hc_charge_port_override(struct host_cmd_handler_args *args)
{
	const struct ec_params_charge_port_override *p = args->params;
	const int16_t override_port = p->override_port;

	if (override_port < OVERRIDE_DONT_CHARGE ||
	    override_port >= PD_PORT_COUNT)
		return EC_RES_INVALID_PARAM;

	if (override_port >= 0 && pd_get_role(override_port) != PD_ROLE_SINK)
		/*
		 * TODO(crosbug.com/p/31195): Switch dual-role ports
		 * from source to sink.
		 */
		return EC_RES_ERROR;

	charge_manager_set_override(override_port);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_CHARGE_PORT_OVERRIDE,
		     hc_charge_port_override,
		     EC_VER_MASK(0));

static int command_charge_port_override(int argc, char **argv)
{
	int port = OVERRIDE_OFF;
	char *e;

	if (argc >= 2) {
		port = strtoi(argv[1], &e, 0);
		if (*e || port < OVERRIDE_DONT_CHARGE || port >= PD_PORT_COUNT)
			return EC_ERROR_PARAM1;
	}

	if (port >= 0 && pd_get_role(override_port) != PD_ROLE_SINK)
		/*
		 * TODO(crosbug.com/p/31195): Switch dual-role ports
		 * from source to sink.
		 */
		return EC_ERROR_PARAM1;

	charge_manager_set_override(port);
	ccprintf("Set override: %d\n", port);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(chgoverride, command_charge_port_override,
	"[port | -1 | -2]",
	"Force charging from a given port (-1 = off, -2 = disable charging)",
	NULL);
