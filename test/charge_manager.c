/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test charge manager module.
 */

#include "battery.h"
#include "charge_manager.h"
#include "common.h"
#include "ec_commands.h"
#include "test_util.h"
#include "timer.h"
#include "usb_pd.h"
#include "util.h"

#define CHARGE_MANAGER_SLEEP_MS 50

/* Charge supplier priority: lower number indicates higher priority. */
const int supplier_priority[] = {
	[CHARGE_SUPPLIER_TEST1] = 0, [CHARGE_SUPPLIER_TEST2] = 1,
	[CHARGE_SUPPLIER_TEST3] = 1, [CHARGE_SUPPLIER_TEST4] = 1,
	[CHARGE_SUPPLIER_TEST5] = 3, [CHARGE_SUPPLIER_TEST6] = 3,
	[CHARGE_SUPPLIER_TEST7] = 5, [CHARGE_SUPPLIER_TEST8] = 6,
	[CHARGE_SUPPLIER_TEST9] = 6, [CHARGE_SUPPLIER_TEST10] = 7,
};
BUILD_ASSERT((int)CHARGE_SUPPLIER_COUNT == (int)CHARGE_SUPPLIER_TEST_COUNT);
BUILD_ASSERT(ARRAY_SIZE(supplier_priority) == CHARGE_SUPPLIER_COUNT);

static unsigned int active_charge_limit = CHARGE_SUPPLIER_NONE;
static unsigned int active_charge_port = CHARGE_PORT_NONE;
static unsigned int charge_port_to_reject = CHARGE_PORT_NONE;
static int new_power_request[CONFIG_USB_PD_PORT_MAX_COUNT];
static enum pd_power_role power_role[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Callback functions called by CM on state change */
void board_set_charge_limit(int port, int supplier, int charge_ma, int max_ma,
			    int charge_mv)
{
	active_charge_limit = charge_ma;
}

__override uint8_t board_get_usb_pd_port_count(void)
{
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}

/* Sets a charge port that will be rejected as the active port. */
static void set_charge_port_to_reject(int port)
{
	charge_port_to_reject = port;
}

int board_set_active_charge_port(int charge_port)
{
	if (charge_port != CHARGE_PORT_NONE &&
	    charge_port == charge_port_to_reject)
		return EC_ERROR_INVAL;

	active_charge_port = charge_port;
	return EC_SUCCESS;
}

void board_charge_manager_override_timeout(void)
{
}

void pd_set_new_power_request(int port)
{
	new_power_request[port] = 1;
}

static void clear_new_power_requests(void)
{
	int i;
	for (i = 0; i < board_get_usb_pd_port_count(); ++i)
		new_power_request[i] = 0;
}

static void pd_set_role(int port, int role)
{
	power_role[port] = role;
}

enum pd_power_role pd_get_power_role(int port)
{
	return power_role[port];
}

void pd_request_power_swap(int port)
{
	if (power_role[port] == PD_ROLE_SINK)
		power_role[port] = PD_ROLE_SOURCE;
	else
		power_role[port] = PD_ROLE_SINK;
}

static void wait_for_charge_manager_refresh(void)
{
	msleep(CHARGE_MANAGER_SLEEP_MS);
}

static void initialize_charge_table(int current, int voltage, int ceil)
{
	int i, j;
	struct charge_port_info charge;

	charge_manager_set_override(OVERRIDE_OFF);
	set_charge_port_to_reject(CHARGE_PORT_NONE);
	charge.current = current;
	charge.voltage = voltage;

	for (i = 0; i < board_get_usb_pd_port_count(); ++i) {
		for (j = 0; j < CEIL_REQUESTOR_COUNT; ++j)
			charge_manager_set_ceil(i, j, ceil);
		charge_manager_update_dualrole(i, CAP_DEDICATED);
		pd_set_role(i, PD_ROLE_SINK);
		for (j = 0; j < CHARGE_SUPPLIER_COUNT; ++j)
			charge_manager_update_charge(j, i, &charge);
	}
	wait_for_charge_manager_refresh();
}

static int test_initialization(void)
{
	int i, j;
	struct charge_port_info charge;

	/*
	 * No charge port should be selected until all ports + suppliers
	 * have reported in with an initial charge.
	 */
	TEST_ASSERT(active_charge_port == CHARGE_PORT_NONE);
	charge.current = 1000;
	charge.voltage = 5000;

	/* Initialize all supplier/port pairs, except for the last one */
	for (i = 0; i < CHARGE_SUPPLIER_COUNT; ++i)
		for (j = 0; j < board_get_usb_pd_port_count(); ++j) {
			if (i == 0)
				charge_manager_update_dualrole(j,
							       CAP_DEDICATED);
			if (i == CHARGE_SUPPLIER_COUNT - 1 &&
			    j == board_get_usb_pd_port_count() - 1)
				break;
			charge_manager_update_charge(i, j, &charge);
		}

	/* Verify no active charge port, since all pairs haven't updated */
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == CHARGE_PORT_NONE);

	/* Update last pair and verify a charge port has been selected */
	charge_manager_update_charge(CHARGE_SUPPLIER_COUNT - 1,
				     board_get_usb_pd_port_count() - 1,
				     &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port != CHARGE_PORT_NONE);

	return EC_SUCCESS;
}

