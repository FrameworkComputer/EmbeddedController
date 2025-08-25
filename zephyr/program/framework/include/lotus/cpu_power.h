/* Copyright 2022 The ChromiumOS Authors
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
	uint8_t APU_only_sPPT;
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

extern struct pmf_table AMD_GPU_PMF_TABLE[];
extern struct pmf_table NV_GPU_PMF_TABLE[];
extern struct pmf_table UMA_PMF_TABLE[];

/**
 * Check if safety level(LEVEL_TYPEC_1_5A) is triggered.
 *
 * @param none
 * @return true:safety level(LEVEL_TYPEC_1_5A) is triggered
 *              all sink port need to be forced to 1.5A
 *         false:safety level(LEVEL_TYPEC_1_5A) is not trigger
 */
bool safety_force_typec_1_5A(void);

void pmf_table_switch_by_cpu_type(void);

#define EC_CUSTOMIZED_MEMMAP_CPU_TYPE 0x19A
enum gpu_type {
	CPU_TYPE_INITIALIZING = 0,
	CPU_PHOENIX = 1,
	CPU_KRACKAN = 2,
	CPU_STRIXPOINT = 3,
};

#endif /* __CROS_EC_CPU_POWER_H__ */
