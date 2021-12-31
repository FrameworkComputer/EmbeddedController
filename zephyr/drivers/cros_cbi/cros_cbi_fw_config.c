/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/cros_cbi.h>
#include <logging/log.h>

#include "cros_board_info.h"
#include "cros_cbi_ssfc.h"
#include "cros_cbi_common.h"

LOG_MODULE_REGISTER(cros_cbi_fw_config, LOG_LEVEL_ERR);

/*
 * Validation macros.
 * These are moved out of header files to reduce the overhead
 * during compilation.
 */
/*
 * Do not perform validation if no FW_CONFIG nodes exist so that
 * the validation macros are simplified.
 */
#if DT_HAS_COMPAT_STATUS_OKAY(CBI_FW_CONFIG_FIELD_COMPAT)

/*
 * Statically count the number of bits set in a 32 bit constant expression.
 */
#define BIT_SET(v, b) ((v >> b) & 1)
#define BIT_COUNT(v) \
	(BIT_SET(v, 31) + BIT_SET(v, 30) + BIT_SET(v, 29) + BIT_SET(v, 28) + \
	 BIT_SET(v, 27) + BIT_SET(v, 26) + BIT_SET(v, 25) + BIT_SET(v, 24) + \
	 BIT_SET(v, 23) + BIT_SET(v, 22) + BIT_SET(v, 21) + BIT_SET(v, 20) + \
	 BIT_SET(v, 19) + BIT_SET(v, 18) + BIT_SET(v, 17) + BIT_SET(v, 16) + \
	 BIT_SET(v, 15) + BIT_SET(v, 14) + BIT_SET(v, 13) + BIT_SET(v, 12) + \
	 BIT_SET(v, 11) + BIT_SET(v, 10) + BIT_SET(v,  9) + BIT_SET(v,  8) + \
	 BIT_SET(v,  7) + BIT_SET(v,  6) + BIT_SET(v,  5) + BIT_SET(v,  4) + \
	 BIT_SET(v,  3) + BIT_SET(v,  2) + BIT_SET(v,  1) + BIT_SET(v,  0))

/*
 * Shorthand macros to access properties on the node.
 */
#define FW_START(id) DT_PROP(id, start)
#define FW_SIZE(id) DT_PROP(id, size)

/*
 * Calculate the mask of the field from the size.
 */
#define FW_MASK(id) ((1 << FW_SIZE(id)) - 1)

/*
 * Calculate the mask and shift it to the correct start bit.
 */
#define FW_SHIFT_MASK(id) (FW_MASK(id) << FW_START(id))

/*
 * For a child "named-cbi-fw-config-value" node, retrieve the
 * size of the field this value is associated with.
 */
#define FW_PARENT_SIZE(id) DT_PROP(DT_PARENT(id), size)

/*
 * Validation check to ensure total field sizes do not exceed 32 bits.
 * The FOREACH loop is nested, one to iterate through all the
 * fw_config nodes, and another for the child field nodes in each
 * of the fw_config nodes.
 */
#define PLUS_FIELD_SIZE(inst) + DT_PROP(inst, size)
#define FIELDS_ALL_SIZE(inst) \
	DT_FOREACH_CHILD_STATUS_OKAY(inst, PLUS_FIELD_SIZE)

/*
 * Result is the sum of all the field sizes.
 */
#define TOTAL_FW_CONFIG_NODES_SIZE \
	(0 DT_FOREACH_STATUS_OKAY(CBI_FW_CONFIG_FIELD_COMPAT, FIELDS_ALL_SIZE))

BUILD_ASSERT(TOTAL_FW_CONFIG_NODES_SIZE <= 32,
	     "CBI FW Config is bigger than 32 bits");

/*
 * Validation check to ensure there are no overlapping fields.
 * OR together all the masks, count the bits, and compare against the
 * total of the sizes. They should match.
 */
