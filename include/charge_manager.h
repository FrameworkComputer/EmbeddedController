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

/* Charge tasks report available current and voltage */
struct charge_port_info {
	int current;
	int voltage;
};

/* Called by charging tasks to update their available charge */
void charge_manager_update_charge(int supplier,
				  int port,
				  struct charge_port_info *charge);

/* Partner port dualrole capabilities */
enum dualrole_capabilities {
	CAP_UNKNOWN,
	CAP_DUALROLE,
	CAP_DEDICATED,
};

/* Called by charging tasks to indicate partner dualrole capability change */
void charge_manager_update_dualrole(int port, enum dualrole_capabilities cap);

/* Update charge ceiling for a given port */
void charge_manager_set_ceil(int port, int ceil);

/* Select an 'override port', which is always the preferred charge port */
int charge_manager_set_override(int port);
int charge_manager_get_override(void);

/* Returns the current active charge port, as determined by charge manager */
int charge_manager_get_active_charge_port(void);

#ifdef CONFIG_USB_PD_LOGGING
/* Save power state log entry for the given port */
void charge_manager_save_log(int port);
#endif

/* Board-level callback functions */

/*
 * Set the active charge port. Returns EC_SUCCESS if the charge port is
 * accepted, returns ec_error_list status otherwise.
 */
int board_set_active_charge_port(int charge_port);

/* Set the charge current limit. */
void board_set_charge_limit(int charge_ma);

/* Called on delayed override timeout */
void board_charge_manager_override_timeout(void);

#endif /* __CHARGE_MANAGER_H */
