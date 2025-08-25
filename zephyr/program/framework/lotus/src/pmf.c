/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cpu_power.h"

/**********************************************************
 * AMD GPU PMF table
 **********************************************************/
struct pmf_info AMD_GPU_AC_DC_PMF[] = {
	{240, EC_AC_BEST_PERFORMANCE, 1, {227, 145, 145, 145, 54}},
	{240, EC_AC_BALANCED, 2, {150, 120, 120, 120, 50}},
	{240, EC_AC_BEST_EFFICIENCY, 3, {150, 95, 95, 95, 45}},
	{180, EC_AC_BEST_PERFORMANCE, 4, {145, 120, 120, 120, 50}},
	{180, EC_AC_BALANCED, 5, {145, 95, 95, 95, 45}},
	{180, EC_AC_BEST_EFFICIENCY, 6, {145, 85, 85, 85, 40}},
	{140, EC_AC_BEST_PERFORMANCE, 7, {138, 95, 95, 95, 50}},
	{140, EC_AC_BALANCED, 8, {120, 85, 85, 85, 40}},
	{140, EC_AC_BEST_EFFICIENCY, 9, {120, 60, 60, 60, 30}},
	{100, EC_AC_BEST_PERFORMANCE, 10, {120, 85, 85, 85, 40}},
	{100, EC_AC_BALANCED, 11, {100, 60, 60, 60, 30}},
	{100, EC_AC_BEST_EFFICIENCY, 11, {100, 60, 60, 60, 30}},
	{80, EC_AC_BEST_PERFORMANCE, 12, {118, 60, 60, 60, 30}},
	{80, EC_AC_BALANCED, 13, {100, 60, 60, 60, 30}},
	{80, EC_AC_BEST_EFFICIENCY, 13, {100, 60, 60, 60, 30}},
	{1, EC_AC_BEST_PERFORMANCE, 14, {100, 60, 60, 60, 30}},
	{1, EC_AC_BALANCED, 14, {100, 60, 60, 60, 30}},
	{1, EC_AC_BEST_EFFICIENCY, 14, {100, 60, 60, 60, 30}},
};

struct pmf_info AMD_GPU_AC_ONLY_PMF[] = {
	{240, EC_AC_BEST_PERFORMANCE, 15, {145, 60, 60, 60, 30}},
	{240, EC_AC_BALANCED, 16, {145, 50, 50, 50, 30}},
	{240, EC_AC_BEST_EFFICIENCY, 17, {145, 30, 30, 30, 30}},
	{180, EC_AC_BEST_PERFORMANCE, 18, {75, 60, 60, 60, 30}},
	{180, EC_AC_BALANCED, 19, {75, 50, 50, 50, 30}},
	{180, EC_AC_BEST_EFFICIENCY, 20, {75, 30, 30, 30, 30}},
	{140, EC_AC_BEST_PERFORMANCE, 21, {75, 60, 60, 60, 30}},
	{140, EC_AC_BALANCED, 22, {75, 50, 50, 50, 30}},
	{140, EC_AC_BEST_EFFICIENCY, 23, {75, 30, 30, 30, 30}},
	{100, EC_AC_BEST_PERFORMANCE, 24, {75, 50, 50, 50, 30}},
	{100, EC_AC_BALANCED, 24, {75, 50, 50, 50, 30}},
	{100, EC_AC_BEST_EFFICIENCY, 25, {75, 30, 30, 30, 30}},
	{80, EC_AC_BEST_PERFORMANCE, 26, {72, 30, 30, 30, 30}},
	{80, EC_AC_BALANCED, 26, {72, 30, 30, 30, 30}},
	{80, EC_AC_BEST_EFFICIENCY, 26, {72, 30, 30, 30, 30}},
	{1, EC_AC_BEST_PERFORMANCE, 27, {54, 30, 30, 30, 30}},
	{1, EC_AC_BALANCED, 27, {54, 30, 30, 30, 30}},
	{1, EC_AC_BEST_EFFICIENCY, 27, {54, 30, 30, 30, 30}},
};

struct pmf_info AMD_GPU_DC_ONLY_PMF[] = {
	{0, EC_DC_BEST_PERFORMANCE, 28, {76, 60, 60, 60, 30}},
	{0, EC_DC_BALANCED, 29, {76, 50, 50, 50, 20}},
	{0, EC_DC_BEST_EFFICIENCY, 29, {76, 50, 50, 50, 20}},
	{0, EC_DC_BATTERY_SAVER, 30, {65, 20, 20, 20, 20}},
};

struct pmf_table AMD_GPU_PMF_TABLE[] = {
	[AC_DC_MODE] = {.info = AMD_GPU_AC_DC_PMF, .arr_size = sizeof(AMD_GPU_AC_DC_PMF)},
	[AC_ONLY_MODE] = {.info = AMD_GPU_AC_ONLY_PMF, .arr_size = sizeof(AMD_GPU_AC_ONLY_PMF)},
	[DC_ONLY_MODE] = {.info = AMD_GPU_DC_ONLY_PMF, .arr_size = sizeof(AMD_GPU_DC_ONLY_PMF)},
};

