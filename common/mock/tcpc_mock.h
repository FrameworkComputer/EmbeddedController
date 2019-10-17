/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
 /* Mock for the TCPC interface */

#include "usb_pd_tcpm.h"
#include "usb_pd.h"

/* Controller for TCPC state */
struct mock_tcpc_ctrl {
	enum tcpc_cc_voltage_status cc1;
	enum tcpc_cc_voltage_status cc2;
	int vbus_level;
	enum pd_power_role power_role;
	enum pd_data_role data_role;
	int num_calls_to_set_header;
	int should_print_header_changes;
	};

/* Reset this TCPC mock */
void mock_tcpc_reset(void);

extern const struct tcpm_drv mock_tcpc_driver;
extern struct mock_tcpc_ctrl mock_tcpc;
