/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Mock for the TCPC interface */

#ifndef __MOCK_TCPC_MOCK_H
#define __MOCK_TCPC_MOCK_H

#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Controller for TCPC state */
struct mock_tcpc_ctrl {
	enum tcpc_cc_voltage_status cc1;
	enum tcpc_cc_voltage_status cc2;
	int vbus_level;
	int num_calls_to_set_header;
	bool should_print_call;
	uint64_t first_call_to_enable_auto_toggle;
	bool lpm_wake_requested;

	/* Set to function pointer if callback is needed for test code */
	struct tcpm_drv callbacks;

	/* Store the latest values that were set on TCPC API */
	struct {
		enum pd_power_role power_role;
		enum pd_data_role data_role;
		enum tcpc_cc_pull cc;
		enum tcpc_rp_value rp;
		enum tcpc_cc_polarity polarity;
	} last;
};

/* Reset this TCPC mock */
void mock_tcpc_reset(void);

extern const struct tcpm_drv mock_tcpc_driver;
extern struct mock_tcpc_ctrl mock_tcpc;

#ifdef __cplusplus
}
#endif

#endif /* __MOCK_TCPC_MOCK_H */