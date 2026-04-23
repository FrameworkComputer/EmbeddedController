/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CPU_POWER_H__
#define __CROS_EC_CPU_POWER_H__

#include "customized_shared_memory.h"

enum power_supply_type {
	AC_DC_MODE,
	AC_ONLY_MODE,
	DC_ONLY_MODE,
};

struct pmf_data {
	uint8_t P3T;
	uint8_t fPPT;
	uint8_t sPPT;
	uint8_t SPL;
} __packed;

/*
 * The active power should be the lower boundary of the watt interval.
 * ex: intervel 240w~180W, the active_power is 180.
 */
struct pmf_info {
	uint16_t active_power;
	enum power_slide_mode slide;
	uint16_t table_num;
	struct pmf_data pmf;
} __packed;

struct pmf_table {
	struct pmf_info *info;
	uint8_t arr_size;
} __packed;

extern struct pmf_info DC_ONLY_55W_PMF[];
extern struct pmf_info DC_ONLY_61W_PMF[];
extern struct pmf_info DC_ONLY_74W_PMF[];
extern struct pmf_table PMF_TABLE[];

#endif /* __CROS_EC_CPU_POWER_H__ */
