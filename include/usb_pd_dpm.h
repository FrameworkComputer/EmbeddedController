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

#include "ec_commands.h"
#include "usb_pd_tcpm.h"

/*
 * Initializes DPM state for a port.
 *
 * @param port USB-C port number
 */
void dpm_init(int port);

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
void dpm_vdm_acked(int port, enum tcpci_msg_type type, int vdo_count,
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
void dpm_vdm_naked(int port, enum tcpci_msg_type type, uint16_t svid,
		uint8_t vdm_cmd);

/*
 * Drives the Policy Engine through entry/exit mode process
 *
 * @param port USB-C port number
 */
void dpm_run(int port);

/*
 * Determines the current allocation for the connection, past the basic
 * CONFIG_USB_PD_PULLUP value set by the TC (generally 1.5 A)
 *
 * @param port		USB-C port number
 * @param vsafe5v_pdo	Copy of first Sink_Capability PDO, which should
 *			represent the vSafe5V fixed PDO
 */
void dpm_evaluate_sink_fixed_pdo(int port, uint32_t vsafe5v_pdo);

/*
 * Registers port as a non-PD sink, so that can be taken into account when
 * allocating current.
 *
 * @param port		USB-C port number
 */
void dpm_add_non_pd_sink(int port);

/*
 * Remove this port as a sink, and reallocate maximum current as needed.
 *
 * @param port		USB-C port number
 */
void dpm_remove_sink(int port);

/*
 * Remove this port as a source, and reallocate reserved FRS maximum current
 * as needed.
 *
 * @param port		USB-C port number
 */
void dpm_remove_source(int port);

/*
 * Return the appropriate Source Capability PDO to offer this port
 *
 * @param src_pdo	Will point to appropriate PDO to offer
 * @param port		USB-C port number
 * @return		Number of PDOs
 */
int dpm_get_source_pdo(const uint32_t **src_pdo, const int port);

/*
 * Report offered source current for this port
 *
 * @param port		USB-C port number
 * @return		Current offered, in mA
 */
int dpm_get_source_current(const int port);

#endif  /* __CROS_EC_USB_DPM_H */
