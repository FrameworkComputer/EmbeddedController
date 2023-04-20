/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_charge.h"
#include "usbc/bc12_pi3usb9201.h"
#include "usbc/bc12_rt1718s.h"
#include "usbc/bc12_rt1739.h"
#include "usbc/bc12_rt9490.h"
#include "usbc/bc12_upstream.h"
#include "usbc/tcpc_rt1718s.h"
#include "usbc/utils.h"

#include <zephyr/devicetree.h>

/* Check RT1718S dependency. BC12 node must be dependent on TCPC node. */
#if DT_HAS_COMPAT_STATUS_OKAY(RT1718S_BC12_COMPAT)
BUILD_ASSERT(DT_HAS_COMPAT_STATUS_OKAY(RT1718S_TCPC_COMPAT));
#endif

#define BC12_CHIP_ENTRY(usbc_id, bc12_id, chip_fn) \
	[USBC_PORT_NEW(usbc_id)] = chip_fn(bc12_id)

#define CHECK_COMPAT(compat, usbc_id, bc12_id, config)   \
	COND_CODE_1(DT_NODE_HAS_COMPAT(bc12_id, compat), \
		    (BC12_CHIP_ENTRY(usbc_id, bc12_id, config)), ())

#define BC12_CHIP_FIND(usbc_id, bc12_id)                                       \
	CHECK_COMPAT(RT1718S_BC12_COMPAT, usbc_id, bc12_id, BC12_CHIP_RT1718S) \
	CHECK_COMPAT(RT1739_BC12_COMPAT, usbc_id, bc12_id, BC12_CHIP_RT1739)   \
	CHECK_COMPAT(RT1739_BC12_EMUL_COMPAT, usbc_id, bc12_id,                \
		     BC12_CHIP_RT1739)                                         \
	CHECK_COMPAT(RT9490_BC12_COMPAT, usbc_id, bc12_id, BC12_CHIP_RT9490)   \
	CHECK_COMPAT(PI3USB9201_COMPAT, usbc_id, bc12_id,                      \
		     BC12_CHIP_PI3USB9201)                                     \
	CHECK_COMPAT(PI3USB9201_UPSTREAM_COMPAT, usbc_id, bc12_id,             \
		     BC12_CHIP_UPSTREAM)

#define BC12_CHIP(usbc_id)                           \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, bc12), \
		    (BC12_CHIP_FIND(usbc_id, DT_PHANDLE(usbc_id, bc12))), ())

/* BC1.2 controllers */
struct bc12_config bc12_ports[CHARGE_PORT_COUNT] = { DT_FOREACH_STATUS_OKAY(
	named_usbc_port, BC12_CHIP) };
