/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USBC PD Default Policies  */

#ifndef __CROS_EC_USB_PD_POLICY_H
#define __CROS_EC_USB_PD_POLICY_H

#include "usb_pe_sm.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Port Discovery DR Swap Policy
 *
 * Different boards can implement its own DR swap policy during a port discovery
 * by implementing this function.
 *
 * @param port USB-C port number
 * @param dr   current port data role
 * @param dr_swap_flag   Data Role Swap Flag bit
 * @param return True if state machine should perform a DR swap, elsf False
 */
__override_proto bool port_discovery_dr_swap_policy(int port,
						    enum pd_data_role dr,
						    bool dr_swap_flag);

/**
 * Port Discovery VCONN Swap Policy
 *
 * Different boards can implement its own VCONN swap policy during a port
 * discovery by implementing this function.
 *
 * @param port USB-C port number
 * @param vconn_swap_to_on_flag   Vconn Swap to On Flag bit
 * @param return True if state machine should perform a VCONN swap, elsf False
 */
__override_proto bool port_discovery_vconn_swap_policy(int port,
						       bool vconn_swap_flag);

/**
 * Port Disable FRS until VBUS source on policy.
 *
 * Port disable FRS until VBUS source on policy. Different boards can implement
 * its own FRS disable timing rules. Default timing is after receiving FRS Rx
 * signal.
 *
 * @param port USB-C port number
 * @param return True if FRS disable is delayed, else False.
 */
__override_proto bool port_frs_disable_until_source_on(int port);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_USB_PD_POLICY_H */
