/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ADC for checking BOARD ID.
 */

#include "adc.h"
#include "board_adc.h"
#include "console.h"
#include "system.h"
#include "hooks.h"

#define CPRINTS(format, args...) cprints(CC_GPIO, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_GPIO, format, ## args)

/*
 * PLATFORM_EC_ADC_RESOLUTION default 10 bit
 *
 * +------------------+-----------+--------------+---------+----------------------+
 * |  BOARD VERSION   |  voltage  |  main board  |   GPU   |     Input module     |
 * +------------------+-----------+--------------+---------+----------------------+
 * | BOARD_VERSION_0  |  100  mv  |    Unused    |         |       Reserved       |
 * | BOARD_VERSION_1  |  310  mv  |    Unused    |         |       Reserved       |
 * | BOARD_VERSION_2  |  520  mv  |    Unused    |         |       Reserved       |
 * | BOARD_VERSION_3  |  720  mv  |    Unused    |         |       Reserved       |
 * | BOARD_VERSION_4  |  930  mv  |    EVT1      |         |       Reserved       |
 * | BOARD_VERSION_5  |  1130 mv  |    Unused    |         |       Reserved       |
 * | BOARD_VERSION_6  |  1340 mv  |    Unused    |         |       Reserved       |
 * | BOARD_VERSION_7  |  1550 mv  |    DVT1      |         |       Reserved       |
 * | BOARD_VERSION_8  |  1750 mv  |    DVT2      |         |    Generic A size    |
 * | BOARD_VERSION_9  |  1960 mv  |    PVT       |         |    Generic B size    |
 * | BOARD_VERSION_10 |  2170 mv  |    MP        |         |    Generic C size    |
 * | BOARD_VERSION_11 |  2370 mv  |    Unused    | RID_0   |    10 Key B size     |
 * | BOARD_VERSION_12 |  2580 mv  |    Unused    | RID_0,1 |       Keyboard       |
 * | BOARD_VERSION_13 |  2780 mv  |    Unused    | RID_0   |       Touchpad       |
 * | BOARD_VERSION_14 |  2990 mv  |    Unused    |         |       Reserved       |
 * | BOARD_VERSION_15 |  3300 mv  |    Unused    |         |    Not installed     |
 * +------------------+-----------+--------------+---------+----------------------+
 */

struct {
	enum board_version_t version;
	int min_thresh_mv;
	int max_thresh_mv;
} const board_versions[] = {
	{ BOARD_VERSION_0, 0, 110 },
	{ BOARD_VERSION_1, 113, 234 },
	{ BOARD_VERSION_2, 239, 360 },
	{ BOARD_VERSION_3, 369, 491 },
	{ BOARD_VERSION_4, 527, 648},
	{ BOARD_VERSION_5, 722, 843 },
	{ BOARD_VERSION_6, 844, 965 },
	{ BOARD_VERSION_7, 972, 1093 },
	{ BOARD_VERSION_8, 1259, 1380 },
	{ BOARD_VERSION_9, 1439, 1560 },
	{ BOARD_VERSION_10, 1580, 1710 },
	{ BOARD_VERSION_11, 1900, 2040 },
	{ BOARD_VERSION_12, 2060, 2195 },
	{ BOARD_VERSION_13, 2250, 2600 },
	{ BOARD_VERSION_14, 2645, 2766 },
	{ BOARD_VERSION_15, 2770, 3300 },
};
BUILD_ASSERT(ARRAY_SIZE(board_versions) == BOARD_VERSION_COUNT);

enum board_version_t get_hardware_id(enum adc_channel channel)
{
	int version = BOARD_VERSION_UNKNOWN;
	int mv;
	int i;

	mv = adc_read_channel(channel);

	if (mv < 0) {
		CPRINTS("ADC could not read (%d)", mv);
		return BOARD_VERSION_UNKNOWN;
	}

	for (i = 0; i < BOARD_VERSION_COUNT; i++)
		if (mv >= board_versions[i].min_thresh_mv && mv <= board_versions[i].max_thresh_mv) {
			version = board_versions[i].version;
			return version;
		}

	return version;
}

__override int board_get_version(void)
{
	static int version = BOARD_VERSION_UNKNOWN;

	if (version != BOARD_VERSION_UNKNOWN)
		return version;

	version = get_hardware_id(ADC_MAIN_BOARD_ID);

	return version;
}

