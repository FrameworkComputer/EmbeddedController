/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include "usbc_ppc.h"
#include "usbc/ppc_nx20p348x.h"
#include "usbc/ppc_rt1739.h"
#include "usbc/ppc_sn5s330.h"
#include "usbc/ppc_syv682x.h"
#include "usbc/ppc.h"

#if DT_HAS_COMPAT_STATUS_OKAY(NX20P348X_COMPAT) ||      \
	DT_HAS_COMPAT_STATUS_OKAY(RT1739_PPC_COMPAT) || \
	DT_HAS_COMPAT_STATUS_OKAY(SN5S330_COMPAT) ||    \
	DT_HAS_COMPAT_STATUS_OKAY(SYV682X_COMPAT)

#define PPC_CHIP_ENTRY(usbc_id, ppc_id, config_fn) \
	[USBC_PORT_NEW(usbc_id)] = config_fn(ppc_id)

#define CHECK_COMPAT(compat, usbc_id, ppc_id, config_fn) \
	COND_CODE_1(DT_NODE_HAS_COMPAT(ppc_id, compat),  \
		    (PPC_CHIP_ENTRY(usbc_id, ppc_id, config_fn)), ())

#define PPC_CHIP_FIND(usbc_id, ppc_id)                                      \
	CHECK_COMPAT(NX20P348X_COMPAT, usbc_id, ppc_id, PPC_CHIP_NX20P348X) \
	CHECK_COMPAT(RT1739_PPC_COMPAT, usbc_id, ppc_id, PPC_CHIP_RT1739)   \
	CHECK_COMPAT(SN5S330_COMPAT, usbc_id, ppc_id, PPC_CHIP_SN5S330)     \
	CHECK_COMPAT(SYV682X_COMPAT, usbc_id, ppc_id, PPC_CHIP_SYV682X)

#define PPC_CHIP(usbc_id)                           \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, ppc), \
		    (PPC_CHIP_FIND(usbc_id, DT_PHANDLE(usbc_id, ppc))), ())

#define PPC_CHIP_ALT(usbc_id)                                               \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, ppc_alt),                     \
		    (PPC_CHIP_FIND(usbc_id, DT_PHANDLE(usbc_id, ppc_alt))), \
		    ())

struct ppc_config_t ppc_chips[] = { DT_FOREACH_STATUS_OKAY(named_usbc_port,
							   PPC_CHIP) };

unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

struct ppc_config_t ppc_chips_alt[] = { DT_FOREACH_STATUS_OKAY(named_usbc_port,
							       PPC_CHIP_ALT) };

#endif /* #if DT_HAS_COMPAT_STATUS_OKAY */
