/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Xol board-specific USB-C configuration */

#ifndef __CROS_EC_USBC_CONFIG_H
#define __CROS_EC_USBC_CONFIG_H

#ifndef CONFIG_ZEPHYR
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#endif

enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_C2, USBC_PORT_COUNT };

#endif /* __CROS_EC_USBC_CONFIG_H */
