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
 * | BOARD_VERSION_0  |  0    mv  |    Unused    |         |       Reserved       |
 * | BOARD_VERSION_1  |  173  mv  |    Unused    |         |       Reserved       |
 * | BOARD_VERSION_2  |  300  mv  |    Unused    |         |       Reserved       |
 * | BOARD_VERSION_3  |  430  mv  |    Unused    |         |       Reserved       |
 * | BOARD_VERSION_4  |  588  mv  |    EVT1      |         |       Reserved       |
 * | BOARD_VERSION_5  |  783  mv  |    Unused    |         |       Reserved       |
 * | BOARD_VERSION_6  |  905  mv  |    Unused    |         |       Reserved       |
 * | BOARD_VERSION_7  |  1033 mv  |    DVT1      |         |       Reserved       |
 * | BOARD_VERSION_8  |  1320 mv  |    DVT2      |         |    Generic A size    |
 * | BOARD_VERSION_9  |  1500 mv  |    PVT       |         |    Generic B size    |
 * | BOARD_VERSION_10 |  1650 mv  |    MP        |         |    Generic C size    |
 * | BOARD_VERSION_11 |  1980 mv  |    Unused    | RID_0   |    10 Key B size     |
 * | BOARD_VERSION_12 |  2135 mv  |    Unused    | RID_0,1 |       Keyboard       |
 * | BOARD_VERSION_13 |  2500 mv  |    Unused    | RID_0   |       Touchpad       |
 * | BOARD_VERSION_14 |  2706 mv  |    Unused    |         |       Reserved       |
 * | BOARD_VERSION_15 |  2813 mv  |    Unused    |         |    Not installed     |
 * +------------------+-----------+--------------+---------+----------------------+
 */

struct {
	enum board_version_t version;
	int thresh_mv;
} const board_versions[] = {
	{ BOARD_VERSION_0, 85  },
	{ BOARD_VERSION_1, 233 },
	{ BOARD_VERSION_2, 360 },
	{ BOARD_VERSION_3, 492 },
	{ BOARD_VERSION_4, 649 },
	{ BOARD_VERSION_5, 844 },
	{ BOARD_VERSION_6, 965 },
	{ BOARD_VERSION_7, 1094 },
	{ BOARD_VERSION_8, 1380 },
	{ BOARD_VERSION_9, 1562 },
	{ BOARD_VERSION_10, 1710 },
	{ BOARD_VERSION_11, 2040 },
	{ BOARD_VERSION_12, 2197 },
	{ BOARD_VERSION_13, 2557 },
	{ BOARD_VERSION_14, 2766 },
	{ BOARD_VERSION_15, 2814 },
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
		if (mv < board_versions[i].thresh_mv) {
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