static int test_safe_mode(void)
{
	int port = 0;
	struct charge_port_info charge;

	/* Initialize table to no charge */
	initialize_charge_table(0, 5000, 5000);

	/*
	 * Set a 2A non-dedicated charger on port 0 and verify that
	 * it is selected, due to safe mode.
	 */
	charge_manager_update_dualrole(port, CAP_DUALROLE);
	charge.current = 2000;
	charge.voltage = 5000;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, port, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == port);
	TEST_ASSERT(active_charge_limit == 2000);

	/* Verify ceil is ignored, due to safe mode. */
	charge_manager_set_ceil(port, 0, 500);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_limit == 2000);

	/*
	 * Leave safe mode and verify normal port selection rules go
	 * into effect.
	 */
	charge_manager_leave_safe_mode();
	wait_for_charge_manager_refresh();
#ifdef CONFIG_CHARGE_MANAGER_DRP_CHARGING
	TEST_ASSERT(active_charge_port == port);
	TEST_ASSERT(active_charge_limit == 500);
#else
	TEST_ASSERT(active_charge_port == CHARGE_PORT_NONE);
#endif

	/* For subsequent tests, safe mode is exited. */
	return EC_SUCCESS;
}

static int test_priority(void)
{
	struct charge_port_info charge;

	/* Initialize table to no charge */
	initialize_charge_table(0, 5000, 5000);
	TEST_ASSERT(active_charge_port == CHARGE_PORT_NONE);

	/*
	 * Set a 1A charge via a high-priority supplier and a 2A charge via
	 * a low-priority supplier, and verify the HP supplier is chosen.
	 */
	charge.current = 2000;
	charge.voltage = 5000;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST6, 0, &charge);
	charge.current = 1000;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 1, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 1000);

	/*
	 * Set a higher charge on a LP supplier and verify we still use the
	 * lower charge.
	 */
	charge.current = 1500;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST7, 1, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 1000);

	/*
	 * Zero our HP charge and verify fallback to next highest priority,
	 * which happens to be a different port.
	 */
	charge.current = 0;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 1, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 0);
	TEST_ASSERT(active_charge_limit == 2000);

	/* Add a charge at equal priority and verify highest charge selected */
	charge.current = 2500;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST5, 0, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 0);
	TEST_ASSERT(active_charge_limit == 2500);

	charge.current = 3000;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST6, 1, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 3000);

	/*
	 * Add a charge at equal priority and equal power, verify that the
	 * active port doesn't change since the first plugged port is
	 * selected as the tiebreaker.
	 */
	charge.current = 3000;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST6, 0, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 3000);

	return EC_SUCCESS;
}

