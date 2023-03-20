/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usbc/ppc.h"
#include "usbc/ppc_aoz1380.h"
#include "usbc/ppc_nx20p348x.h"
#include "usbc/ppc_rt1739.h"
#include "usbc/ppc_sn5s330.h"
#include "usbc/ppc_syv682x.h"
#include "usbc_ppc.h"

#include <zephyr/devicetree.h>

#if DT_HAS_COMPAT_STATUS_OKAY(AOZ1380_COMPAT) ||          \
	DT_HAS_COMPAT_STATUS_OKAY(NX20P348X_COMPAT) ||    \
	DT_HAS_COMPAT_STATUS_OKAY(RT1739_PPC_COMPAT) ||   \
	DT_HAS_COMPAT_STATUS_OKAY(SN5S330_COMPAT) ||      \
	DT_HAS_COMPAT_STATUS_OKAY(SN5S330_EMUL_COMPAT) || \
	DT_HAS_COMPAT_STATUS_OKAY(SYV682X_COMPAT) ||      \
	DT_HAS_COMPAT_STATUS_OKAY(SYV682X_EMUL_COMPAT)

#define PPC_CHIP_ENTRY(usbc_id, ppc_id, config_fn) \
	[USBC_PORT_NEW(usbc_id)] = config_fn(ppc_id),

#define CHECK_COMPAT(compat, usbc_id, ppc_id, config_fn) \
	COND_CODE_1(DT_NODE_HAS_COMPAT(ppc_id, compat),  \
		    (PPC_CHIP_ENTRY(usbc_id, ppc_id, config_fn)), ())

#define PPC_CHIP_FIND(usbc_id, ppc_id)                                       \
	CHECK_COMPAT(AOZ1380_COMPAT, usbc_id, ppc_id, PPC_CHIP_AOZ1380)      \
	CHECK_COMPAT(NX20P348X_COMPAT, usbc_id, ppc_id, PPC_CHIP_NX20P348X)  \
	CHECK_COMPAT(RT1739_PPC_COMPAT, usbc_id, ppc_id, PPC_CHIP_RT1739)    \
	CHECK_COMPAT(SN5S330_COMPAT, usbc_id, ppc_id, PPC_CHIP_SN5S330)      \
	CHECK_COMPAT(SN5S330_EMUL_COMPAT, usbc_id, ppc_id, PPC_CHIP_SN5S330) \
	CHECK_COMPAT(SYV682X_COMPAT, usbc_id, ppc_id, PPC_CHIP_SYV682X)      \
	CHECK_COMPAT(SYV682X_EMUL_COMPAT, usbc_id, ppc_id, PPC_CHIP_SYV682X)

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

#define PPC_ALT_DEFINITION(node_id, config_fn) \
	const struct ppc_config_t PPC_ALT_NAME_GET(node_id) = config_fn(node_id)

#define PPC_ALT_DEFINE(node_id, config_fn)          \
	COND_CODE_1(DT_PROP_OR(node_id, is_alt, 0), \
		    (PPC_ALT_DEFINITION(node_id, config_fn);), ())

/*
 * Define a global struct ppc_config_t for every PPC node in the tree with the
 * "is-alt" property set.
 */
DT_FOREACH_STATUS_OKAY_VARGS(AOZ1380_COMPAT, PPC_ALT_DEFINE, PPC_CHIP_AOZ1380)
DT_FOREACH_STATUS_OKAY_VARGS(NX20P348X_COMPAT, PPC_ALT_DEFINE,
			     PPC_CHIP_NX20P348X)
DT_FOREACH_STATUS_OKAY_VARGS(RT1739_PPC_COMPAT, PPC_ALT_DEFINE, PPC_CHIP_RT1739)
DT_FOREACH_STATUS_OKAY_VARGS(SN5S330_COMPAT, PPC_ALT_DEFINE, PPC_CHIP_SN5S330)
DT_FOREACH_STATUS_OKAY_VARGS(SN5S330_EMUL_COMPAT, PPC_ALT_DEFINE,
			     PPC_CHIP_SN5S330)
DT_FOREACH_STATUS_OKAY_VARGS(SYV682X_COMPAT, PPC_ALT_DEFINE, PPC_CHIP_SYV682X)
DT_FOREACH_STATUS_OKAY_VARGS(SYV682X_EMUL_COMPAT, PPC_ALT_DEFINE,
			     PPC_CHIP_SYV682X)

#endif /* #if DT_HAS_COMPAT_STATUS_OKAY */
