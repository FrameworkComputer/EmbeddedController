/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

#include "cbi_ec_fw_config.h"
#include "power_button.h"
#include "power.h"
#include "switch.h"
#include "throttle_ap.h"

#include "gpio_list.h" /* Must come after other header files. */

/******************************************************************************/
/* USB-A charging control */

const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_PP5000_USBA_R,
};
BUILD_ASSERT(ARRAY_SIZE(usb_port_enable) == USB_PORT_COUNT);

/******************************************************************************/

/*
 * FW_CONFIG defaults for brya if the CBI.FW_CONFIG data is not
 * initialized.
 */
const union brya_cbi_fw_config fw_config_defaults = {
	.usb_db = DB_USB3_PS8815,
};
