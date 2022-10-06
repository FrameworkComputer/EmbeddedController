/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Agah board-specific USB-C configuration */

#ifndef __CROS_EC_USBC_CONFIG_H
#define __CROS_EC_USBC_CONFIG_H

#define CONFIG_USB_PD_PORT_MAX_COUNT 2

enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_C2, USBC_PORT_COUNT };

struct ps8818_reg_val {
	uint8_t reg;
	uint8_t mask;
	uint16_t val;
};

#endif /* __CROS_EC_USBC_CONFIG_H */
