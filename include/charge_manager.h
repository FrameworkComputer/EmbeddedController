/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CHARGE_MANAGER_H
#define __CHARGE_MANAGER_H

/* Charge port that indicates no active port */
#define CHARGE_SUPPLIER_NONE -1
#define CHARGE_PORT_NONE -1
#define CHARGE_CEIL_NONE -1

/* Initial charge state */
#define CHARGE_CURRENT_UNINITIALIZED -1
#define CHARGE_VOLTAGE_UNINITIALIZED -1

/* Port override settings */
enum {
	OVERRIDE_DONT_CHARGE = -2,
	OVERRIDE_OFF = -1,
	/* [0, PD_PORT_COUNT): Port# */
};

#define POWER(charge_port) ((charge_port.current) * (charge_port.voltage))

/* Charge tasks report available current and voltage */
struct charge_port_info {
	int current;
	int voltage;
};

/* Called by charging tasks to update their available charge */
void charge_manager_update(int supplier,
			   int charge_port,
			   struct charge_port_info *charge);

/* Update charge ceiling for a given port */
void charge_manager_set_ceil(int port, int ceil);

/* Select an 'override port', which is always the preferred charge port */
void charge_manager_set_override(int port);

/* Returns the current active charge port, as determined by charge manager */
int charge_manager_get_active_charge_port(void);

#endif /* __CHARGE_MANAGER_H */
