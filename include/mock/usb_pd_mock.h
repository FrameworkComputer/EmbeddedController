/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Mock USB PD */

#ifndef __MOCK_USB_PD_MOCK_H
#define __MOCK_USB_PD_MOCK_H

#include "common.h"
#include "usb_pd.h"

/* Defaults should all be 0 values. */
struct mock_pd_port_t {
	enum pd_data_role data_role;
	enum pd_power_role power_role;
};

extern struct mock_pd_port_t mock_pd_port[CONFIG_USB_PD_PORT_MAX_COUNT];

void mock_pd_reset(void);

#endif /* __MOCK_USB_PD_MOCK_H */
