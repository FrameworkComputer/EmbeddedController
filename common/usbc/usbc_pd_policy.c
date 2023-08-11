/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "ec_commands.h"
#include "usb_pe_sm.h"
#include "usb_tc_sm.h"

/*
 * TODO(b:159715784): Implement a more robust solution
 * to managing PD Policies.
 */

/*
 * Default Port Discovery DR Swap Policy.
 *
 * 1) If dr_swap_to_dfp_flag == true and port data role is UFP,
 *    transition to pe_drs_send_swap
 */
__overridable bool port_discovery_dr_swap_policy(int port, enum pd_data_role dr,
						 bool dr_swap_flag)
{
	if (dr_swap_flag && dr == PD_ROLE_UFP)
		return true;

	/* Do not perform a DR swap */
	return false;
}

/*
 * Default Port Discovery VCONN Swap Policy.
 *
 * 1) If vconn_swap_to_on_flag == true, and vconn is currently off,
 * 2) Sourcing VCONN is possible
 *    then transition to pe_vcs_send_swap
 */
__overridable bool port_discovery_vconn_swap_policy(int port,
						    bool vconn_swap_flag)
{
	if (IS_ENABLED(CONFIG_USBC_VCONN) && vconn_swap_flag &&
	    !tc_is_vconn_src(port) && tc_check_vconn_swap(port))
		return true;

	/* Do not perform a VCONN swap */
	return false;
}

/*
 * Default Port Disable FRS until VBUS source on Policy.
 *
 * Default implementation is disabling FRS after receiving FRS Rx signal.
 *
 * @param port USB-C port number
 * @param return True if FRS disable is delayed until PE_SRC_STARTUP, else
 *        False.
 */
__overridable bool port_frs_disable_until_source_on(int port)
{
	return false;
}
