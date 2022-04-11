/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <devicetree.h>
#include <sys/util.h>
#include "usb_pd_tcpm.h"
#include "usb_pd.h"
#include "usbc/tcpc_fusb302.h"
#include "usbc/tcpc_it8xxx2.h"
#include "usbc/tcpc_ps8xxx.h"
#include "usbc/tcpci.h"
#include "usbc/utils.h"

#if DT_HAS_COMPAT_STATUS_OKAY(FUSB302_TCPC_COMPAT) ||                          \
	DT_HAS_COMPAT_STATUS_OKAY(IT8XXX2_TCPC_COMPAT) ||                      \
	DT_HAS_COMPAT_STATUS_OKAY(PS8XXX_COMPAT) ||                            \
	DT_HAS_COMPAT_STATUS_OKAY(TCPCI_COMPAT)                                \

#define TCPC_CONFIG(id, fn) [USBC_PORT(id)] = fn(id)

#define MAYBE_CONST COND_CODE_1(CONFIG_PLATFORM_EC_USB_PD_TCPC_RUNTIME_CONFIG, \
				(), (const))

MAYBE_CONST struct tcpc_config_t tcpc_config[] = {
	DT_FOREACH_STATUS_OKAY_VARGS(FUSB302_TCPC_COMPAT, TCPC_CONFIG,
				     TCPC_CONFIG_FUSB302)
	DT_FOREACH_STATUS_OKAY_VARGS(IT8XXX2_TCPC_COMPAT, TCPC_CONFIG,
				     TCPC_CONFIG_IT8XXX2)
	DT_FOREACH_STATUS_OKAY_VARGS(PS8XXX_COMPAT, TCPC_CONFIG,
				     TCPC_CONFIG_PS8XXX)
	DT_FOREACH_STATUS_OKAY_VARGS(TCPCI_COMPAT, TCPC_CONFIG,
				     TCPC_CONFIG_TCPCI)
};

#endif /* DT_HAS_COMPAT_STATUS_OKAY */
