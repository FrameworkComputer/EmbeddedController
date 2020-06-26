/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * DisplayPort alternate mode support
 * Refer to VESA DisplayPort Alt Mode on USB Type-C Standard, version 2.0,
 * section 5.2
 */

#ifndef __CROS_EC_USB_DP_ALT_MODE_H
#define __CROS_EC_USB_DP_ALT_MODE_H

#include <stdint.h>

#include "tcpm.h"

/*
 * Initialize DP state for the specified port.
 *
 * @param port USB-C port number
 */
void dp_init(int port);

/*
 * Returns True if DisplayPort mode is in active state
 *
 * @param port      USB-C port number
 * @return          True if DisplayPort mode is in active state
 *                  False otherwise
 */
bool dp_is_active(int port);

/*
 * Handles received DisplayPort VDM ACKs.
 *
 * @param port      USB-C port number
 * @param type      Transmit type (SOP, SOP') for received ACK
 * @param vdo_count The number of VDOs in the ACK VDM
 * @param vdm       VDM from ACK
 */
void dp_vdm_acked(int port, enum tcpm_transmit_type type, int vdo_count,
		uint32_t *vdm);

/*
 * Handles NAKed (or Not Supported or timed out) DisplayPort VDM requests.
 *
 * @param port    USB-C port number
 * @param type    Transmit type (SOP, SOP') for request
 * @param svid    The SVID of the request
 * @param vdm_cmd The VDM command of the request
 */
void dp_vdm_naked(int port, enum tcpm_transmit_type type, uint8_t vdm_cmd);

/*
 * Reset the DisplayPort VDM state for the specified port, as when exiting
 * DisplayPort mode.
 *
 * @param port USB-C port number
 */
void dp_teardown(int port);

/*
 * Construct the next DisplayPort VDM that should be sent.
 *
 * @param port      USB-C port number
 * @param vdo_count The number of VDOs in vdm; must be at least VDO_MAX_SIZE
 * @param vdm       The VDM payload to be sent; output; must point to at least
 *                  VDO_MAX_SIZE elements
 * @return          The number of VDOs written to VDM or -1 to indicate error
 */
int dp_setup_next_vdm(int port, int vdo_count, uint32_t *vdm);

#endif  /* __CROS_EC_USB_DP_ALT_MODE_H */
