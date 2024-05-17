/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "cros_board_info.h"
#include "cros_cbi.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(cros_cbi_ssfc, LOG_LEVEL_ERR);

/* Actually, two "compatible" values are handle here - cros_ec_cbi_ssfc_value
 * and cros_ec_cbi_ssfc. cros_ec_cbi_ssfc_value nodes are grandchildren of the
 * cros_ec_cbi_ssfc node. cros_ec_cbi_ssfc_value is introduced to iterate over
 * grandchildren of the cros_ec_cbi_ssfc (macro DT_FOREACH_CHILD can not be
 * nested) and it can be pointed by a sensor dts to indicate alternative usage.
 */
#define DT_DRV_COMPAT cros_ec_cbi_ssfc_value

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(cros_ec_cbi_ssfc) < 2,
	     "More than 1 CBI SSFS node");
#define CBI_SSFC_NODE DT_INST(0, cros_ec_cbi_ssfc)

#define CBI_SSFC_INIT_DEFAULT_ID(id, ssfc)                              \
	do {                                                            \
		if (DT_PROP(id, default)) {                             \
			ssfc.CBI_SSFC_UNION_ENTRY_NAME(DT_PARENT(id)) = \
				DT_PROP(id, value);                     \
		}                                                       \
	} while (0);

#define CBI_SSFC_INIT_DEFAULT(inst, ssfc) \
	CBI_SSFC_INIT_DEFAULT_ID(DT_DRV_INST(inst), ssfc)

#define CBI_SSFC_VALUE_ARRAY_ID(id) \
	[CBI_SSFC_VALUE_ID(id)] = DT_PROP(id, value),

#define CBI_SSFC_VALUE_ARRAY(inst) CBI_SSFC_VALUE_ARRAY_ID(DT_DRV_INST(inst))

#define CBI_SSFC_VALUE_BUILD_ASSERT(inst)                    \
	BUILD_ASSERT(DT_INST_PROP(inst, value) <= UINT8_MAX, \
		     "CBI SSFS value too big");

#define CBI_SSFC_PARENT_VALUE_CASE_GENERATE(value_id, value_parent, value) \
	case value_id:                                                     \
		*value = value_parent;                                     \
		break;

#define CBI_SSFC_PARENT_VALUE_CASE_ID(id, cached_ssfc, value) \
	CBI_SSFC_PARENT_VALUE_CASE_GENERATE(                  \
		CBI_SSFC_VALUE_ID(id),                        \
		cached_ssfc.CBI_SSFC_UNION_ENTRY_NAME(DT_PARENT(id)), value)

#define CBI_SSFC_PARENT_VALUE_CASE(inst, cached_ssfc, value) \
	CBI_SSFC_PARENT_VALUE_CASE_ID(DT_DRV_INST(inst), cached_ssfc, value)

#define CBI_SSFC_UNION_ENTRY_NAME(id) DT_CAT(cbi_ssfc_, id)
#define CBI_SSFC_UNION_ENTRY(id)               \
	uint32_t CBI_SSFC_UNION_ENTRY_NAME(id) \
		: DT_PROP(id, size);

#define CBI_SSFC_PLUS_FIELD_SIZE(id) +DT_PROP(id, size)
#define CBI_SSFC_FIELDS_SIZE                                                 \
	(0 COND_CODE_1(                                                      \
		DT_NODE_EXISTS(CBI_SSFC_NODE),                               \
		(DT_FOREACH_CHILD(CBI_SSFC_NODE, CBI_SSFC_PLUS_FIELD_SIZE)), \
		()))

BUILD_ASSERT(CBI_SSFC_FIELDS_SIZE <= 32, "CBI SSFS is bigger than 32 bits");

/*
 * Define union bit fields based on the device tree entries. Example:
 * cbi-ssfc {
 *	compatible = "cros-ec,cbi-ssfc";
 *
 *	base_sensor {
 *		enum-name = "BASE_SENSOR";
 *		size = <3>;
 *		bmi160 {
 *			compatible = "cros-ec,cbi-ssfc-value";
 *			status = "okay";
 *			value = <1>;
 *		};
 *	};
 *	lid_sensor {
 *		enum-name = "LID_SENSOR";
 *		size = <3>;
 *		bma255 {
 *			compatible = "cros-ec,cbi-ssfc-value";
 *			status = "okay";
 *			value = <1>;
 *		};
 *	};
 *	lightbar {
 *		enum-name = "LIGHTBAR";
 *		size = <2>;
 *		10_led {
 *			compatible = "cros-ec,cbi-ssfc-value";
 *			status = "okay";
 *			value = <1>;
 *		};
 *	};
 * };
 * Should be converted into
 * union cbi_ssfc {
 *	struct {
 *		uint32_t cbi_ssfc_DT_N_S_cbi_ssfc_S_base_sensor:3
 *		uint32_t cbi_ssfc_DT_N_S_cbi_ssfc_S_lid_sensor:3
 *		uint32_t cbi_ssfc_DT_N_S_cbi_ssfc_S_lightbar:2
 *		uint32_t reserved : 24;
 *	};
 *	uint32_t raw_value;
 * };
 */
union cbi_ssfc {
	struct {
#if DT_NODE_EXISTS(CBI_SSFC_NODE)
		DT_FOREACH_CHILD(CBI_SSFC_NODE, CBI_SSFC_UNION_ENTRY)
		uint32_t reserved : (32 - CBI_SSFC_FIELDS_SIZE);
#endif
	};
	uint32_t raw_value;
};

BUILD_ASSERT(sizeof(union cbi_ssfc) == sizeof(uint32_t),
	     "CBI SSFS structure exceeds 32 bits");

DT_INST_FOREACH_STATUS_OKAY(CBI_SSFC_VALUE_BUILD_ASSERT)

static const uint8_t ssfc_values[] = { DT_INST_FOREACH_STATUS_OKAY(
	CBI_SSFC_VALUE_ARRAY) };

static union cbi_ssfc cached_ssfc;

void cros_cbi_ssfc_init(void)
{
	if (cbi_get_ssfc(&cached_ssfc.raw_value) != EC_SUCCESS) {
		DT_INST_FOREACH_STATUS_OKAY_VARGS(CBI_SSFC_INIT_DEFAULT,
						  cached_ssfc)
	}

	LOG_INF("Read CBI SSFC : 0x%08X\n", cached_ssfc.raw_value);
}

static int cros_cbi_ssfc_get_parent_field_value(union cbi_ssfc cached_ssfc,
						enum cbi_ssfc_value_id value_id,
						uint32_t *value)
{
	switch (value_id) {
		DT_INST_FOREACH_STATUS_OKAY_VARGS(CBI_SSFC_PARENT_VALUE_CASE,
						  cached_ssfc, value)
	default:
		LOG_ERR("CBI SSFC parent field value not found: %d\n",
			value_id);
		return -EINVAL;
	}
	return 0;
}

bool cros_cbi_ssfc_check_match(enum cbi_ssfc_value_id value_id)
{
	int rc;
	uint32_t value;

	rc = cros_cbi_ssfc_get_parent_field_value(cached_ssfc, value_id,
						  &value);
	if (rc) {
		return false;
	}
	return value == ssfc_values[value_id];
}
