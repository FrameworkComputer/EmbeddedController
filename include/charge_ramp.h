/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Charge input current limit ramp header for Chrome EC */

#ifndef __CROS_EC_CHARGE_RAMP_H
#define __CROS_EC_CHARGE_RAMP_H

#include "timer.h"

/* Charge ramp state used for checking VBUS */
enum chg_ramp_vbus_state {
	CHG_RAMP_VBUS_RAMPING,
	CHG_RAMP_VBUS_STABLE
};

/**
 * Check if ramping is allowed for given supplier
 *
 * @supplier Supplier to check
 *
 * @return Ramping is allowed for given supplier
 */
int board_is_ramp_allowed(int supplier);

/**
 * Get the maximum current limit that we are allowed to ramp to
 *
 * @supplier Active supplier type
 * @sup_curr Input current limit based on supplier
 *
 * @return Maximum current in mA
 */
int board_get_ramp_current_limit(int supplier, int sup_curr);

/**
 * Check if board is consuming full input current
 *
 * @return Board is consuming full input current
 */
int board_is_consuming_full_charge(void);

/**
 * Check if VBUS is too low
 *
 * @param ramp_state Current ramp state
 *
 * @return VBUS is sagging low
 */
int board_is_vbus_too_low(enum chg_ramp_vbus_state ramp_state);

/**
 * Get the input current limit set by ramp module
 *
 * Active input current limit (mA)
 */
int chg_ramp_get_current_limit(void);

/**
 * Return if charge ramping has reached stable state
 *
 * @return 1 if stable, 0 otherwise
 */
int chg_ramp_is_stable(void);

/**
 * Return if charge ramping has reached detected state
 *
 * @return 1 if detected, 0 otherwise
 */
int chg_ramp_is_detected(void);

#ifdef HAS_TASK_CHG_RAMP
/**
 * Notify charge ramp module of supplier type change on a port. If port
 * is CHARGE_PORT_NONE, the call indicates the last charge supplier went
 * away.
 *
 * @port Active charging port
 * @supplier Active charging supplier
 * @current Minimum input current limit
 * @registration_time Timestamp of when the supplier is registered
 */
void chg_ramp_charge_supplier_change(int port, int supplier, int current,
				     timestamp_t registration_time);

#else
static inline void chg_ramp_charge_supplier_change(
		int port, int supplier, timestamp_t registration_time) { }

/* Point directly to board function to set charge limit */
#define chg_ramp_set_min_current board_set_charge_limit
#endif

#endif /* __CROS_EC_CHARGE_RAMP_H */
