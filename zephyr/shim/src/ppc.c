/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include "usbc_ppc.h"
#include "usbc/ppc_rt1739.h"
#include "usbc/ppc_sn5s330.h"
#include "usbc/ppc_syv682x.h"
#include "usbc/ppc.h"

#if DT_HAS_COMPAT_STATUS_OKAY(RT1739_PPC_COMPAT) || \
	DT_HAS_COMPAT_STATUS_OKAY(SN5S330_COMPAT) || \
	DT_HAS_COMPAT_STATUS_OKAY(SYV682X_COMPAT)

#define PPC_CHIP_PRIM(id, fn)                                \
	COND_CODE_1(DT_NODE_HAS_PROP(id, alternate_for), (), \
		    (PPC_CHIP_ELE_PRIM(id, fn)))

#define PPC_CHIP_ALT(id, fn)                             \
	COND_CODE_1(DT_NODE_HAS_PROP(id, alternate_for), \
		    (PPC_CHIP_ELE_ALT(id, fn)), ())

#define PPC_CHIP_ELE_PRIM(id, fn) [USBC_PORT(id)] = fn(id)

#define PPC_CHIP_ELE_ALT(id, fn) [PPC_ID(id)] = fn(id)

/* Power Path Controller */
struct ppc_config_t ppc_chips[] = {
	DT_FOREACH_STATUS_OKAY_VARGS(RT1739_PPC_COMPAT, PPC_CHIP_PRIM,
				     PPC_CHIP_RT1739)
	DT_FOREACH_STATUS_OKAY_VARGS(SN5S330_COMPAT, PPC_CHIP_PRIM,
				     PPC_CHIP_SN5S330)
	DT_FOREACH_STATUS_OKAY_VARGS(SYV682X_COMPAT, PPC_CHIP_PRIM,
				     PPC_CHIP_SYV682X)
};
unsigned int ppc_cnt = ARRAY_SIZE(ppc_chips);

/* Alt Power Path Controllers */
struct ppc_config_t ppc_chips_alt[] = {
	DT_FOREACH_STATUS_OKAY_VARGS(RT1739_PPC_COMPAT, PPC_CHIP_ALT,
				     PPC_CHIP_RT1739)
	DT_FOREACH_STATUS_OKAY_VARGS(SN5S330_COMPAT, PPC_CHIP_ALT,
				     PPC_CHIP_SN5S330)
	DT_FOREACH_STATUS_OKAY_VARGS(SYV682X_COMPAT, PPC_CHIP_ALT,
				     PPC_CHIP_SYV682X)
};

#endif /* #if DT_HAS_COMPAT_STATUS_OKAY */
