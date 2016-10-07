/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Case Closed Debug interface
 */
#ifndef __CROS_EC_CASE_CLOSED_DEBUG_H
#define __CROS_EC_CASE_CLOSED_DEBUG_H

enum ccd_mode {
	/*
	 * The disabled mode tri-states the DP and DN lines.
	 */
	CCD_MODE_DISABLED,

	/*
	 * The partial mode allows some CCD functionality and is to be set
	 * when the device is write protected and a CCD cable is detected.
	 * This mode gives access to the APs console.
	 */
	CCD_MODE_PARTIAL,

	/*
	 * The fully enabled mode is used in factory and test lab
	 * configurations where it is acceptable to be able to reflash the
	 * device over CCD.
	 */
	CCD_MODE_ENABLED,

	CCD_MODE_COUNT,
};

/*
 * Set current CCD mode, this function is idempotent.
 */
void ccd_set_mode(enum ccd_mode new_mode);

/* Initialize the PHY based on CCD state */
void ccd_phy_init(int enable_ccd);

/*
 * Get current CCD mode.
 */
enum ccd_mode ccd_get_mode(void);
#endif /* __CROS_EC_CASE_CLOSED_DEBUG_H */
