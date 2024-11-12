/*
 * Copyright 2024 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ucsi.h"
#include "cypress_pd_common.h"

enum ucsi_port {
	UCSI_PORT_1,
	UCSI_PORT_2,
	UCSI_PORT_3,
	UCSI_PORT_4,
};

struct ucsi_to_pd_port_map ucsi_pd_port_map[] = {
	[UCSI_PORT_1] = {.pd_controller = PD_CHIP_0,
					 .pd_controller_port = PD_CHIP_UCSI_CONNECTOR_1},
	[UCSI_PORT_2] = {.pd_controller = PD_CHIP_0,
					 .pd_controller_port = PD_CHIP_UCSI_CONNECTOR_2},
	[UCSI_PORT_3] = {.pd_controller = PD_CHIP_1,
					 .pd_controller_port = PD_CHIP_UCSI_CONNECTOR_1},
	[UCSI_PORT_4] = {.pd_controller = PD_CHIP_1,
					 .pd_controller_port = PD_CHIP_UCSI_CONNECTOR_2},
};
BUILD_ASSERT(ARRAY_SIZE(ucsi_pd_port_map) == CONFIG_USB_PD_PORT_MAX_COUNT);
