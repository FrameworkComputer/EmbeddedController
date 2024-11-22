/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DIAGNOSTICS_H
#define __CROS_EC_DIAGNOSTICS_H

#ifndef CONFIG_PLATFORM_EC_FRAMEWORK_MINI_PC
#include "diagnostics_laptop.h"
#else
#include "diagnostics_minipc.h"
#endif

/*
 * If there is an error with this diagnostic, then set error=true
 * this is used as a bitmask to flash out any error codes
 */
void set_diagnostic(enum diagnostics_device_idx idx, bool error);

/*
 * Set it to true means ec has done the device detecting
 */
void set_device_complete(int done);

uint32_t get_hw_diagnostic(void);
uint8_t is_bios_complete(void);
uint8_t is_device_complete(void);

void set_bios_diagnostic(uint8_t code);

void reset_diagnostics(void);

void cancel_diagnostics(void);

void project_diagnostics(void);

extern uint8_t run_diagnostics;
extern uint32_t hw_diagnostics;

#endif	/* __CROS_EC_DIAGNOSTICS_H */
