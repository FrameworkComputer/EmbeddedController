/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mithrax board-specific USB-C configuration */

#ifndef __CROS_EC_USBC_CONFIG_H
#define __CROS_EC_USBC_CONFIG_H

#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USB_PD_TCPC_RUNTIME_CONFIG
#define CONFIG_USB_MUX_RUNTIME_CONFIG

enum usbc_port { USBC_PORT_C2 = 0, USBC_PORT_C1, USBC_PORT_COUNT };

void config_usb_db_type(void);

#endif /* __CROS_EC_USBC_CONFIG_H */
