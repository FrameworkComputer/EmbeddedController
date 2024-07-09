/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Type-C control logic header */

#ifndef __CROS_EC_TYPEC_CONTROL_H
#define __CROS_EC_TYPEC_CONTROL_H

#include "usb_pd_tcpm.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sets the polarity of the port
 *
 * @param port USB-C port number
 * @param polarity Polarity of CC lines
 */
void typec_set_polarity(int port, enum tcpc_cc_polarity polarity);

/**
 * Turn on/off the SBU FETs.
 *
 * @param port USB-C port number
 * @param enable true:enable, false:disable
 */
void typec_set_sbu(int port, bool enable);

/**
 * Set the type-C current limit when sourcing current
 *
 * @param port USB-C port number
 * @param rp Pull-up values to be aplied as a SRC to advertise current limits
 */
__override_proto void typec_set_source_current_limit(int port,
						     enum tcpc_rp_value rp);

/**
 * Turn on/off the VCONN FET
 *
 * @param port USB-C port number
 * @param enable true:enable, false:disable
 */
void typec_set_vconn(int port, bool enable);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_TYPEC_CONTROL_H */
