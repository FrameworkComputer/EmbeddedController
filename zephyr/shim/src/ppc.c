/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usbc/ppc.h"
#include "usbc/ppc_aoz1380.h"
#include "usbc/ppc_ktu1125.h"
#include "usbc/ppc_nx20p348x.h"
#include "usbc/ppc_rt1739.h"
#include "usbc/ppc_sn5s330.h"
#include "usbc/ppc_syv682x.h"
#include "usbc_ppc.h"

#include <zephyr/devicetree.h>

#define PPC_CHIP_ENTRY(usbc_id, ppc_id, config_fn) \
	[USBC_PORT_NEW(usbc_id)] = config_fn(ppc_id),

#define CHECK_COMPAT(compat, usbc_id, ppc_id, config_fn) \
	COND_CODE_1(DT_NODE_HAS_COMPAT(ppc_id, compat),  \
		    (PPC_CHIP_ENTRY(usbc_id, ppc_id, config_fn)), ())

/**
 * @param driver Tuple containing the PPC (compatible, config) pair.
 * @param nodes Tuple containing the (usbc_node_id, ppc_node_id) pair
 */
#define CHECK_COMPAT_HELPER(driver, nodes)                                     \
	CHECK_COMPAT(USBC_DRIVER_GET_COMPAT(driver), NODES_GET_USBC_ID(nodes), \
		     NODES_GET_PROP_ID(nodes), USBC_DRIVER_GET_CONFIG(driver))

#define PPC_CHIP_FIND(usbc_id, ppc_id)                                 \
	FOR_EACH_FIXED_ARG(CHECK_COMPAT_HELPER, (), (usbc_id, ppc_id), \
			   PPC_DRIVERS)

/*
 * This macro gets invoked for every driver in the PPC_DRIVERS list.
 * If the passed in ppc node contains the specified compat string, then
 * this macro returns 1.  Otherwise the macro returns nothing (EMPTY).
 */
#define PPC_HAS_COMPAT(compat, ppc) \
	IF_ENABLED(DT_NODE_HAS_COMPAT(ppc, compat), 1)

/*
 * Verify the compatible property of a PPC node is valid.
 *
 * Call PPC_HAS_COMPAT() for all PPC compatible strings listed in the
 * PPC_DRIVERS list.  If the resulting list is empty, then there was no
 * matching PPC driver found and this macro generates a build error.
 */
#define PPC_PROP_COMPATIBLE_VERIFY(ppc)                                        \
	IF_ENABLED(                                                            \
		IS_EMPTY(FOR_EACH_FIXED_ARG(PPC_HAS_COMPAT, (), ppc,           \
					    PPC_DRIVER_COMPATS)),              \
		(BUILD_ASSERT(0, "Invalid PPC compatible on node: " STRINGIFY( \
					 ppc));))

/* clang-format off */
#define PPC_CHIP_STUB(usbc_id) \
	[USBC_PORT_NEW(usbc_id)] = {},
/* clang-format on */

#define PPC_CHIP(usbc_id)                                               \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, ppc),                     \
		    (PPC_CHIP_FIND(usbc_id, DT_PHANDLE(usbc_id, ppc))), \
		    (PPC_CHIP_STUB(usbc_id)))

#define PPC_CHIP_VERIFY(usbc_id)                   \
	IF_ENABLED(DT_NODE_HAS_PROP(usbc_id, ppc), \
		   (PPC_PROP_COMPATIBLE_VERIFY(DT_PHANDLE(usbc_id, ppc))))

#define PPC_CHIP_ALT(usbc_id)                                               \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, ppc_alt),                     \
		    (PPC_CHIP_FIND(usbc_id, DT_PHANDLE(usbc_id, ppc_alt))), \
		    ())

/*
 * The PPC_CHIP_VERIFY macro expands to nothing when the PPC driver
 * compatible string is found in the PPC_DRIVER_COMPATS list.  Otherwise the
 * macro expands to a BUILD_ASSERT error.
 */
DT_FOREACH_STATUS_OKAY(named_usbc_port, PPC_CHIP_VERIFY)

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
DT_FOREACH_USBC_DRIVER_STATUS_OK_VARGS(PPC_ALT_DEFINE, PPC_DRIVERS)
