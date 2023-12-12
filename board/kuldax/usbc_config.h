/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Brya board-specific USB-C configuration */

#ifndef __CROS_EC_USBC_CONFIG_H
#define __CROS_EC_USBC_CONFIG_H

#define CONFIG_USB_PD_PORT_MAX_COUNT 1

enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_COUNT };

void mb_update_usb4_tbt_config(void);

#endif /* __CROS_EC_USBC_CONFIG_H */
