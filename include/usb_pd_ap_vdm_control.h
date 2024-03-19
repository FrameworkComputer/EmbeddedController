/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * AP VDM Control functions
 * Note: Stubs of APIs are implemented for linking if feature is not enabled
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 16

#ifndef __CROS_EC_USB_AP_VDM_CONTROL_H
#define __CROS_EC_USB_AP_VDM_CONTROL_H

#include "ec_commands.h"
#include "usb_pd_tcpm.h"

#ifdef CONFIG_USB_PD_VDM_AP_CONTROL
/*
 * Informs the AP VDM module of a received Attention message.  Note: all
 * Attention messages are assumed to be SOP since cables are disallowed from
 * sending this type of VDM.
 *
 * @param[in] port		USB-C port number
 * @param[in] length		Number of objects filled in
 * @param[in] buf		Buffer containing received VDM (header and VDO)
 */
void ap_vdm_attention_enqueue(int port, int length, uint32_t *buf);

/*
 * Initializes AP VDM state for a port.
 *
 * @param port USB-C port number
 */
void ap_vdm_init(int port);

/*
 * Informs the AP VDM module that a VDM ACK was received.
 *
 * @param port      USB-C port number
 * @param type      Transmit type (SOP, SOP') for received ACK
 * @param vdo_count The number of VDOs in VDM; must be at least 1
 * @param vdm       The VDM payload of the ACK
 */
void ap_vdm_acked(int port, enum tcpci_msg_type type, int vdo_count,
		  uint32_t *vdm);

/*
 * Informs the AP VDM module that a VDM NAK was received. Also applies when a
 * VDM request received a Not Supported response or timed out waiting for a
 * response.
 *
 * @param port		USB-C port number
 * @param type		Transmit type (SOP, SOP') for request
 * @param svid		The SVID of the request
 * @param vdm_cmd	The VDM command of the request
 * @param vdm_header    VDM header reply (0 if a NAK wasn't actually received)
 */
void ap_vdm_naked(int port, enum tcpci_msg_type type, uint16_t svid,
		  uint8_t vdm_cmd, uint32_t vdm_header);

#else /* else for CONFIG_USB_PD_VDM_AP_CONTROL */
static inline void ap_vdm_attention_enqueue(int port, int length, uint32_t *buf)
{
}

static inline void ap_vdm_init(int port)
{
}

static inline void ap_vdm_acked(int port, enum tcpci_msg_type type,
				int vdo_count, uint32_t *vdm)
{
}

static inline void ap_vdm_naked(int port, enum tcpci_msg_type type,
				uint16_t svid, uint8_t vdm_cmd,
				uint32_t vdm_header)
{
}

#endif /* CONFIG_USB_PD_VDM_AP_CONTROL */
#endif /* __CROS_EC_USB_AP_VDM_H */
