/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _AP_PWRSEQ_SM_DEFS_H_
#define _AP_PWRSEQ_SM_DEFS_H_
#include "ap_power/ap_pwrseq.h"

#include <zephyr/smf.h>

struct ap_pwrseq_smf {
	/* Zephyr SMF state actions */
	const struct smf_state actions;
	/* Enumeration value of power state */
	enum ap_pwrseq_state state;
};

/**
 * Makes structures declarations of ACPI states for every level visible
 * throughout state machine domain, definition will be completed by build system
 * and appended to array `ap_pwrseq_states`.
 **/
#define AP_POWER_STATE_DECL(state)                               \
	extern const struct smf_state arch_##state##_actions;    \
	extern const struct smf_state chipset_##state##_actions; \
	extern const struct ap_pwrseq_smf app_state_##name;

AP_POWER_STATE_DECL(AP_POWER_STATE_G3)
AP_POWER_STATE_DECL(AP_POWER_STATE_S5)
AP_POWER_STATE_DECL(AP_POWER_STATE_S4)
AP_POWER_STATE_DECL(AP_POWER_STATE_S3)
AP_POWER_STATE_DECL(AP_POWER_STATE_S2)
AP_POWER_STATE_DECL(AP_POWER_STATE_S1)
AP_POWER_STATE_DECL(AP_POWER_STATE_S0)

/**
 * Makes visible `struct ap_pwrseq_smf` declarations for defined substates
 * throughout the state machine domain, definition will be completed by build
 * system and appended to array `ap_pwrseq_states`.
 **/
#define AP_PWRSEQ_CHIPSET_SUB_STATE_DECL(state, prefix) \
	extern const struct ap_pwrseq_smf chipset_##state##_actions;

#define AP_PWRSEQ_CHIPSET_SUB_STATE_DECL_(state) \
	AP_PWRSEQ_CHIPSET_SUB_STATE_DECL(state, prefix)

#define AP_PWRSEQ_CHIPSET_SUB_STATE_DECL__(node_id, prop, idx) \
	AP_PWRSEQ_CHIPSET_SUB_STATE_DECL_(                     \
		DT_CAT6(node_id, _P_, prop, _IDX_, idx, _STRING_UPPER_TOKEN))

#define AP_PWRSEQ_EACH_CHIPSET_SUB_STATE_DECL(node_id)                      \
	COND_CODE_1(                                                        \
		DT_NODE_HAS_PROP(node_id, chipset),                         \
		(DT_FOREACH_PROP_ELEM(node_id, chipset,                     \
				      AP_PWRSEQ_CHIPSET_SUB_STATE_DECL__)), \
		())

#define AP_PWRSEQ_APP_SUB_STATE_DECL(state) \
	extern const struct ap_pwrseq_smf app_state_##state;

#define AP_PWRSEQ_APP_SUB_STATE_DECL_(state) AP_PWRSEQ_APP_SUB_STATE_DECL(state)

#define AP_PWRSEQ_APP_SUB_STATE_DECL__(node_id, prop, idx) \
	AP_PWRSEQ_APP_SUB_STATE_DECL_(                     \
		DT_CAT6(node_id, _P_, prop, _IDX_, idx, _STRING_UPPER_TOKEN))

#define AP_PWRSEQ_EACH_APP_SUB_STATE_DECL(node_id)                          \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, application),                 \
		    (DT_FOREACH_PROP_ELEM(node_id, application,             \
					  AP_PWRSEQ_APP_SUB_STATE_DECL__)), \
		    ())

DT_FOREACH_STATUS_OKAY(ap_pwrseq_sub_states,
		       AP_PWRSEQ_EACH_CHIPSET_SUB_STATE_DECL)
DT_FOREACH_STATUS_OKAY(ap_pwrseq_sub_states, AP_PWRSEQ_EACH_APP_SUB_STATE_DECL)

#endif /* _AP_PWRSEQ_SM_DEFS_H_ */
