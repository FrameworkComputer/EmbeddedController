/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/cros_cbi.h>
#include "cros_board_info.h"
#include <logging/log.h>

LOG_MODULE_REGISTER(cros_cbi, LOG_LEVEL_ERR);

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

#define CBI_SSFC_PARENT_VALUE_CASE_GENERATE(value_id, value_parent) \
	case value_id:                                              \
		return value_parent;

#define CBI_SSFC_PARENT_VALUE_CASE_ID(id)    \
	CBI_SSFC_PARENT_VALUE_CASE_GENERATE( \
		CBI_SSFC_VALUE_ID(id),       \
		cached_ssfc.CBI_SSFC_UNION_ENTRY_NAME(DT_PARENT(id)))

#define CBI_SSFC_PARENT_VALUE_CASE(inst) \
	CBI_SSFC_PARENT_VALUE_CASE_ID(DT_DRV_INST(inst))

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
	     "CBI SSFS structure exceedes 32 bits");

DT_INST_FOREACH_STATUS_OKAY(CBI_SSFC_VALUE_BUILD_ASSERT)

static const uint8_t ssfc_values[] = {
	DT_INST_FOREACH_STATUS_OKAY(CBI_SSFC_VALUE_ARRAY)
};

/* CBI SSFC part end */

/* Device config */
struct cros_cbi_config {
	/* SSFC values for specific configs */
	const uint8_t *ssfc_values;
};

/* Device data */
struct cros_cbi_data {
	/* Cached SSFC configs */
	union cbi_ssfc cached_ssfc;
};

/* CBI SSFC part */

static void cros_cbi_ssfc_init(const struct device *dev)
{
	struct cros_cbi_data *data = (struct cros_cbi_data *)(dev->data);

	if (cbi_get_ssfc(&data->cached_ssfc.raw_value) != EC_SUCCESS) {
		DT_INST_FOREACH_STATUS_OKAY_VARGS(CBI_SSFC_INIT_DEFAULT, data)
	}

	LOG_INF("Read CBI SSFC : 0x%08X\n", data->cached_ssfc.raw_value);
}

static uint32_t cros_cbi_ssfc_get_parent_field_value(union cbi_ssfc cached_ssfc,
						enum cbi_ssfc_value_id value_id)
{
	switch (value_id) {
		DT_INST_FOREACH_STATUS_OKAY(CBI_SSFC_PARENT_VALUE_CASE)
	default:
		LOG_ERR("CBI SSFC parent field value not found: %d\n",
		        value_id);
		return 0;
	}
}

static int cros_cbi_ec_ssfc_check_match(const struct device *dev,
					enum cbi_ssfc_value_id value_id)
{
	struct cros_cbi_data *data = (struct cros_cbi_data *)(dev->data);
	struct cros_cbi_config *cfg = (struct cros_cbi_config *)(dev->config);

	return cros_cbi_ssfc_get_parent_field_value(data->cached_ssfc,
						    value_id) ==
	       cfg->ssfc_values[value_id];
}

/* CBI SSFC part end */
#undef DT_DRV_COMPAT

static int cros_cbi_ec_init(const struct device *dev)
{
	cros_cbi_ssfc_init(dev);

	return 0;
}

/* cros ec cbi driver registration */
static const struct cros_cbi_driver_api cros_cbi_driver_api = {
	.init = cros_cbi_ec_init,
	.ssfc_check_match = cros_cbi_ec_ssfc_check_match,
};

static int cbi_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static const struct cros_cbi_config cros_cbi_cfg = {
	.ssfc_values = ssfc_values,
};

static struct cros_cbi_data cros_cbi_data;

DEVICE_DEFINE(cros_cbi, CROS_CBI_LABEL, cbi_init, NULL, &cros_cbi_data,
	      &cros_cbi_cfg, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
	      &cros_cbi_driver_api);
