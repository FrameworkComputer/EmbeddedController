/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DIAGNOSTICS_H
#define __CROS_EC_DIAGNOSTICS_H

enum diagnostics_device_idx {
	DIAGNOSTICS_START = 0,
	DIAGNOSTICS_HW_NO_BATTERY,
	DIAGNOSTICS_HW_PGOOD_3V5V,
	DIAGNOSTICS_VCCIN_AUX_VR,
	DIAGNOSTICS_SLP_S4,
	DIAGNOSTICS_HW_PGOOD_VR,

	DIAGNOSTICS_TOUCHPAD,
	DIAGNOSTICS_AUDIO_DAUGHTERBOARD,
	DIAGNOSTICS_THERMAL_SENSOR,
	DIAGNOSTICS_NOFAN,

	DIAGNOSTICS_NO_S0,
	DIAGNOSTICS_NO_DDR,
	DIAGNOSTICS_NO_EDP,
	DIAGNOSTICS_HW_FINISH,

	/*BIOS BITS*/
	DIAGNOSTICS_BIOS_BIT0,
	DIAGNOSTICS_BIOS_BIT1,
	DIAGNOSTICS_BIOS_BIT2,
	DIAGNOSTICS_BIOS_BIT3,
	DIAGNOSTICS_BIOS_BIT4,
	DIAGNOSTICS_BIOS_BIT5,
	DIAGNOSTICS_BIOS_BIT6,
	DIAGNOSTICS_BIOS_BIT7,
	DIAGNOSTICS_MAX
};

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

bool diagnostics_tick(void);

void set_standalone_mode(int enable);

int get_standalone_mode(void);

#endif	/* __CROS_EC_DIAGNOSTICS_H */
