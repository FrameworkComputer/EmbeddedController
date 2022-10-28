/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"
#include <zephyr/ztest.h>

#include "cros_board_info.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

ZTEST(common_cbi, test_cbi_set_string__null_str)
{
	struct cbi_data data = { 0 };
	struct cbi_data unused_data = { 0 };
	enum cbi_data_tag arbitrary_valid_tag = CBI_TAG_BOARD_VERSION;

	zassert_equal(cbi_set_string((uint8_t *)&data, arbitrary_valid_tag,
				     NULL),
		      (uint8_t *)&data);

	/* Validate no writes happened */
	zassert_mem_equal(&data, &unused_data, sizeof(data));
}

ZTEST(common_cbi, test_cbi_set_string)
{
	const char arbitrary_str[] = "hello cbi";
	enum cbi_data_tag arbitrary_valid_tag = CBI_TAG_SKU_ID;

	struct cbi_data_wrapper {
		struct cbi_data data;
		uint8_t value_arr[ARRAY_SIZE(arbitrary_str)];
	};
	struct cbi_data_wrapper cbi_data = { 0 };

	/* Set some provided memory then check values */
	uint8_t *addr_byte_after_store = cbi_set_string(
		(uint8_t *)&cbi_data, arbitrary_valid_tag, arbitrary_str);

	zassert_equal(cbi_data.data.tag, arbitrary_valid_tag);
	zassert_equal(cbi_data.data.size, ARRAY_SIZE(arbitrary_str));
	zassert_mem_equal(cbi_data.data.value, arbitrary_str,
			  cbi_data.data.size);

	uint32_t expected_added_memory =
		(ARRAY_SIZE(arbitrary_str) + sizeof(cbi_data.data));

	/* Validate that next address for write was set appropriately */
	zassert_equal_ptr(addr_byte_after_store - expected_added_memory,
			  &cbi_data.data);
}

ZTEST_SUITE(common_cbi, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
