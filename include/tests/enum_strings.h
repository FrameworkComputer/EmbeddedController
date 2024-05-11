/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Defines helper function that convert Enums to strings for prints in tests */

#include "usb_pd.h"
#include "usb_pd_tcpm.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __CROS_EC_TEST_ENUM_STINGS_H
#define __CROS_EC_TEST_ENUM_STINGS_H

#ifndef TEST_BUILD
#error enum_strings.h can only be used in test builds
#endif

static inline const char *from_tcpc_rp_value(enum tcpc_rp_value value)
{
	switch (value) {
	case TYPEC_RP_USB:
		return "USB-DEFAULT";
	case TYPEC_RP_1A5:
		return "1A5";
	case TYPEC_RP_3A0:
		return "3A0";
	case TYPEC_RP_RESERVED:
		return "RESERVED";
	default:
		return "UNKNOWN";
	}
}

static inline const char *from_tcpc_cc_pull(enum tcpc_cc_pull value)
{
	switch (value) {
	case TYPEC_CC_RA:
		return "RA";
	case TYPEC_CC_RP:
		return "RP";
	case TYPEC_CC_RD:
		return "RD";
	case TYPEC_CC_OPEN:
		return "OPEN";
	case TYPEC_CC_RA_RD:
		return "RA_RD";
	default:
		return "UNKNOWN";
	}
}

static inline const char *from_tcpc_cc_polarity(enum tcpc_cc_polarity value)
{
	switch (value) {
	case POLARITY_CC1:
		return "CC1";
	case POLARITY_CC2:
		return "CC2";
	case POLARITY_CC1_DTS:
		return "CC1 DTS";
	case POLARITY_CC2_DTS:
		return "CC2 DTS";
	default:
		return "UNKNOWN";
	}
}

static inline const char *from_pd_power_role(enum pd_power_role value)
{
	switch (value) {
	case PD_ROLE_SINK:
		return "SNK";
	case PD_ROLE_SOURCE:
		return "SRC";
	default:
		return "UNKNOWN";
	}
}

static inline const char *from_pd_data_role(enum pd_data_role value)
{
	switch (value) {
	case PD_ROLE_UFP:
		return "UFP";
	case PD_ROLE_DFP:
		return "DRP";
	case PD_ROLE_DISCONNECTED:
		return "DISCONNECTED";
	default:
		return "UNKNOWN";
	}
}

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_TEST_ENUM_STINGS_H */