#define OR_FIELD_SHIFT_MASK(id) | FW_SHIFT_MASK(id)
#define FIELDS_ALL_BITS_SET(inst)		\
	DT_FOREACH_CHILD_STATUS_OKAY(inst, OR_FIELD_SHIFT_MASK)

#define TOTAL_BITS_SET \
	(0 DT_FOREACH_STATUS_OKAY(CBI_FW_CONFIG_FIELD_COMPAT, \
				  FIELDS_ALL_BITS_SET))

BUILD_ASSERT(BIT_COUNT(TOTAL_BITS_SET) == TOTAL_FW_CONFIG_NODES_SIZE,
		       "CBI FW Config has overlapping fields");

/*
 * Validation for each assigned field values.
 * The value must fit within the parent's defined size.
 */
#define FW_VALUE_BUILD_ASSERT(inst)			\
	BUILD_ASSERT(DT_PROP(inst, value) <		\
		     (1 << FW_PARENT_SIZE(inst)),	\
		     "CBI FW Config value too big")

DT_FOREACH_STATUS_OKAY(DT_DRV_COMPAT, FW_VALUE_BUILD_ASSERT)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(CBI_FW_CONFIG_FIELD_COMPAT) */

/*
 * Define union bit fields based on the device tree entries. Example:
 * cbi-fw-config {
 *	compatible = "named-cbi-fw-config";
 *
 *	fan {
 *		enum-name = "FAN";
 *		start = <0>;
 *		size = <1>;
 *		fan_present {
 *			enum-name = "PRESENT"
 *			compatible = "named-cbi-fw-config-value";
 *			value = <1>;
 *		};
 *	};
 * };
 *
 * Creates
 * switch (field_id) {
 * case CBI_FW_CONFIG_FAN:
 *	return (value >> 0) & 1;
 *	...
 * }
 *
 */

/*
 * The per-field case statement.
 * Extract the field value using the start and size.
 */
#define FW_FIELD_CASE(id, cached, value)	\
	case CBI_FW_CONFIG_FIELD_ID(id):				\
		*value = (cached >> FW_START(id)) & FW_MASK(id);	\
		break;

/*
 * Create a case for every child of this "named-cbi-fw-config" node.
 */
#define FW_FIELD_NODES(inst, cached, value) \
	DT_FOREACH_CHILD_STATUS_OKAY_VARGS(inst, FW_FIELD_CASE, cached, value)

void cros_cbi_fw_config_init(const struct device *dev)
{
	struct cros_cbi_data *data = (struct cros_cbi_data *)(dev->data);

	if (cbi_get_fw_config(&data->cached_fw_config) != EC_SUCCESS)
		/*
		 * Missing fw config will defaults to all zeros.
		 */
		data->cached_fw_config = 0;

	LOG_INF("Read CBI FW Config : 0x%08X\n", data->cached_fw_config);
}

static int cros_cbi_fw_config_get_field(
				uint32_t cached_fw_config,
				enum cbi_fw_config_field_id field_id,
				uint32_t *value)
{
	switch (field_id) {
		/*
		 * Iterate through all the the "named-cbi-fw-config" nodes,
		 * and create cases for all of their child nodes.
		 */
		DT_FOREACH_STATUS_OKAY_VARGS(CBI_FW_CONFIG_FIELD_COMPAT,
					     FW_FIELD_NODES,
					     cached_fw_config,
					     value)
	default:
		return -EINVAL;
	}
	return 0;
}

int cros_cbi_ec_get_fw_config(const struct device *dev,
			      enum cbi_fw_config_field_id field_id,
			      uint32_t *value)
{
	struct cros_cbi_data *data = (struct cros_cbi_data *)(dev->data);
	int rc;

	rc = cros_cbi_fw_config_get_field(data->cached_fw_config,
					  field_id, value);
	if (rc)
		LOG_ERR("CBI FW Config field not found: %d\n", field_id);
	return rc;
}
