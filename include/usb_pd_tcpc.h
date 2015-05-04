/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port controller */

#ifndef __USB_PD_TCPC_H
#define __USB_PD_TCPC_H

#ifndef CONFIG_TCPC_I2C_BASE_ADDR
#define CONFIG_TCPC_I2C_BASE_ADDR 0x9c
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

#endif /* __USB_PD_TCPC_H */
