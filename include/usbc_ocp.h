/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USBC_OCP_H
#define __CROS_EC_USBC_OCP_H

/* Common APIs for USB Type-C Overcurrent Protection (OCP) Module */

/**
 * Increment the overcurrent event counter.
 *
 * @param port: The Type-C port that has overcurrented.
 * @return EC_SUCCESS on success, EC_ERROR_INVAL if non-existent port.
 */
int usbc_ocp_add_event(int port);

/**
 * Clear the overcurrent event counter
 *
 * @param port: The Type-C port number.
 * @return EC_SUCCESS on success, EC_ERROR_INVAL if non-existent port
 */
int usbc_ocp_clear_event_counter(int port);

/**
 * Is the port latched off due to multiple overcurrent events in succession?
 *
 * @param port: The Type-C port number.
 * @return 1 if the port is latched off, 0 if it is not latched off.
 */
int usbc_ocp_is_port_latched_off(int port);

/**
 * Register a port as having a sink connected
 *
 * @param port: The Type-C port number.
 * @param connected: true if sink is now connected on port
 */
void usbc_ocp_snk_is_connected(int port, bool connected);

/**
 * Board specific callback when a port overcurrents.
 *
 * @param port: The Type-C port which overcurrented.
 * @param is_overcurrented: 1 if port overcurrented, 0 if the condition is gone.
 */
__override_proto void board_overcurrent_event(int port, int is_overcurrented);

#endif /* !defined(__CROS_EC_USBC_OCP_H) */
