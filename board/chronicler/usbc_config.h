/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* volteer board-specific USB-C configuration */

#ifndef __CROS_EC_USBC_CONFIG_H
#define __CROS_EC_USBC_CONFIG_H

enum usbc_port {
	USBC_PORT_C0 = 0,
	USBC_PORT_C1,
	USBC_PORT_COUNT
};

/* Configure the USB3 daughterboard type */
void config_usb3_db_type(void);

#endif /* __CROS_EC_USBC_CONFIG_H */
