/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cros_cbi.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#define DT_DRV_COMPAT cros_ec_cbi_ufsc_pdc_selector

LOG_MODULE_REGISTER(cros_cbi_ufsc_pdc, LOG_LEVEL_INF);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Expected exactly 1 UFSC PDC selector node");

#define SELECTOR_NODE DT_DRV_INST(0)

/* Helper to check a specific index in the selections array. */
#define CHECK_MATCH(node_id, prop, idx, return_ptr)                           \
	COND_CODE_1(DT_NODE_HAS_COMPAT(DT_PHANDLE_BY_IDX(node_id, prop, idx), \
				       cros_ec_cbi_ufsc_value),               \
		    (/* UFSC-Value node: check if it matches. */              \
		     if (*return_ptr == NULL &&                               \
			 cros_cbi_ufsc_check_match(CBI_UFSC_VALUE_ID(         \
				 DT_PHANDLE_BY_IDX(node_id, prop, idx)))) {   \
			     *return_ptr = DEVICE_DT_GET(DT_PHANDLE_BY_IDX(   \
				     node_id, prop, UTIL_INC(idx)));          \
		     }),                                                      \
		    (/* Device node: skip. */                                 \
		     ))

/* Generates a static resolver function for a specific slot node. */
#define GENERATE_PORT_RESOLVER(child_node)                          \
	static const struct device *resolve_port_##child_node(void) \
	{                                                           \
		const struct device *dev = NULL;                    \
		DT_FOREACH_PROP_ELEM_VARGS(child_node, selections,  \
					   CHECK_MATCH, &dev);      \
		return dev;                                         \
	}

/* Generate a function for each port defined in DTS (port@0, port@1...) */
DT_FOREACH_CHILD(SELECTOR_NODE, GENERATE_PORT_RESOLVER)

/* Map runtime port to the generated resolver function via switch. */
#define CASE_PORT(child_node)                                               \
	case DT_PROP(child_node, reg):                                      \
		*dev = resolve_port_##child_node();                         \
		if (*dev) {                                                 \
			LOG_INF("PDC Port %d: Selected %s", port,           \
				(*dev)->name ? (*dev)->name : "(unnamed)"); \
		} else {                                                    \
			LOG_INF("PDC Port %d: Disabled (No UFSC match)",    \
				port);                                      \
		}                                                           \
		return 0;

int board_get_pdc_for_port(int port, const struct device **dev)
{
	*dev = NULL;

	switch (port) {
		DT_FOREACH_CHILD(SELECTOR_NODE, CASE_PORT)
	default:
		LOG_WRN("PDC Port %d: No selector defined", port);
		return 0;
	}
}
