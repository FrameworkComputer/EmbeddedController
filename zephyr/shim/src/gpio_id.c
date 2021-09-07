/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <devicetree.h>
#include "gpio.h"
#include "util.h"

#define IS_BOARD_COMPATIBLE \
	DT_NODE_HAS_COMPAT(DT_PATH(board), cros_ec_gpio_id)
#define IS_SKU_COMPATIBLE \
	DT_NODE_HAS_COMPAT(DT_PATH(sku), cros_ec_gpio_id)

#define CONVERT_NUMERAL_SYSTEM_EVAL(system, bits, nbits) \
	system##_from_bits(bits, nbits)
#define CONVERT_NUMERAL_SYSTEM(system, bits, nbits) \
	CONVERT_NUMERAL_SYSTEM_EVAL(system, bits, nbits)

#define READ_PIN_FROM_PHANDLE(node_id, prop, idx) \
	gpio_get_ternary(GPIO_SIGNAL(DT_PHANDLE_BY_IDX(node_id, prop, idx))),

#if DT_NODE_EXISTS(DT_PATH(sku)) && IS_SKU_COMPATIBLE

__override uint32_t board_get_sku_id(void)
{
	static uint32_t sku_id = (uint32_t)-1;

	if (sku_id == (uint32_t)-1) {
		int bits[] = {
			DT_FOREACH_PROP_ELEM(DT_PATH(sku),
					bits,
					READ_PIN_FROM_PHANDLE)
		};

		if (sizeof(bits) == 0)
			return (uint32_t)-1;

		sku_id = CONVERT_NUMERAL_SYSTEM(DT_STRING_TOKEN(DT_PATH(sku),
								system),
						bits, ARRAY_SIZE(bits));
	}

	return sku_id;
}

#endif

#if DT_NODE_EXISTS(DT_PATH(board)) && IS_BOARD_COMPATIBLE

__override int board_get_version(void)
{
	static int board_version = -1;

	if (board_version == -1) {
		int bits[] = {
			DT_FOREACH_PROP_ELEM(DT_PATH(board),
					bits,
					READ_PIN_FROM_PHANDLE)
		};

		if (sizeof(bits) == 0)
			return -1;

		board_version = CONVERT_NUMERAL_SYSTEM(
			DT_STRING_TOKEN(DT_PATH(board), system), bits,
			ARRAY_SIZE(bits));
	}

	return board_version;
}

#endif
