/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Renesas (Intersil) ISL-9237/38 battery charger public header
 */

#ifndef __CROS_EC_DRIVER_CHARGER_ISL923X_PUBLIC_H
#define __CROS_EC_DRIVER_CHARGER_ISL923X_PUBLIC_H

#include "common.h"
#include "stdbool.h"

#define ISL923X_ADDR_FLAGS (0x09)

extern const struct charger_drv isl923x_drv;

/**
 * Initialize AC & DC prochot threshold
 *
 * @param	chgnum: Index into charger chips
 * @param	AC Prochot threshold current in mA:
 *			multiple of 128 up to 6400 mA
 *			DC Prochot threshold current in mA:
 *			multiple of 128 up to 12800 mA
 *		Bits below 128mA are truncated (ignored).
 * @return enum ec_error_list
 */
int isl923x_set_ac_prochot(int chgnum, uint16_t ma);
int isl923x_set_dc_prochot(int chgnum, uint16_t ma);

/**
 * Set charger level 2 input current limit.
 *
 * @param chgnum: Index into charger chips
 * @param input_current_2: current limit value.
 */
int isl923x_set_level_2_input_current_limit(int chgnum, int input_current_2);

/**
 * Set the general comparator output polarity when asserted.
 *
 * @param chgnum: Index into charger chips
 * @param invert: Non-zero to invert polarity, zero to non-invert.
 * @return EC_SUCCESS, error otherwise.
 */
int isl923x_set_comparator_inversion(int chgnum, int invert);

/**
 * Return whether ACOK is high or low.
 *
 * @param chgnum index into chg_chips table.
 * @param acok will be set to true if ACOK is asserted, otherwise false.
 * @return EC_SUCCESS, error otherwise.
 */
enum ec_error_list raa489000_is_acok(int chgnum, bool *acok);

/**
 * Prepare the charger IC for battery ship mode.  Battery ship mode sets the
 * lowest power state for the IC. Battery ship mode can only be entered from
 * battery only mode.
 *
 * @param chgnum index into chg_chips table.
 */
void raa489000_hibernate(int chgnum, bool disable_adc);

/**
 * Enable or Disable the ASGATE in the READY state.
 *
 * @param chgnum: Index into charger chips
 * @param enable: whether to enable ASGATE
 */
int raa489000_enable_asgate(int chgnum, bool enable);

/**
 * Check whether the comparator output needs to
 * be inverted for the AC_PRESENT signal.
 */
void raa489000_check_ac_present(void);

enum ec_error_list isl9238c_hibernate(int chgnum);
enum ec_error_list isl9238c_resume(int chgnum);

#endif /* __CROS_EC_DRIVER_CHARGER_ISL923X_PUBLIC_H */