/**********************************************************
 * NV GPU PMF table
 **********************************************************/
struct pmf_info NV_GPU_AC_DC_PMF[] = {
	{240, EC_AC_BEST_PERFORMANCE, 66, {227, 65, 54, 45, 0}},
	{240, EC_AC_BALANCED, 67, {227, 65, 48, 40, 0}},
	{240, EC_AC_BEST_EFFICIENCY, 68, {221, 63, 42, 35, 0}},
	{180, EC_AC_BEST_PERFORMANCE, 69, {227, 65, 54, 45, 0}},
	{180, EC_AC_BALANCED, 70, {227, 65, 48, 40, 0}},
	{180, EC_AC_BEST_EFFICIENCY, 71, {221, 63, 42, 35, 0}},
	{140, EC_AC_BEST_PERFORMANCE, 72, {227, 65, 54, 45, 0}},
	{140, EC_AC_BALANCED, 73, {227, 65, 48, 40, 0}},
	{140, EC_AC_BEST_EFFICIENCY, 74, {221, 63, 42, 35, 0}},
	{100, EC_AC_BEST_PERFORMANCE, 75, {221, 63, 42, 35, 0}},
	{100, EC_AC_BALANCED, 76, {189, 54, 36, 30, 0}},
	{100, EC_AC_BEST_EFFICIENCY, 77, {176, 50, 34, 28, 0}},
	{80, EC_AC_BEST_PERFORMANCE, 78, {221, 63, 42, 35, 0}},
	{80, EC_AC_BALANCED, 79, {176, 50, 34, 28, 0}},
	{80, EC_AC_BEST_EFFICIENCY, 80, {158, 45, 30, 25, 0}},
	{1, EC_AC_BEST_PERFORMANCE, 81, {126, 36, 24, 20, 0}},
	{1, EC_AC_BALANCED, 82, {95, 27, 18, 15, 0}},
	{1, EC_AC_BEST_EFFICIENCY, 82, {95, 27, 18, 15, 0}},
};

struct pmf_info NV_GPU_AC_ONLY_PMF[] = {
	{240, EC_AC_BEST_PERFORMANCE, 83, {227, 65, 54, 45, 0}},
	{240, EC_AC_BALANCED, 84, {227, 65, 48, 40, 0}},
	{240, EC_AC_BEST_EFFICIENCY, 85, {221, 63, 42, 35, 0}},
	{180, EC_AC_BEST_PERFORMANCE, 86, {227, 65, 54, 45, 0}},
	{180, EC_AC_BALANCED, 87, {227, 65, 48, 40, 0}},
	{180, EC_AC_BEST_EFFICIENCY, 88, {221, 63, 42, 35, 0}},
	{140, EC_AC_BEST_PERFORMANCE, 89, {227, 65, 48, 40, 0}},
	{140, EC_AC_BALANCED, 90, {221, 63, 42, 35, 0}},
	{140, EC_AC_BEST_EFFICIENCY, 91, {189, 54, 36, 30, 0}},
	{100, EC_AC_BEST_PERFORMANCE, 92, {158, 45, 30, 25, 0}},
	{100, EC_AC_BALANCED, 93, {126, 36, 24, 20, 0}},
	{100, EC_AC_BEST_EFFICIENCY, 94, {95, 27, 18, 15, 0}},
	{80, EC_AC_BEST_PERFORMANCE, 95, {126, 36, 24, 20, 0}},
	{80, EC_AC_BALANCED, 96, {95, 27, 18, 15, 0}},
	{80, EC_AC_BEST_EFFICIENCY, 96, {95, 27, 18, 15, 0}},
	{1, EC_AC_BEST_PERFORMANCE, 97, {95, 27, 18, 15, 0}},
	{1, EC_AC_BALANCED, 97, {95, 27, 18, 15, 0}},
	{1, EC_AC_BEST_EFFICIENCY, 97, {95, 27, 18, 15, 0}},
};

struct pmf_info NV_GPU_DC_ONLY_PMF[] = {
	{0, EC_DC_BEST_PERFORMANCE, 98, {85, 45, 30, 30, 0}},
	{0, EC_DC_BALANCED, 99, {71, 38, 25, 20, 0}},
	{0, EC_DC_BEST_EFFICIENCY, 100, {66, 35, 23, 20, 0}},
	{0, EC_DC_BATTERY_SAVER, 101, {57, 30, 20, 20, 0}},
};

struct pmf_table NV_GPU_PMF_TABLE[] = {
	[AC_DC_MODE] = {.info = NV_GPU_AC_DC_PMF, .arr_size = sizeof(NV_GPU_AC_DC_PMF)},
	[AC_ONLY_MODE] = {.info = NV_GPU_AC_ONLY_PMF, .arr_size = sizeof(NV_GPU_AC_ONLY_PMF)},
	[DC_ONLY_MODE] = {.info = NV_GPU_DC_ONLY_PMF, .arr_size = sizeof(NV_GPU_DC_ONLY_PMF)},
};

