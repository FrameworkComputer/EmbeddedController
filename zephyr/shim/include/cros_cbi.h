/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CROS_CBI_H
#define __CROS_EC_CROS_CBI_H

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Macros are _INST_ types, so require DT_DRV_COMPAT to be defined.
 */
#define DT_DRV_COMPAT cros_ec_cbi_ssfc_value

#define CBI_SSFC_VALUE_COMPAT DT_DRV_COMPAT
#define CBI_SSFC_VALUE_ID(id) DT_CAT(CBI_SSFC_VALUE_, id)
#define CBI_SSFC_VALUE_ID_WITH_COMMA(id) CBI_SSFC_VALUE_ID(id),
#define CBI_SSFC_VALUE_INST_ENUM(inst, _) \
	CBI_SSFC_VALUE_ID_WITH_COMMA(DT_INST(inst, CBI_SSFC_VALUE_COMPAT))

enum cbi_ssfc_value_id {
	LISTIFY(DT_NUM_INST_STATUS_OKAY(CBI_SSFC_VALUE_COMPAT),
		CBI_SSFC_VALUE_INST_ENUM, ()) CBI_SSFC_VALUE_COUNT
};

#undef DT_DRV_COMPAT

/*
 * Macros to help generate the enum list of field and value names
 * for the FW_CONFIG CBI data.
 */
#define CBI_FW_CONFIG_COMPAT cros_ec_cbi_fw_config
#define CBI_FW_CONFIG_VALUE_COMPAT cros_ec_cbi_fw_config_value

/*
 * Retrieve the enum-name property for this node.
 */
#define CBI_FW_CONFIG_ENUM(node) DT_STRING_TOKEN(node, enum_name)

/*
 * Create an enum entry without a value (an enum with a following comma).
 */
#define CBI_FW_CONFIG_ENUM_WITH_COMMA(node) CBI_FW_CONFIG_ENUM(node),

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
/* clang-format off */
enum cbi_fw_config_field_id {
	DT_FOREACH_STATUS_OKAY(CBI_FW_CONFIG_COMPAT,
			       CBI_FW_CONFIG_CHILD_ENUM_LIST)
	CBI_FW_CONFIG_FIELDS_COUNT
};
/* clang-format on */

/*
 * enum list of all child values.
 */
/* clang-format off */
enum cbi_fw_config_value_id {
	DT_FOREACH_STATUS_OKAY(CBI_FW_CONFIG_VALUE_COMPAT,
			       CBI_FW_CONFIG_ENUM_WITH_VALUE)
	CBI_FW_CONFIG_VALUES_LAST /* added to ensure at least one entry */
};
/* clang-format on */

/**
 * @brief Initialize CBI SSFC
 *
 * The function has to be called before checking SSFC values.
 */
void cros_cbi_ssfc_init(void);

/**
 * @brief Initialize CBI FW
 *
 * The function has to be called before getting CBI FW_CONFIG.
 */
void cros_cbi_fw_config_init(void);

/**
 * @brief Check if the CBI SSFC value matches the one in the EEPROM
 *
 * @param value_id The SSFC value to check in EEPROM.
 *
 * @return true If matches, false if not.
 */
bool cros_cbi_ssfc_check_match(enum cbi_ssfc_value_id value_id);

/**
 * @brief Retrieve the value of the FW_CONFIG field
 *
 * @param field_id Enum identifying the field to return.
 * @param value Pointer to the returned value.
 *
 * @return 0 if value found and returned.
 * @retval -ENOTSUP Not supported api function.
 * @retval -EINVAL Invalid field_id.
 */
int cros_cbi_get_fw_config(enum cbi_fw_config_field_id field_id,
			   uint32_t *value);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_CROS_CBI_H */
