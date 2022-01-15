/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Chrome OS-specific API for access to Cros Board Info(CBI)
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_CROS_CBI_H_
#define ZEPHYR_INCLUDE_DRIVERS_CROS_CBI_H_

#include <kernel.h>
#include <device.h>
#include <devicetree.h>

/*
 * Macros are _INST_ types, so require DT_DRV_COMPAT to be defined.
 */
#define DT_DRV_COMPAT named_cbi_ssfc_value
#define CROS_CBI_LABEL				"cros_cbi"

#define CBI_SSFC_VALUE_COMPAT			named_cbi_ssfc_value
#define CBI_SSFC_VALUE_ID(id)			DT_CAT(CBI_SSFC_VALUE_, id)
#define CBI_SSFC_VALUE_ID_WITH_COMMA(id)	CBI_SSFC_VALUE_ID(id),
#define CBI_SSFC_VALUE_INST_ENUM(inst, _) \
	CBI_SSFC_VALUE_ID_WITH_COMMA(DT_INST(inst, CBI_SSFC_VALUE_COMPAT))

enum cbi_ssfc_value_id {
	UTIL_LISTIFY(DT_NUM_INST_STATUS_OKAY(CBI_SSFC_VALUE_COMPAT),
		     CBI_SSFC_VALUE_INST_ENUM)
	CBI_SSFC_VALUE_COUNT
};

#undef DT_DRV_COMPAT

/*
 * Macros to help generate the enum list of field and value names
 * for the FW_CONFIG CBI data.
 */
#define CBI_FW_CONFIG_COMPAT		cros_ec_cbi_fw_config
#define CBI_FW_CONFIG_VALUE_COMPAT	cros_ec_cbi_fw_config_value

/*
 * Retrieve the enum-name property for this node.
 */
#define CBI_FW_CONFIG_ENUM(node)	DT_STRING_TOKEN(node, enum_name)

/*
 * Create an enum entry without a value (an enum with a following comma).
 */
#define CBI_FW_CONFIG_ENUM_WITH_COMMA(node)	\
	CBI_FW_CONFIG_ENUM(node),

/*
 * Create a single enum entry with assignment to the node's value,
 * along with a following comma.
 */
#define CBI_FW_CONFIG_ENUM_WITH_VALUE(node) \
	CBI_FW_CONFIG_ENUM(node) = DT_PROP(node, value),

/*
 * Generate a list of enum entries without a value.
 */
#define CBI_FW_CONFIG_CHILD_ENUM_LIST(node) \
	DT_FOREACH_CHILD_STATUS_OKAY(node, CBI_FW_CONFIG_ENUM_WITH_COMMA)

/*
 * Enum list of all fields.
 */
enum cbi_fw_config_field_id {
	DT_FOREACH_STATUS_OKAY(CBI_FW_CONFIG_COMPAT,
			       CBI_FW_CONFIG_CHILD_ENUM_LIST)
	CBI_FW_CONFIG_FIELDS_COUNT
};

/*
 * enum list of all child values.
 */
enum cbi_fw_config_value_id {
	DT_FOREACH_STATUS_OKAY(CBI_FW_CONFIG_VALUE_COMPAT,
			       CBI_FW_CONFIG_ENUM_WITH_VALUE)
	CBI_FW_CONFIG_VALUES_LAST /* added to ensure at least one entry */
};

/**
 * @cond INTERNAL_HIDDEN
 *
 * cros cbi raw driver API definition and system call entry points
 *
 * (Internal use only.)
 */
typedef int (*cros_cbi_api_init)(const struct device *dev);
typedef bool (*cros_cbi_api_ssfc_check_match)(const struct device *dev,
					      enum cbi_ssfc_value_id value_id);
typedef int (*cros_cbi_api_get_fw_config)(const struct device *dev,
					  enum cbi_fw_config_field_id field_id,
					  uint32_t *value);

__subsystem struct cros_cbi_driver_api {
	cros_cbi_api_init init;
	cros_cbi_api_ssfc_check_match ssfc_check_match;
	cros_cbi_api_get_fw_config get_fw_config;
};

/**
 * @endcond
 */

/**
 * @brief Initialize CBI.
 *
 * @param dev Pointer to the device structure for the CBI instance.
 *
 * @return 0 If successful.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_cbi_init(const struct device *dev);

static inline int z_impl_cros_cbi_init(const struct device *dev)
{
	const struct cros_cbi_driver_api *api =
		(const struct cros_cbi_driver_api *)dev->api;

	if (!api->init) {
		return -ENOTSUP;
	}

	return api->init(dev);
}

/**
 * @brief Check if the CBI SSFC value matches the one in the EEPROM
 *
 * @param dev Pointer to the device.
 *
 * @return 1 If matches, 0 if not.
 * @retval -ENOTSUP Not supported api function.
 */
__syscall int cros_cbi_ssfc_check_match(const struct device *dev,
					enum cbi_ssfc_value_id value_id);

static inline int
z_impl_cros_cbi_ssfc_check_match(const struct device *dev,
				 enum cbi_ssfc_value_id value_id)
{
	const struct cros_cbi_driver_api *api =
		(const struct cros_cbi_driver_api *)dev->api;

	if (!api->ssfc_check_match)
		return -ENOTSUP;

	return api->ssfc_check_match(dev, value_id);
}

/**
 * @brief Retrieve the value of the FW_CONFIG field
 *
 * @param dev Pointer to the device.
 * @param field_id Enum identifying the field to return.
 * @param value Pointer to the returned value.
 *
 * @return 0 if value found and returned.
 * @retval -ENOTSUP Not supported api function.
 * @retval -EINVAL Invalid field_id.
 */
__syscall int cros_cbi_get_fw_config(const struct device *dev,
				     enum cbi_fw_config_field_id field_id,
				     uint32_t *value);

static inline int
z_impl_cros_cbi_get_fw_config(const struct device *dev,
			      enum cbi_fw_config_field_id field_id,
			      uint32_t *value)
{
	const struct cros_cbi_driver_api *api =
		(const struct cros_cbi_driver_api *)dev->api;

	if (!api->get_fw_config)
		return -ENOTSUP;

	return api->get_fw_config(dev, field_id, value);
}

/**
 * @}
 */
#include <syscalls/cros_cbi.h>
#endif /* ZEPHYR_INCLUDE_DRIVERS_CROS_CBI_H_ */
