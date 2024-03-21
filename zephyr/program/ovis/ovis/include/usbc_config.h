/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_USBC_CONFIG_H
#define __ZEPHYR_USBC_CONFIG_H

#include "usb_mux.h"

/* USB-C ports */
enum usbc_port {
	USBC_PORT_C0 = 0,
	USBC_PORT_C1,
	USBC_PORT_C2,
	USBC_PORT_COUNT
};
BUILD_ASSERT(USBC_PORT_COUNT == CONFIG_USB_PD_PORT_MAX_COUNT);

void reset_nct38xx_port(int port);

#endif /* __ZEPHYR_USBC_CONFIG_H */