static int test_charge_ceil(void)
{
	int port;
	struct charge_port_info charge;

	/* Initialize table to 1A @ 5V, and verify port + limit */
	initialize_charge_table(1000, 5000, 1000);
	TEST_ASSERT(active_charge_port != CHARGE_PORT_NONE);
	TEST_ASSERT(active_charge_limit == 1000);

	/* Set a 500mA ceiling, verify port is unchanged */
	port = active_charge_port;
	charge_manager_set_ceil(port, 0, 500);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(port == active_charge_port);
	TEST_ASSERT(active_charge_limit == 500);

	/* Raise the ceiling to 2A, verify limit goes back to 1A */
	charge_manager_set_ceil(port, 0, 2000);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(port == active_charge_port);
	TEST_ASSERT(active_charge_limit == 1000);

	/* Verify that ceiling is ignored in determining active charge port */
	charge.current = 2000;
	charge.voltage = 5000;
	charge_manager_update_charge(0, 0, &charge);
	charge.current = 2500;
	charge_manager_update_charge(0, 1, &charge);
	charge_manager_set_ceil(1, 0, 750);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 750);

	/* Set a secondary lower ceiling and verify it takes effect */
	charge_manager_set_ceil(1, 1, 500);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 500);

	/* Raise the secondary ceiling and verify the primary takes effect */
	charge_manager_set_ceil(1, 1, 800);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 750);

	/* Remove the primary celing and verify the secondary takes effect */
	charge_manager_set_ceil(1, 0, CHARGE_CEIL_NONE);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 800);

	/* Remove all ceilings */
	charge_manager_set_ceil(1, 1, CHARGE_CEIL_NONE);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 2500);

	/* Verify forced ceil takes effect immediately */
	charge_manager_force_ceil(1, 500);
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 500);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 500);

	return EC_SUCCESS;
}

static int test_new_power_request(void)
{
	struct charge_port_info charge;

	/* Initialize table to no charge */
	initialize_charge_table(0, 5000, 5000);
	/* Clear power requests, and verify they are zero'd */
	clear_new_power_requests();
	wait_for_charge_manager_refresh();
	TEST_ASSERT(new_power_request[0] == 0);
	TEST_ASSERT(new_power_request[1] == 0);

	/* Charge from port 1 and verify NPR on port 1 only */
	charge.current = 1000;
	charge.voltage = 5000;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 1, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(new_power_request[0] == 0);
	TEST_ASSERT(new_power_request[1] == 1);
	clear_new_power_requests();

	/* Reduce port 1 through ceil and verify no NPR */
	charge_manager_set_ceil(1, 0, 500);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(new_power_request[0] == 0);
	TEST_ASSERT(new_power_request[1] == 0);
	clear_new_power_requests();

	/* Change port 1 voltage and verify NPR on port 1 */
	charge.voltage = 4000;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 1, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(new_power_request[0] == 0);
	TEST_ASSERT(new_power_request[1] == 1);
	clear_new_power_requests();

	/* Add low-priority source and verify no NPRs */
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST6, 0, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(new_power_request[0] == 0);
	TEST_ASSERT(new_power_request[1] == 0);
	clear_new_power_requests();

	/*
	 * Add higher-priority source and verify NPR on both ports,
	 * since we're switching charge ports.
	 */
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST1, 0, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(new_power_request[0] == 1);
	TEST_ASSERT(new_power_request[1] == 1);
	clear_new_power_requests();

	return EC_SUCCESS;
}

