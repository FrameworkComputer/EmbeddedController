/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_ADC_H__
#define __BOARD_ADC_H__

#include "adc.h"

enum board_version_t {
	BOARD_VERSION_UNKNOWN = -1,
	BOARD_VERSION_0,
	BOARD_VERSION_1,
	BOARD_VERSION_2,
	BOARD_VERSION_3,
	BOARD_VERSION_4,
	BOARD_VERSION_5,
	BOARD_VERSION_6,
	BOARD_VERSION_7,
	BOARD_VERSION_8,
	BOARD_VERSION_9,
	BOARD_VERSION_10,
	BOARD_VERSION_11,
	BOARD_VERSION_12,
	BOARD_VERSION_13,
	BOARD_VERSION_14,
	BOARD_VERSION_15,
	BOARD_VERSION_COUNT,
};

enum board_version_t get_hardware_id(enum adc_channel channel);

#endif /* __BOARD_LED_H__ */