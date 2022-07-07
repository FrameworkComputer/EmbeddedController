/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>
#include "usb_pd_tcpm.h"
#include "usb_pd.h"
#include "usbc/tcpc_ccgxxf.h"
#include "usbc/tcpc_fusb302.h"
#include "usbc/tcpc_it8xxx2.h"
#include "usbc/tcpc_nct38xx.h"
#include "usbc/tcpc_ps8xxx.h"
#include "usbc/tcpci.h"
#include "usbc/utils.h"

#if DT_HAS_COMPAT_STATUS_OKAY(CCGXXF_TCPC_COMPAT) ||      \
	DT_HAS_COMPAT_STATUS_OKAY(FUSB302_TCPC_COMPAT) || \
	DT_HAS_COMPAT_STATUS_OKAY(IT8XXX2_TCPC_COMPAT) || \
	DT_HAS_COMPAT_STATUS_OKAY(PS8XXX_COMPAT) ||       \
	DT_HAS_COMPAT_STATUS_OKAY(NCT38XX_TCPC_COMPAT) || \
	DT_HAS_COMPAT_STATUS_OKAY(TCPCI_COMPAT)

#define TCPC_CONFIG(id, fn) [USBC_PORT(id)] = fn(id)

#define MAYBE_CONST \
	COND_CODE_1(CONFIG_PLATFORM_EC_USB_PD_TCPC_RUNTIME_CONFIG, (), (const))

#define MAYBE_EMPTY(compat, config)                                          \
	COND_CODE_1(                                                         \
		DT_HAS_STATUS_OKAY(compat),                                  \
		(DT_FOREACH_STATUS_OKAY_VARGS(compat, TCPC_CONFIG, config)), \
		(EMPTY))

MAYBE_CONST struct tcpc_config_t tcpc_config[] = { LIST_DROP_EMPTY(
	MAYBE_EMPTY(CCGXXF_TCPC_COMPAT, TCPC_CONFIG_CCGXXF),
	MAYBE_EMPTY(FUSB302_TCPC_COMPAT, TCPC_CONFIG_FUSB302),
	MAYBE_EMPTY(IT8XXX2_TCPC_COMPAT, TCPC_CONFIG_IT8XXX2),
	MAYBE_EMPTY(PS8XXX_COMPAT, TCPC_CONFIG_PS8XXX),
	MAYBE_EMPTY(NCT38XX_TCPC_COMPAT, TCPC_CONFIG_NCT38XX),
	MAYBE_EMPTY(TCPCI_COMPAT, TCPC_CONFIG_TCPCI)) };

#endif /* DT_HAS_COMPAT_STATUS_OKAY */