static int test_override(void)
{
	struct charge_port_info charge;

	/* Initialize table to no charge */
	initialize_charge_table(0, 5000, 1000);

	/*
	 * Set a low-priority supplier on p0 and high-priority on p1, then
	 * verify that p1 is selected.
	 */
	charge.current = 500;
	charge.voltage = 5000;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 0, &charge);
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST1, 1, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 500);

	/* Set override to p0 and verify p0 is selected */
	charge_manager_set_override(0);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 0);

	/* Remove override and verify p1 is again selected */
	charge_manager_set_override(OVERRIDE_OFF);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);

	/*
	 * Set override again to p0, but set p0 charge to 0, and verify p1
	 * is again selected.
	 */
	charge.current = 0;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 0, &charge);
	charge_manager_set_override(0);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);

	/* Set non-zero charge on port 0 and verify override was auto-removed */
	charge.current = 250;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST5, 0, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);

	/*
	 * Verify current limit is still selected according to supplier
	 * priority on the override port.
	 */
	charge.current = 300;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 0, &charge);
	charge_manager_set_override(0);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 0);
	TEST_ASSERT(active_charge_limit == 300);
	charge.current = 100;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST1, 0, &charge);
	charge_manager_set_override(0);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 0);
	TEST_ASSERT(active_charge_limit == 100);

	/*
	 * Verify that a don't charge override request on a dual-role
	 * port causes a swap to source.
	 */
	pd_set_role(0, PD_ROLE_SINK);
	charge_manager_update_dualrole(0, CAP_DUALROLE);
	charge_manager_set_override(OVERRIDE_DONT_CHARGE);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(pd_get_power_role(0) == PD_ROLE_SOURCE);

	/*
	 * Verify that an override request to a dual-role source port
	 * causes a role swap to sink.
	 */
	charge_manager_set_override(0);
	wait_for_charge_manager_refresh();
	charge.current = 200;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST1, 0, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 0);
	TEST_ASSERT(active_charge_limit == 200);
	TEST_ASSERT(pd_get_power_role(0) == PD_ROLE_SINK);

	/* Set override to "don't charge", then verify we're not charging */
	charge_manager_set_override(OVERRIDE_DONT_CHARGE);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == CHARGE_PORT_NONE);
	TEST_ASSERT(active_charge_limit == 0);
	TEST_ASSERT(pd_get_power_role(0) == PD_ROLE_SOURCE);

	/* Update a charge supplier, verify that we still aren't charging */
	charge.current = 200;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST1, 0, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == CHARGE_PORT_NONE);
	TEST_ASSERT(active_charge_limit == 0);
	TEST_ASSERT(pd_get_power_role(0) == PD_ROLE_SOURCE);

	/* Turn override off, verify that we go back to the correct charge */
	charge_manager_set_override(OVERRIDE_OFF);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 500);
	TEST_ASSERT(pd_get_power_role(0) == PD_ROLE_SOURCE);

	return EC_SUCCESS;
}

static int test_dual_role(void)
{
	struct charge_port_info charge;

	/* Initialize table to no charge. */
	initialize_charge_table(0, 5000, 1000);

	/* Mark P0 as dual-role and set a charge. */
	charge_manager_update_dualrole(0, CAP_DUALROLE);
	charge.current = 500;
	charge.voltage = 5000;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 0, &charge);
	wait_for_charge_manager_refresh();
#ifdef CONFIG_CHARGE_MANAGER_DRP_CHARGING
	/* Verify we do charge from dual-role port */
	TEST_ASSERT(active_charge_port == 0);
	TEST_ASSERT(active_charge_limit == 500);
#else
	/* Verify we don't charge from dual-role port */
	TEST_ASSERT(active_charge_port == CHARGE_PORT_NONE);
	TEST_ASSERT(active_charge_limit == 0);
#endif

	/* Mark P0 as the override port, verify that we now charge. */
	charge_manager_set_override(0);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 0);
	TEST_ASSERT(active_charge_limit == 500);
	TEST_ASSERT(pd_get_power_role(0) == PD_ROLE_SINK);

	/* Remove override and verify we go back to previous state */
	charge_manager_set_override(OVERRIDE_OFF);
	wait_for_charge_manager_refresh();
#ifdef CONFIG_CHARGE_MANAGER_DRP_CHARGING
	TEST_ASSERT(active_charge_port == 0);
	TEST_ASSERT(active_charge_limit == 500);
#else
	TEST_ASSERT(active_charge_port == CHARGE_PORT_NONE);
	TEST_ASSERT(active_charge_limit == 0);
	TEST_ASSERT(pd_get_power_role(0) == PD_ROLE_SOURCE);
