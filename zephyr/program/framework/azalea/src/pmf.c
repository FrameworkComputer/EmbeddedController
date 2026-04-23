/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cpu_power.h"
#include "customized_shared_memory.h"

/**********************************************************
 * PMF table
 **********************************************************/
struct pmf_info AC_DC_PMF[] = {
	{1, EC_AC_BEST_PERFORMANCE, 1, {120, 53, 35, 30}},
	{1, EC_AC_BALANCED, 2, {120, 51, 33, 25}},
	{1, EC_AC_BEST_EFFICIENCY, 3, {120, 30, 25, 15}},
};

struct pmf_info AC_ONLY_PMF[] = {
	{100, EC_AC_BEST_PERFORMANCE, 4, {80, 53, 35, 30}},
	{100, EC_AC_BALANCED, 5, {80, 51, 33, 25}},
	{100, EC_AC_BEST_EFFICIENCY, 6, {80, 30, 25, 15}},
	{1, EC_AC_BEST_PERFORMANCE, 7, {80, 35, 33, 30}},
	{1, EC_AC_BALANCED, 8, {80, 35, 33, 25}},
	{1, EC_AC_BEST_EFFICIENCY, 9, {80, 30, 25, 15}},
};

struct pmf_info DC_ONLY_55W_PMF[] = {
	{0, EC_DC_BEST_PERFORMANCE, 10, {80, 40, 35, 30}},
	{0, EC_DC_BALANCED, 11, {80, 40, 33, 25}},
	{0, EC_DC_BEST_EFFICIENCY, 12, {80, 30, 25, 15}},
	{0, EC_DC_BATTERY_SAVER, 13, {55, 30, 15, 15}},
};

struct pmf_info DC_ONLY_61W_PMF[] = {
	{0, EC_DC_BEST_PERFORMANCE, 14, {80, 46, 35, 30}},
	{0, EC_DC_BALANCED, 15, {80, 46, 33, 25}},
	{0, EC_DC_BEST_EFFICIENCY, 16, {80, 30, 25, 15}},
	{0, EC_DC_BATTERY_SAVER, 17, {61, 30, 15, 15}},
};

struct pmf_info DC_ONLY_74W_PMF[] = {
	{0, EC_DC_BEST_PERFORMANCE, 18, {90, 53, 35, 30}},
	{0, EC_DC_BALANCED, 19, {90, 51, 33, 25}},
	{0, EC_DC_BEST_EFFICIENCY, 20, {90, 30, 25, 15}},
	{0, EC_DC_BATTERY_SAVER, 21, {74, 30, 15, 15}},
};

struct pmf_table PMF_TABLE[] = {
	[AC_DC_MODE] = {.info = AC_DC_PMF, .arr_size = sizeof(AC_DC_PMF)},
	[AC_ONLY_MODE] = {.info = AC_ONLY_PMF, .arr_size = sizeof(AC_ONLY_PMF)},
	[DC_ONLY_MODE] = {.info = DC_ONLY_55W_PMF, .arr_size = sizeof(DC_ONLY_55W_PMF)},
};
