/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ACPI EC interface block. */

#ifndef __CROS_EC_ACPI_H
#define __CROS_EC_ACPI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Handle AP write to EC via the ACPI I/O port.
 *
 * @param is_cmd	Is write command (is_cmd=1) or data (is_cmd=0)
 * @param value         Value written to cmd or data register by AP
 * @param result        Value for AP to read from data port, if any
 * @return              True if *result was updated by this call
 */
int acpi_ap_to_ec(int is_cmd, uint8_t value, uint8_t *result);

enum acpi_dptf_profile_num {
	/*
	 * Value of 0 is reserved meaning the Device DPTF Profile Number in EC
	 * shared memory is invalid and host should fallback to using tablet
	 * mode switch to determine the DPTF table to load.
	 */
	DPTF_PROFILE_INVALID = 0,

#ifdef CONFIG_DPTF_MULTI_PROFILE
	/*
	 * Set default profile value to 2 under the assumption that profile
	 * value of 1 means default high-power mode and value of 2 corresponds
	 * to a low-power mode. This value is used by ACPI routines to report
	 * back to host until appropriate EC driver updates the current profile
	 * number.
	 */
	DPTF_PROFILE_DEFAULT = 2,
#else
	/* Default DPTF profile when multi-profile is not supported. */
	DPTF_PROFILE_DEFAULT = 1,
#endif

	/* Range of valid values for DPTF profile number. */
	DPTF_PROFILE_VALID_FIRST = 1,
	DPTF_PROFILE_VALID_LAST = 7,

	/* Standard convertible profiles */
	DPTF_PROFILE_CLAMSHELL = 1,
	DPTF_PROFILE_FLIPPED_360_MODE = 2,

	/* Standard detachable profiles */
	DPTF_PROFILE_BASE_ATTACHED = 1,
	DPTF_PROFILE_BASE_DETACHED = 2,
};

/**
 * Set current DPTF profile in EC shared memory.
 *
 * @param prof_num      Profile number of current profile. Valid values are 1-7.
 *                      See enum acpi_dptf_profile_num for standard profile
 *                      numbers for convertibles/detachables.
 * @return              EC_SUCCESS if operation was successful, EC_ERROR* in
 *                      case of error.
 */
int acpi_dptf_set_profile_num(int n);

/**
 * Get value of current DPTF profile.
 *
 * @return              DPTF Profile number currently set to be shared with the
 *                      host using EC shared memory.
 */
int acpi_dptf_get_profile_num(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ACPI_H */