#endif

	/* Mark P0 as the override port, verify that we again charge. */
	charge_manager_set_override(0);
	charge.current = 550;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 0, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 0);
	TEST_ASSERT(active_charge_limit == 550);
	TEST_ASSERT(pd_get_power_role(0) == PD_ROLE_SINK);

	/*
	 * Insert a dual-role charger into P1 and set the override. Verify
	 * that the override correctly changes.
	 */
	charge_manager_update_dualrole(1, CAP_DUALROLE);
	charge_manager_set_override(1);
	charge.current = 500;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST6, 1, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 500);
	TEST_ASSERT(pd_get_power_role(1) == PD_ROLE_SINK);
	TEST_ASSERT(pd_get_power_role(0) == PD_ROLE_SOURCE);

	/* Set override back to P0 and verify switch */
	charge_manager_set_override(0);
	charge.current = 600;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 0, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 0);
	TEST_ASSERT(active_charge_limit == 600);
	TEST_ASSERT(pd_get_power_role(0) == PD_ROLE_SINK);
	TEST_ASSERT(pd_get_power_role(1) == PD_ROLE_SOURCE);

	/* Insert a dedicated charger and verify override is removed */
	charge.current = 0;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST6, 1, &charge);
	wait_for_charge_manager_refresh();
	charge_manager_update_dualrole(1, CAP_DEDICATED);
	charge.current = 400;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST6, 1, &charge);
	wait_for_charge_manager_refresh();
#ifdef CONFIG_CHARGE_MANAGER_DRP_CHARGING
	TEST_ASSERT(active_charge_port == 0);
	TEST_ASSERT(active_charge_limit == 600);
#else
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 400);
	TEST_ASSERT(pd_get_power_role(0) == PD_ROLE_SOURCE);
#endif

	/*
	 * Verify the port is handled normally if the dual-role source is
	 * unplugged and replaced with a dedicated source.
	 */
	charge_manager_update_dualrole(0, CAP_DEDICATED);
	charge.current = 0;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 0, &charge);
	charge.current = 500;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 0, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 0);
	TEST_ASSERT(active_charge_limit == 500);

	/*
	 * Test one port connected to dedicated charger and one connected
	 * to dual-role device.
	 */
	charge_manager_update_dualrole(0, CAP_DUALROLE);
	charge.current = 0;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 0, &charge);
	charge.current = 500;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 0, &charge);
	charge.current = 200;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST6, 1, &charge);
	wait_for_charge_manager_refresh();
#ifdef CONFIG_CHARGE_MANAGER_DRP_CHARGING
	/* Verify we charge from port with higher priority */
	TEST_ASSERT(active_charge_port == 0);
	TEST_ASSERT(active_charge_limit == 500);
#else
	/*
	 * Verify that we charge from the dedicated port if a dual-role
	 * source is also attached.
	 */
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 200);
	TEST_ASSERT(pd_get_power_role(0) == PD_ROLE_SOURCE);
#endif

	return EC_SUCCESS;
}

static int test_rejected_port(void)
{
	struct charge_port_info charge;

	/* Initialize table to no charge. */
	initialize_charge_table(0, 5000, 1000);
	TEST_ASSERT(active_charge_port == CHARGE_PORT_NONE);

	/* Set a charge on P0. */
	charge.current = 500;
	charge.voltage = 5000;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 0, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 0);
	TEST_ASSERT(active_charge_limit == 500);

	/* Set P0 as rejected, and verify that it doesn't become active. */
	set_charge_port_to_reject(1);
	charge.current = 1000;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST1, 1, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 0);
	TEST_ASSERT(active_charge_limit == 500);

	/* Don't reject P0, and verify it can become active. */
	set_charge_port_to_reject(CHARGE_PORT_NONE);
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST1, 1, &charge);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 1000);

	return EC_SUCCESS;
}

static int test_unknown_dualrole_capability(void)
{
	struct charge_port_info charge;

	/* Initialize table to no charge. */
	initialize_charge_table(0, 5000, 2000);
	TEST_ASSERT(active_charge_port == CHARGE_PORT_NONE);

	/* Set a charge on P0 with unknown dualrole capability, */
	charge.current = 500;
	charge.voltage = 5000;
	charge_manager_update_dualrole(0, CAP_UNKNOWN);
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 0, &charge);
	wait_for_charge_manager_refresh();
