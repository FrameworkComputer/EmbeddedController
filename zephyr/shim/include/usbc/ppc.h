/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_CHROME_USBC_PPC_H
#define ZEPHYR_CHROME_USBC_PPC_H

#include <device.h>
#include <devicetree.h>
#include "usbc/ppc_rt1739.h"
#include "usbc/ppc_sn5s330.h"
#include "usbc/ppc_syv682x.h"
#include "usbc/utils.h"
#include "usbc_ppc.h"

#define PPC_ID(id) DT_CAT(PPC_, id)
#define PPC_ID_WITH_COMMA(id) PPC_ID(id),
#define PPC_ALT_FOR(alt_id) USBC_PORT(DT_PHANDLE(alt_id, alternate_for))

#define PPC_ALT_ENUM(id)                                 \
	COND_CODE_1(DT_NODE_HAS_PROP(id, alternate_for), \
		    (PPC_ID_WITH_COMMA(id)), ())

enum ppc_chips_alt_id {
	DT_FOREACH_STATUS_OKAY(RT1739_PPC_COMPAT, PPC_ALT_ENUM)
	DT_FOREACH_STATUS_OKAY(SN5S330_COMPAT, PPC_ALT_ENUM)
	DT_FOREACH_STATUS_OKAY(SYV682X_COMPAT, PPC_ALT_ENUM)
	PPC_CHIP_ALT_COUNT
};

extern struct ppc_config_t ppc_chips_alt[];

#define PPC_ENABLE_ALTERNATE(nodelabel)                                  \
	do {                                                             \
		BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(nodelabel)),    \
			     "PPC alternate node does not exist");       \
		memcpy(&ppc_chips[PPC_ALT_FOR(DT_NODELABEL(nodelabel))], \
		       &ppc_chips_alt[PPC_ID(DT_NODELABEL(nodelabel))],  \
		       sizeof(struct ppc_config_t));                     \
	} while (0)

#endif /* ZEPHYR_CHROME_USBC_PPC_H */
