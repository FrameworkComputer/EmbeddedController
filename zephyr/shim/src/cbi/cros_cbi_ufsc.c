/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cros_board_info.h"
#include "cros_cbi.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(cros_cbi_ufsc, LOG_LEVEL_ERR);

#define CBI_UFSC_COMPAT cros_ec_cbi_ufsc
#define CBI_UFSC_NODE DT_INST(0, CBI_UFSC_COMPAT)

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(CBI_UFSC_COMPAT) == 1,
	     "More than one CBI UFSC node defined");

#define DT_DRV_COMPAT cros_ec_cbi_ufsc_value

/* --- Compile-time DTS validation --- */

#define VALIDATE_UFSC_FIELD(id)                                               \
	BUILD_ASSERT(DT_PROP_LEN(id, start) == 1,                             \
		     "UFSC field has discontiguous bits, not yet supported"); \
	BUILD_ASSERT(DT_PROP_BY_IDX(id, start, 0) <                           \
			     (CBI_UFSC_DATA_COUNT * 32),                      \
		     "UFSC start bit is out of bounds (must be < 160)");      \
	BUILD_ASSERT(DT_PROP_BY_IDX(id, size, 0) <= 8,                        \
		     "UFSC field size cannot exceed 8 bits");                 \
	BUILD_ASSERT(DT_PROP_LEN(id, start) == DT_PROP_LEN(id, size),         \
		     "UFSC start and size arrays must have same length");     \
	BUILD_ASSERT(                                                         \
		(DT_PROP_BY_IDX(id, start, 0) / 32) ==                        \
			((DT_PROP_BY_IDX(id, start, 0) +                      \
			  DT_PROP_BY_IDX(id, size, 0) - 1) /                  \
			 32),                                                 \
		"UFSC field crosses a 32-bit boundary, which is not allowed.");

DT_FOREACH_CHILD_STATUS_OKAY(CBI_UFSC_NODE, VALIDATE_UFSC_FIELD)

#define VALIDATE_UFSC_VALUE(inst)                                              \
	BUILD_ASSERT(DT_INST_PROP(inst, value) <                               \
			     (1 << DT_PROP_BY_IDX(                             \
				      DT_PARENT(DT_DRV_INST(inst)), size, 0)), \
		     "UFSC value is too large for its parent field size");

DT_INST_FOREACH_STATUS_OKAY(VALIDATE_UFSC_VALUE)

/* --- Data Structures --- */

static struct cbi_ufsc cached_ufsc;
static bool cached_ufsc_ready;

#define CBI_UFSC_VALUE_ARRAY_ID(id) \
	[CBI_UFSC_VALUE_ID(id)] = DT_PROP(id, value),
#define CBI_UFSC_VALUE_ARRAY(inst) CBI_UFSC_VALUE_ARRAY_ID(DT_DRV_INST(inst))

/* Compile-time generated array mapping `enum cbi_ufsc_value_id` to its value */
static const uint8_t ufsc_values[] = { DT_INST_FOREACH_STATUS_OKAY(
	CBI_UFSC_VALUE_ARRAY) };

/* --- Device Tree Parsing Macros --- */

#define UFSC_PARENT_NODE(inst) DT_PARENT(DT_DRV_INST(inst))
#define UFSC_FIELD_START(inst) DT_PROP_BY_IDX(UFSC_PARENT_NODE(inst), start, 0)
#define UFSC_FIELD_SIZE(inst) DT_PROP_BY_IDX(UFSC_PARENT_NODE(inst), size, 0)
#define UFSC_VALUE(inst) ((uint32_t)DT_INST_PROP(inst, value))

/*
 * Builds a case statement for the `get_parent_field_value` switch. It maps a
 * `value_id` enum back to the start and size of its parent field.
 */
#define CBI_UFSC_PARENT_FIELD_CASE(inst)                 \
	case CBI_UFSC_VALUE_ID(DT_DRV_INST(inst)):       \
		start = (uint8_t)UFSC_FIELD_START(inst); \
		size = (uint8_t)UFSC_FIELD_SIZE(inst);   \
		break;

/*
 * Generates a bitwise-OR term for a default value if it belongs to the
 * specified data index.
 *
 * If the node has a `default` property and its field is in the target data,
 * this expands to `| (value << bit_offset)`. Otherwise, it expands to nothing
 * (or `| 0`).
 */
#define CBI_UFSC_DEFAULT_TERM(inst, index)                           \
	COND_CODE_1(DT_INST_PROP(inst, default),                     \
		    (| (((UFSC_FIELD_START(inst) / 32) == index) ?   \
				(UFSC_VALUE(inst)                    \
				 << (UFSC_FIELD_START(inst) % 32)) : \
				0)),                                 \
		    ())

/*
 * Builds a complete 32-bit default value for a single data by iterating
 * over all `-value` nodes and summing the terms for the given `index`.
 */
#define CBI_UFSC_DEFAULT_DATA(index) \
	(0 DT_INST_FOREACH_STATUS_OKAY_VARGS(CBI_UFSC_DEFAULT_TERM, index))

/* Pre-calculates and packs the default UFSC. */
static const struct cbi_ufsc default_ufsc = { .data = {
						      CBI_UFSC_DEFAULT_DATA(0),
						      CBI_UFSC_DEFAULT_DATA(1),
						      CBI_UFSC_DEFAULT_DATA(2),
						      CBI_UFSC_DEFAULT_DATA(3),
					      } };

/* --- Internal Helper Functions --- */

static inline uint8_t read_ufsc_field(const struct cbi_ufsc *ufsc,
				      uint8_t start, uint8_t size)
{
	uint8_t data_index = start / 32;
	uint8_t bit_offset = start % 32;
	uint32_t mask = BIT_MASK(size);

	return (ufsc->data[data_index] >> bit_offset) & mask;
}

static int get_parent_field_value(struct cbi_ufsc ufsc,
				  enum cbi_ufsc_value_id value_id,
				  uint8_t *value)
{
	uint8_t start = 0;
	uint8_t size = 0;

	/*
	 * This switch statement is populated by the preprocessor to map a
	 * value_id enum back to its parent field's start and size.
	 */
	switch (value_id) {
		DT_INST_FOREACH_STATUS_OKAY(CBI_UFSC_PARENT_FIELD_CASE)
	default:
		return -EINVAL;
	}

	*value = read_ufsc_field(&ufsc, start, size);
	return 0;
}

/* --- Public API --- */

void cros_cbi_ufsc_init(void)
{
	if (cbi_get_ufsc(&cached_ufsc) != EC_SUCCESS) {
		LOG_WRN("CBI: UFSC not found, using defaults.");
		cached_ufsc = default_ufsc;
	}
	cached_ufsc_ready = true;
	LOG_INF("Read CBI UFSC: 0x%08x 0x%08x 0x%08x 0x%08X 0x%08x",
		cached_ufsc.data[0], cached_ufsc.data[1], cached_ufsc.data[2],
		cached_ufsc.data[3], cached_ufsc.data[4]);
}

test_mockable bool cros_cbi_ufsc_check_match(enum cbi_ufsc_value_id value_id)
{
	uint8_t cbi_val;

	if (!cached_ufsc_ready) {
		LOG_ERR("CBI UFSC read before init");
		return false;
	}

	if (get_parent_field_value(cached_ufsc, value_id, &cbi_val) != 0) {
		return false;
	}

	return cbi_val == ufsc_values[value_id];
}
