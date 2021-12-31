/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_CBI_CROS_CBI_SSFC_H
#define __CROS_CBI_CROS_CBI_SSFC_H

/* CBI SSFC part */

/* This part of the driver is about CBI SSFC part.
 * Actually, two "compatible" values are handle here -
 * named_cbi_ssfc_value and named_cbi_ssfc. named_cbi_ssfc_value nodes are
 * grandchildren of the named_cbi_ssfc node. named_cbi_ssfc_value is introduced
 * to iterate over grandchildren of the named_cbi_ssfc(macro
 * DT_FOREACH_CHILD can not be nested) and it can be pointed by a sensor dts to
 * indicate alternative usage.
 */
#define DT_DRV_COMPAT named_cbi_ssfc_value

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(named_cbi_ssfc) < 2,
	     "More than 1 CBI SSFS node");
#define CBI_SSFC_NODE DT_INST(0, named_cbi_ssfc)

#define CBI_SSFC_INIT_DEFAULT_ID(id, data)                           \
	do {                                                         \
		if (DT_PROP(id, default)) {                          \
			data->cached_ssfc.CBI_SSFC_UNION_ENTRY_NAME( \
				DT_PARENT(id)) = DT_PROP(id, value); \
		}                                                    \
	} while (0);

#define CBI_SSFC_INIT_DEFAULT(inst, data) \
	CBI_SSFC_INIT_DEFAULT_ID(DT_DRV_INST(inst), data)

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
 *	compatible = "named-cbi-ssfc";
 *
 *	base_sensor {
 *		enum-name = "BASE_SENSOR";
 *		size = <3>;
 *		bmi160 {
 *			compatible = "named-cbi-ssfc-value";
 *			status = "okay";
 *			value = <1>;
 *		};
 *	};
 *	lid_sensor {
 *		enum-name = "LID_SENSOR";
 *		size = <3>;
 *		bma255 {
 *			compatible = "named-cbi-ssfc-value";
 *			status = "okay";
 *			value = <1>;
 *		};
 *	};
 *	lightbar {
 *		enum-name = "LIGHTBAR";
 *		size = <2>;
 *		10_led {
 *			compatible = "named-cbi-ssfc-value";
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

#undef DT_DRV_COMPAT

#endif /* __CROS_CBI_CROS_CBI_SSFC_H */
