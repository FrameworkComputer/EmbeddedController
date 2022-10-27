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
	struct cbi_data data = { 0 };
	enum cbi_data_tag arbitrary_valid_tag = CBI_TAG_SKU_ID;
	const char *arbitrary_str = "hello cbi";

	uint8_t *addr_byte_after_store = cbi_set_string(
		(uint8_t *)&data, arbitrary_valid_tag, arbitrary_str);

	zassert_equal(data.tag, arbitrary_valid_tag);
	zassert_equal(data.size, strlen(arbitrary_str) + 1);
	zassert_mem_equal(data.value, arbitrary_str, data.size);
	zassert_equal(addr_byte_after_store -
			      (strlen(arbitrary_str) + 1 + sizeof(data)),
		      (uint8_t *)&data);
}

ZTEST_SUITE(common_cbi, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
