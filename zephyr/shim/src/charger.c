/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include <devicetree.h>
#include "charger/chg_bq25710.h"
#include "charger/chg_isl923x.h"
#include "charger/chg_isl9241.h"
#include "charger/chg_rt9490.h"
#include "charger/chg_sm5803.h"

#if DT_HAS_COMPAT_STATUS_OKAY(ISL923X_CHG_COMPAT) ||     \
	DT_HAS_COMPAT_STATUS_OKAY(ISL9241_CHG_COMPAT) || \
	DT_HAS_COMPAT_STATUS_OKAY(RT9490_CHG_COMPAT) ||  \
	DT_HAS_COMPAT_STATUS_OKAY(SM5803_CHG_COMPAT)

#define CHG_CHIP(id, fn) [DT_REG_ADDR(DT_PARENT(id))] = fn(id)

/* Charger chips */
#ifndef CONFIG_PLATFORM_EC_CHARGER_RUNTIME_CONFIG
const struct charger_config_t chg_chips[] = {
#else
struct charger_config_t chg_chips[] = {
#endif
	DT_FOREACH_STATUS_OKAY_VARGS(BQ25710_CHG_COMPAT, CHG_CHIP,
				     CHG_CONFIG_BQ25710)
	DT_FOREACH_STATUS_OKAY_VARGS(ISL923X_CHG_COMPAT, CHG_CHIP,
				     CHG_CONFIG_ISL923X)
	DT_FOREACH_STATUS_OKAY_VARGS(ISL9241_CHG_COMPAT, CHG_CHIP,
				     CHG_CONFIG_ISL9241)
	DT_FOREACH_STATUS_OKAY_VARGS(RT9490_CHG_COMPAT, CHG_CHIP,
				     CHG_CONFIG_RT9490)
	DT_FOREACH_STATUS_OKAY_VARGS(SM5803_CHG_COMPAT, CHG_CHIP,
				     CHG_CONFIG_SM5803)
};

#ifdef CONFIG_PLATFORM_EC_CHARGER_SINGLE_CHIP
BUILD_ASSERT(ARRAY_SIZE(chg_chips) == 1,
	"For the CHARGER_SINGLE_CHIP config, the number of defined charger "
	"chips must equal 1.");
#else
BUILD_ASSERT(ARRAY_SIZE(chg_chips) == CONFIG_USB_PD_PORT_MAX_COUNT,
	"For the OCPC config, the number of defined charger chips must equal "
	"the number of USB-C ports.");
#endif

#endif /* #if DT_HAS_COMPAT_STATUS_OKAY */
