/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * USB4 mode support
 * Refer USB Type-C Cable and Connector Specification Release 2.0 Section 5 and
 * USB Power Delivery Specification Revision 3.0, Version 2.0 Section 6.4.8
 */

#ifndef __CROS_EC_USB_MODE_H
#define __CROS_EC_USB_MODE_H

#include "tcpm/tcpm.h"
#include "usb_pd_tcpm.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize USB4 state for the specified port.
 *
 * @param port USB-C port number
 */
void enter_usb_init(int port);

/*
 * Checks whether the mode entry sequence for USB4 is done for a port.
 *
 * @param port      USB-C port number
 * @return          True if entry sequence for USB4 is completed
 *                  False otherwise
 */
bool enter_usb_entry_is_done(int port);

/*
 * Requests the retimer and mux to exit USB4 mode and re-initalizes the USB4
 * state machine.
 *
 * @param port USB-C port number
 */
void usb4_exit_mode_request(int port);

/*
 * Resets USB4 state and mux state.
 *
 * @param port USB-C port number
 */
void enter_usb_failed(int port);

/*
 * Returns True if port partner supports USB4 mode
 *
 * @param port    USB-C port number
 * @return        True if USB4 mode is supported by the port partner,
 *                False otherwise
 */
bool enter_usb_port_partner_is_capable(int port);

/*
 * Returns True if cable supports USB4 mode
 *
 * @param port    USB-C port number
 * @return        True if USB4 mode is supported by the cable,
 *                False otherwise
 */
bool enter_usb_cable_is_capable(int port);

/*
 * Handles accepted USB4 response
 *
 * @param port    USB-C port number
 * @param type    Transmit type (SOP, SOP', SOP'') for request
 */
void enter_usb_accepted(int port, enum tcpci_msg_type type);

/*
 * Handles rejected USB4 response
 *
 * @param port    USB-C port number
 * @param type    Transmit type (SOP, SOP', SOP'') for request
 */
void enter_usb_rejected(int port, enum tcpci_msg_type type);

/*
 * Constructs the next USB4 EUDO that should be sent.
 *
 * @param port    USB-C port number
 * @param type    Transmit type (SOP, SOP', SOP'') for request
 */
uint32_t enter_usb_setup_next_msg(int port, enum tcpci_msg_type *type);

#ifdef __cplusplus
}
#endif

#endif
