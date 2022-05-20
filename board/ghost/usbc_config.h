/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Ghost board-specific USB-C configuration */

#ifndef __CROS_EC_USBC_CONFIG_H
#define __CROS_EC_USBC_CONFIG_H

#include "baseboard_usbc_config.h"

#ifndef CONFIG_ZEPHYR
#define CONFIG_USB_PD_PORT_MAX_COUNT	2
#endif

enum usbc_port {
	USBC_PORT_C0 = 0,
	USBC_PORT_C1,
	USBC_PORT_COUNT
};

#endif /* __CROS_EC_USBC_CONFIG_H */
