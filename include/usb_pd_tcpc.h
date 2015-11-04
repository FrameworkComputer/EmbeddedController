/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port controller */

#ifndef __CROS_EC_USB_PD_TCPC_H
#define __CROS_EC_USB_PD_TCPC_H

/* If we are a TCPC but do not a TCPM, then we implement the slave TCPCI */
#if defined(CONFIG_USB_PD_TCPC) && !defined(CONFIG_USB_PD_TCPM_STUB)
#define TCPCI_I2C_SLAVE
#endif

#ifdef TCPCI_I2C_SLAVE
/* Convert TCPC address to type-C port number */
#define TCPC_ADDR_TO_PORT(addr) (((addr) - CONFIG_TCPC_I2C_BASE_ADDR) >> 1)
/* Check if the i2c address belongs to TCPC */
#define ADDR_IS_TCPC(addr)      (((addr) & 0xfc) == CONFIG_TCPC_I2C_BASE_ADDR)
#endif

/**
 * Process incoming TCPCI I2C command
 *
 * @param read This is a read request. If 0, this is a write request.
 * @param len Length of incoming payload
 * @param payload Pointer to incoming and outgoing data
 * @param send_response Function to call to send response if necessary
 */
void tcpc_i2c_process(int read, int port, int len, uint8_t *payload,
		      void (*send_response)(int));

/**
 * Handle VBUS wake interrupts
 *
 * @param signal The VBUS wake interrupt signal
 */
void pd_vbus_evt_p0(enum gpio_signal signal);
void pd_vbus_evt_p1(enum gpio_signal signal);

#endif /* __CROS_EC_USB_PD_TCPC_H */
