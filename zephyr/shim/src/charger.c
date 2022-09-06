/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include <zephyr/devicetree.h>
#include "charger/chg_bq25710.h"
#include "charger/chg_isl923x.h"
#include "charger/chg_isl9241.h"
#include "charger/chg_rt9490.h"
#include "charger/chg_sm5803.h"
#include "usbc/utils.h"

#define CHG_CHIP_ENTRY(usbc_id, chg_id, config_fn) \
	[USBC_PORT_NEW(usbc_id)] = config_fn(chg_id)

#define CHECK_COMPAT(compat, usbc_id, chg_id, config_fn) \
	COND_CODE_1(DT_NODE_HAS_COMPAT(chg_id, compat),  \
		    (CHG_CHIP_ENTRY(usbc_id, chg_id, config_fn)), ())

#define CHG_CHIP_FIND(usbc_id, chg_id)                                         \
	CHECK_COMPAT(BQ25710_CHG_COMPAT, usbc_id, chg_id, CHG_CONFIG_BQ25710)  \
	CHECK_COMPAT(ISL923X_CHG_COMPAT, usbc_id, chg_id, CHG_CONFIG_ISL923X)  \
	CHECK_COMPAT(ISL923X_EMUL_COMPAT, usbc_id, chg_id, CHG_CONFIG_ISL923X) \
	CHECK_COMPAT(ISL9241_CHG_COMPAT, usbc_id, chg_id, CHG_CONFIG_ISL9241)  \
	CHECK_COMPAT(RT9490_CHG_COMPAT, usbc_id, chg_id, CHG_CONFIG_RT9490)    \
	CHECK_COMPAT(RT9490_EMUL_COMPAT, usbc_id, chg_id, CHG_CONFIG_RT9490)   \
	CHECK_COMPAT(SM5803_CHG_COMPAT, usbc_id, chg_id, CHG_CONFIG_SM5803)

#define CHG_CHIP(usbc_id)                           \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, chg), \
		    (CHG_CHIP_FIND(usbc_id, DT_PHANDLE(usbc_id, chg))), ())

#define CHG_CHIP_ALT(usbc_id)                                               \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, chg_alt),                     \
		    (CHG_CHIP_FIND(usbc_id, DT_PHANDLE(usbc_id, chg_alt))), \
		    ())

#define MAYBE_CONST \
	COND_CODE_1(CONFIG_PLATFORM_EC_CHARGER_RUNTIME_CONFIG, (), (const))

/* Charger chips */
MAYBE_CONST struct charger_config_t chg_chips[] = { DT_FOREACH_STATUS_OKAY(
	named_usbc_port, CHG_CHIP) };

/* Alternate options */
const struct charger_config_t chg_chips_alt[] = { DT_FOREACH_STATUS_OKAY(
	named_usbc_port, CHG_CHIP_ALT) };

#ifdef CONFIG_PLATFORM_EC_CHARGER_SINGLE_CHIP
BUILD_ASSERT(ARRAY_SIZE(chg_chips) == 1,
	     "For the CHARGER_SINGLE_CHIP config, the number of defined charger "
	     "chips must equal 1.");
#else
BUILD_ASSERT(
	ARRAY_SIZE(chg_chips) == CONFIG_USB_PD_PORT_MAX_COUNT,
	"For the OCPC config, the number of defined charger chips must equal "
	"the number of USB-C ports.");
#endif
