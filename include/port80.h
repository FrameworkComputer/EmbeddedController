/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Port 80 module for Chrome EC */

#ifndef __CROS_EC_PORT80_H
#define __CROS_EC_PORT80_H

#include "common.h"

enum port_80_event {
	PORT_80_EVENT_RESUME = 0x1001,  /* S3->S0 transition */
};

/**
 * Store data from a LPC write to port 80, or a port_80_event code.
 *
 * @param data		Data written to port 80.
 */
void port_80_write(int data);

#endif  /* __CROS_EC_PORT80_H */
