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

#include "tcpm/tcpm.h"
#include "usb_pd_dpm.h"

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
 * Checks whether the mode entry sequence for DisplayPort alternate mode is done
 * for a port.
 *
 * @param port      USB-C port number
 * @return          True if entry sequence for DisplayPort mode is completed
 *                  False otherwise
 */
bool dp_entry_is_done(int port);

/*
 * Handles received DisplayPort VDM ACKs.
 *
 * @param port      USB-C port number
 * @param type      Transmit type (SOP, SOP') for received ACK
 * @param vdo_count The number of VDOs in the ACK VDM
 * @param vdm       VDM from ACK
 */
void dp_vdm_acked(int port, enum tcpci_msg_type type, int vdo_count,
		uint32_t *vdm);

/*
 * Handles NAKed (or Not Supported or timed out) DisplayPort VDM requests.
 *
 * @param port    USB-C port number
 * @param type    Transmit type (SOP, SOP') for request
 * @param svid    The SVID of the request
 * @param vdm_cmd The VDM command of the request
 */
void dp_vdm_naked(int port, enum tcpci_msg_type type, uint8_t vdm_cmd);

/*
 * Construct the next DisplayPort VDM that should be sent.
 *
 * @param[in] port	    USB-C port number
 * @param[in,out] vdo_count The number of VDOs in vdm; must be at least
 *			    VDO_MAX_SIZE.  On success, number of populated VDOs
 * @param[out] vdm          The VDM payload to be sent; output; must point to at
 *			    least VDO_MAX_SIZE elements
 * @return		    enum dpm_msg_setup_status
 */
enum dpm_msg_setup_status dp_setup_next_vdm(int port, int *vdo_count,
					    uint32_t *vdm);

#endif  /* __CROS_EC_USB_DP_ALT_MODE_H */