/**********************************************************
 * UMA PMF table
 **********************************************************/
struct pmf_info UMA_AC_DC_PMF[] = {
	{240, EC_AC_BEST_PERFORMANCE, 31, {227, 65, 54, 45, 0}},
	{240, EC_AC_BALANCED, 32, {227, 65, 52, 43, 0}},
	{240, EC_AC_BEST_EFFICIENCY, 33, {227, 65, 48, 40, 0}},
	{180, EC_AC_BEST_PERFORMANCE, 34, {221, 63, 42, 35, 0}},
	{180, EC_AC_BALANCED, 35, {158, 45, 30, 25, 0}},
	{180, EC_AC_BEST_EFFICIENCY, 36, {145, 41, 28, 23, 0}},
	{140, EC_AC_BEST_PERFORMANCE, 37, {208, 59, 40, 33, 0}},
	{140, EC_AC_BALANCED, 38, {189, 54, 36, 30, 0}},
	{140, EC_AC_BEST_EFFICIENCY, 39, {176, 50, 34, 28, 0}},
	{100, EC_AC_BEST_PERFORMANCE, 40, {202, 58, 38, 32, 0}},
	{100, EC_AC_BALANCED, 41, {189, 54, 36, 30, 0}},
	{100, EC_AC_BEST_EFFICIENCY, 42, {176, 50, 34, 28, 0}},
	{80, EC_AC_BEST_PERFORMANCE, 43, {189, 54, 36, 30, 0}},
	{80, EC_AC_BALANCED, 44, {176, 50, 34, 28, 0}},
	{80, EC_AC_BEST_EFFICIENCY, 45, {158, 45, 30, 25, 0}},
	{1, EC_AC_BEST_PERFORMANCE, 46, {126, 36, 24, 20, 0}},
	{1, EC_AC_BALANCED, 47, {95, 27, 18, 15, 0}},
	{1, EC_AC_BEST_EFFICIENCY, 47, {95, 27, 18, 15, 0}},
};

struct pmf_info UMA_AC_ONLY_PMF[] = {
	{240, EC_AC_BEST_PERFORMANCE, 48, {227, 65, 54, 45, 0}},
	{240, EC_AC_BALANCED, 49, {221, 63, 42, 35, 0}},
	{240, EC_AC_BEST_EFFICIENCY, 50, {158, 45, 30, 25, 0}},
	{180, EC_AC_BEST_PERFORMANCE, 51, {227, 65, 54, 45, 0}},
	{180, EC_AC_BALANCED, 52, {221, 63, 42, 35, 0}},
	{180, EC_AC_BEST_EFFICIENCY, 53, {158, 45, 30, 25, 0}},
	{140, EC_AC_BEST_PERFORMANCE, 54, {227, 65, 50, 42, 0}},
	{140, EC_AC_BALANCED, 55, {221, 63, 42, 35, 0}},
	{140, EC_AC_BEST_EFFICIENCY, 56, {158, 45, 30, 25, 0}},
	{100, EC_AC_BEST_PERFORMANCE, 57, {158, 45, 30, 25, 0}},
	{100, EC_AC_BALANCED, 58, {145, 41, 28, 23, 0}},
	{100, EC_AC_BEST_EFFICIENCY, 59, {126, 36, 24, 20, 0}},
	{80, EC_AC_BEST_PERFORMANCE, 60, {95, 27, 18, 15, 0}},
	{80, EC_AC_BALANCED, 60, {95, 27, 18, 15, 0}},
	{80, EC_AC_BEST_EFFICIENCY, 60, {95, 27, 18, 15, 0}},
	{1, EC_AC_BEST_PERFORMANCE, 61, {95, 27, 18, 15, 0}},
	{1, EC_AC_BALANCED, 61, {95, 27, 18, 15, 0}},
	{1, EC_AC_BEST_EFFICIENCY, 61, {95, 27, 18, 15, 0}},
};

struct pmf_info UMA_DC_ONLY_PMF[] = {
	{0, EC_DC_BEST_PERFORMANCE, 62, {76, 60, 40, 30, 0}},
	{0, EC_DC_BALANCED, 63, {76, 53, 35, 20, 0}},
	{0, EC_DC_BEST_EFFICIENCY, 64, {76, 45, 30, 20, 0}},
	{0, EC_DC_BATTERY_SAVER, 65, {65, 30, 20, 20, 0}},
};

struct pmf_table UMA_PMF_TABLE[] = {
	[AC_DC_MODE] = {.info = UMA_AC_DC_PMF, .arr_size = sizeof(UMA_AC_DC_PMF)},
	[AC_ONLY_MODE] = {.info = UMA_AC_ONLY_PMF, .arr_size = sizeof(UMA_AC_ONLY_PMF)},
	[DC_ONLY_MODE] = {.info = UMA_DC_ONLY_PMF, .arr_size = sizeof(UMA_DC_ONLY_PMF)},
};

void pmf_table_switch_by_cpu_type(void)
{
	/*Lotus only use PHOENIX CPU, no need to change pmf table*/
}
