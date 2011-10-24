/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* System module for Chrome EC */

#ifndef __CROS_EC_SYSTEM_H
#define __CROS_EC_SYSTEM_H

#include "ec_common.h"

/* Reset causes */
typedef enum EcSystemResetCause {
  /* Unknown reset cause */
  EC_SYSTEM_RESET_UNKNOWN = 0,
  /* System reset cause is known, but not one of the causes listed below */
  EC_SYSTEM_RESET_OTHER,
  /* Brownout */
  EC_SYSTEM_RESET_BROWNOUT,
  /* Power-on reset */
  EC_SYSTEM_RESET_POWER_ON,
  /* Reset caused by asserting reset (RST#) pin */
  EC_SYSTEM_RESET_RESET_PIN,
  /* Software requested cold reset */
  EC_SYSTEM_RESET_SOFT_COLD,
  /* Software requested warm reset */
  EC_SYSTEM_RESET_SOFT_WARM,
  /* Watchdog timer reset */
  EC_SYSTEM_RESET_WATCHDOG,
} EcSystemResetCause;


/* Initializes the system module. */
EcError EcSystemInit(void);

/* Returns the cause of the last reset, or EC_SYSTEM_RESET_UNKNOWN if
 * the cause is not known. */
EcSystemResetCause EcSystemGetResetCause(void);

/* Resets the system.  If is_cold!=0, performs a cold reset (which
 * resets on-chip peripherals); else performs a warm reset (which does
 * not reset on-chip peripherals).  If successful, does not return.
 * Returns error if the reboot type cannot be requested (e.g. brownout
 * or reset pin). */
EcError EcSystemReset(int is_cold);

/* Sets a scratchpad register to the specified value.  The scratchpad
 * register must maintain its contents across a software-requested
 * warm reset. */
EcError EcSystemSetScratchpad(uint32_t value);

/* Stores the current scratchpad register value into <value_ptr>. */
EcError EcSystemGetScratchpad(uint32_t* value_ptr);

/* TODO: request sleep.  How do we want to handle transitioning
 * to/from low-power states? */

#endif  /* __CROS_EC_SYSTEM_H */
