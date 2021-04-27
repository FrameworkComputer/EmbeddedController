/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SHIM_INCLUDE_CBI_SSFC_H_
#define ZEPHYR_SHIM_INCLUDE_CBI_SSFC_H_

#include <devicetree.h>
#include <device.h>

#define CBI_SSFC_NODE			DT_PATH(cbi_ssfc)

#define CBI_SSFC_UNION_ENTRY_NAME(id)	DT_CAT(cbi_ssfc_, id)
#define CBI_SSFC_UNION_ENTRY(id)               \
	uint32_t CBI_SSFC_UNION_ENTRY_NAME(id) \
		: DT_PROP(id, size);

#define CBI_SSFC_PLUS_FIELD_SIZE(id)	+ DT_PROP(id, size)
#define CBI_SSFC_FIELDS_SIZE                                         \
	(0 COND_CODE_1(DT_NODE_EXISTS(CBI_SSFC_NODE),                \
		       (DT_FOREACH_CHILD(CBI_SSFC_NODE,              \
					 CBI_SSFC_PLUS_FIELD_SIZE)), \
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
 *
 *			value = <1>;
 *			devices = <>;
 *		};
 *	};
 *	lid_sensor {
 *		enum-name = "LID_SENSOR";
 *		size = <3>;
 *		bma255 {
 *			compatible = "named-cbi-ssfc-value";
 *			status = "okay";
 *
 *			value = <1>;
 *			devices = <&lid_accel>;
 *		};
 *	};
 *	lightbar {
 *		enum-name = "LIGHTBAR";
 *		size = <2>;
 *		10_led {
 *			compatible = "named-cbi-ssfc-value";
 *			status = "okay";
 *
 *			value = <1>;
 *			devices = <>;
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
	"CBI SSFS structure exceedes 32 bits");

#endif /* ZEPHYR_SHIM_INCLUDE_CBI_SSFC_H_ */
