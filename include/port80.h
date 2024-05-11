/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Port 80 module for Chrome EC */

#ifndef __CROS_EC_PORT80_H
#define __CROS_EC_PORT80_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

enum port_80_event {
	PORT_80_EVENT_RESUME = 0x1001, /* S3->S0 transition */
	PORT_80_EVENT_RESET = 0x1002, /* RESET transition */
	PORT_80_IGNORE = 0xffff, /* Invalid POST CODE */
};

/**
 * Store data from a LPC write to port 80, or a port_80_event code.
 *
 * @param data		Data written to port 80.
 */
void port_80_write(int data);

/**
 * Chip specific function to read from port 80.
 *
 * @return data from the last LPC write to port 80,
 *	or PORT_80_IGNORE if no data is available.
 */
int port_80_read(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_PORT80_H */
