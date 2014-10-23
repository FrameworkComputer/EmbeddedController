/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "console.h"
#include "hooks.h"
#include "usb_pd_config.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

/* Keep track of available charge for each charge port. */
static struct charge_port_info available_charge[CHARGE_SUPPLIER_COUNT]
					       [PD_PORT_COUNT];

/* Store current state of port enable / charge current. */
static int charge_port = CHARGE_PORT_NONE;
static int charge_current = CHARGE_CURRENT_UNINITIALIZED;

/**
 * Initialize available charge. Run before board init, so board init can
 * initialize data, if needed.
 */
static void charge_manager_init(void)
{
	int i, j;

	for (i = 0; i < CHARGE_SUPPLIER_COUNT; ++i)
		for (j = 0; j < PD_PORT_COUNT; ++j) {
			available_charge[i][j].current =
				CHARGE_CURRENT_UNINITIALIZED;
			available_charge[i][j].voltage =
				CHARGE_VOLTAGE_UNINITIALIZED;
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
	int new_charge_current, new_charge_voltage, i, j;

	/*
	 * Charge supplier selection logic:
	 * 1. Prefer higher priority supply.
	 * 2. Prefer higher power over lower in case priority is tied.
	 * available_charge can be changed at any time by other tasks,
	 * so make no assumptions about its consistency.
	 */
	for (i = 0; i < CHARGE_SUPPLIER_COUNT; ++i)
		for (j = 0; j < PD_PORT_COUNT; ++j)
			if (available_charge[i][j].current > 0 &&
			    available_charge[i][j].voltage > 0 &&
			    (new_supplier == CHARGE_SUPPLIER_NONE ||
			     supplier_priority[i] <
			     supplier_priority[new_supplier] ||
			    (supplier_priority[i] ==
			     supplier_priority[new_supplier] &&
			     POWER(available_charge[i][j]) >
			     POWER(available_charge[new_supplier]
						   [new_port])))) {
				new_supplier = i;
				new_port = j;
			}

	if (new_supplier == CHARGE_SUPPLIER_NONE)
		new_charge_current = new_charge_voltage = 0;
	else {
		new_charge_current =
			available_charge[new_supplier][new_port].current;
		new_charge_voltage =
			available_charge[new_supplier][new_port].voltage;
	}

	/* Change the charge limit + charge port if changed. */
	if (new_port != charge_port || new_charge_current != charge_current) {
		CPRINTS("New charge limit: supplier %d port %d current %d "
			"voltage %d", new_supplier, new_port,
			new_charge_current, new_charge_voltage);
		board_set_charge_limit(new_charge_current);
		board_set_active_charge_port(new_port);

		charge_current = new_charge_current;
		charge_port = new_port;
	}
}
DECLARE_DEFERRED(charge_manager_refresh);

/**
 * Update available charge for a given port / supplier.
 *
 * @param supplier		Charge supplier to update.
 * @param charge_port		Charge port to update.
 * @param charge		Charge port current / voltage.
 */
void charge_manager_update(int supplier,
			   int charge_port,
			   struct charge_port_info *charge)
{
	if (supplier < 0 || supplier >= CHARGE_SUPPLIER_COUNT) {
		CPRINTS("Invalid charge supplier: %d", supplier);
		return;
	}

	/* Update charge table if needed. */
	if (available_charge[supplier][charge_port].current !=
		charge->current ||
		available_charge[supplier][charge_port].voltage !=
		charge->voltage) {
		available_charge[supplier][charge_port].current =
			charge->current;
		available_charge[supplier][charge_port].voltage =
			charge->voltage;

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
