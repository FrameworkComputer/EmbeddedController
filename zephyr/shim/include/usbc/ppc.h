/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_CHROME_USBC_PPC_H
#define ZEPHYR_CHROME_USBC_PPC_H

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include "usbc/ppc_rt1739.h"
#include "usbc/ppc_nx20p348x.h"
#include "usbc/ppc_sn5s330.h"
#include "usbc/ppc_syv682x.h"
#include "usbc/utils.h"
#include "usbc_ppc.h"

extern struct ppc_config_t ppc_chips_alt[];

#define ALT_PPC_CHIP_CHK(usbc_id, usb_port_num)                              \
	COND_CODE_1(DT_REG_HAS_IDX(usbc_id, usb_port_num),                   \
		    (COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, ppc_alt), (|| 1), \
				 (|| 0))),                                   \
		    (|| 0))

#define PPC_ENABLE_ALTERNATE(usb_port_num)                                            \
	do {                                                                          \
		BUILD_ASSERT(                                                         \
			(0 DT_FOREACH_STATUS_OKAY_VARGS(named_usbc_port,              \
							ALT_PPC_CHIP_CHK,             \
							usb_port_num)),               \
			"Selected USB node does not exist or does not specify a PPC " \
			"alternate chip");                                            \
		memcpy(&ppc_chips[usb_port_num], &ppc_chips_alt[usb_port_num],        \
		       sizeof(struct ppc_config_t));                                  \
	} while (0)

#endif /* ZEPHYR_CHROME_USBC_PPC_H */
