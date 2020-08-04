/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Device Policy Manager implementation
 * Refer to USB PD 3.0 spec, version 2.0, sections 8.2 and 8.3
 */

#ifndef __CROS_EC_USB_DPM_H
#define __CROS_EC_USB_DPM_H

/*
 * Initializes DPM state for a port.
 *
 * @param port USB-C port number
 */
void dpm_init(int port);

/*
 * Informs the DPM that the mode entry sequence (including appropriate
 * configuration) is done for a port.
 *
 * @param port USB-C port number
 */
void dpm_set_mode_entry_done(int port);

/*
 * Informs the DPM that Exit Mode request is received
 *
 * @param port USB-C port number
 */
void dpm_set_mode_exit_request(int port);

/*
 * Informs the DPM that a VDM ACK was received.
 *
 * @param port      USB-C port number
 * @param type      Transmit type (SOP, SOP') for received ACK
 * @param vdo_count The number of VDOs in vdm; must be at least 1
 * @param vdm       The VDM payload of the ACK
 */
void dpm_vdm_acked(int port, enum tcpm_transmit_type type, int vdo_count,
		uint32_t *vdm);

/*
 * Informs the DPM that a VDM NAK was received. Also applies when a VDM request
 * received a Not Supported response or timed out waiting for a response.
 *
 * @param port    USB-C port number
 * @param type    Transmit type (SOP, SOP') for request
 * @param svid    The SVID of the request
 * @param vdm_cmd The VDM command of the request
 */
void dpm_vdm_naked(int port, enum tcpm_transmit_type type, uint16_t svid,
		uint8_t vdm_cmd);

/*
 * Drives the Policy Engine through entry/exit mode process
 *
 * @param port USB-C port number
 */
void dpm_run(int port);

#endif  /* __CROS_EC_USB_DPM_H */
