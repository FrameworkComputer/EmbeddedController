/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Case Closed Debug interface
 */
#ifndef INCLUDE_CASE_CLOSED_DEBUG_H
#define INCLUDE_CASE_CLOSED_DEBUG_H

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
};

/*
 * Set current CCD mode, this function is idempotent.
 */
void ccd_set_mode(enum ccd_mode new_mode);

/*
 * Board provided function that should ensure that the debug USB port is ready
 * for use by the case closed debug code.  This could mean updating a MUX or
 * switch to disconnect USB from the AP.
 */
void ccd_board_connect(void);

/*
 * Board provided function that releases the debug USB port, giving it back
 * to the AP.
 */
void ccd_board_disconnect(void);

#endif /* INCLUDE_CASE_CLOSED_DEBUG_H */