#ifdef CONFIG_CHARGE_MANAGER_DRP_CHARGING
	/* Verify we do charge from that port */
	TEST_ASSERT(active_charge_port == 0);
	TEST_ASSERT(active_charge_limit == 500);
#else
	/* Verify that we don't charge from the port. */
	TEST_ASSERT(active_charge_port == CHARGE_PORT_NONE);
#endif

	/* Toggle to dedicated and verify port becomes active. */
	charge_manager_update_dualrole(0, CAP_DEDICATED);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 0);

	/* Add dualrole charger in port 1 */
	charge.current = 1000;
	charge_manager_update_dualrole(1, CAP_DUALROLE);
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 1, &charge);
	wait_for_charge_manager_refresh();
#ifdef CONFIG_CHARGE_MANAGER_DRP_CHARGING
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 1000);
#else
	TEST_ASSERT(active_charge_port == 0);
#endif

	/* Remove charger on port 0 */
	charge.current = 0;
	charge_manager_update_dualrole(0, CAP_UNKNOWN);
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 0, &charge);
	wait_for_charge_manager_refresh();
#ifdef CONFIG_CHARGE_MANAGER_DRP_CHARGING
	TEST_ASSERT(active_charge_port == 1);
	TEST_ASSERT(active_charge_limit == 1000);
#else
	TEST_ASSERT(active_charge_port == CHARGE_PORT_NONE);
#endif

	/* Set override to charge on port 1 */
	charge_manager_set_override(1);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);

	/*
	 * Toggle port 0 to dedicated, verify that override is still kept
	 * because there's no charge on the port.
	 */
	charge_manager_update_dualrole(0, CAP_DEDICATED);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 1);

	/* Insert UNKNOWN capability charger on port 0 */
	charge_manager_update_dualrole(0, CAP_UNKNOWN);
	charge.current = 2000;
	charge_manager_update_charge(CHARGE_SUPPLIER_TEST2, 0, &charge);
	wait_for_charge_manager_refresh();
	wait_for_charge_manager_refresh();
	wait_for_charge_manager_refresh();
#ifdef CONFIG_CHARGE_MANAGER_DRP_CHARGING
	/* Verify override is removed */
	TEST_ASSERT(active_charge_port == 0);
	TEST_ASSERT(active_charge_limit == 2000);
#else
	/* Verify override is still kept */
	TEST_ASSERT(active_charge_port == 1);
#endif

	/* Toggle to dualrole */
	charge_manager_update_dualrole(0, CAP_DUALROLE);
	wait_for_charge_manager_refresh();
#ifdef CONFIG_CHARGE_MANAGER_DRP_CHARGING
	/* Verify no change */
	TEST_ASSERT(active_charge_port == 0);
#else
	/* Verify override is still kept */
	TEST_ASSERT(active_charge_port == 1);
#endif

	/* Toggle to dedicated */
	charge_manager_update_dualrole(0, CAP_UNKNOWN);
	wait_for_charge_manager_refresh();
#ifdef CONFIG_CHARGE_MANAGER_DRP_CHARGING
	/* Verify no change */
	TEST_ASSERT(active_charge_port == 0);
#else
	/* Verify override is still kept */
	TEST_ASSERT(active_charge_port == 1);
#endif
	charge_manager_update_dualrole(0, CAP_DEDICATED);
	wait_for_charge_manager_refresh();
	TEST_ASSERT(active_charge_port == 0);

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_initialization);
	RUN_TEST(test_safe_mode);
	RUN_TEST(test_priority);
	RUN_TEST(test_charge_ceil);
	RUN_TEST(test_new_power_request);
	RUN_TEST(test_override);
	RUN_TEST(test_dual_role);
	RUN_TEST(test_rejected_port);
	RUN_TEST(test_unknown_dualrole_capability);

	/* Some handlers are still running after the test ends. */
	sleep(2);

	test_print_result();
}
