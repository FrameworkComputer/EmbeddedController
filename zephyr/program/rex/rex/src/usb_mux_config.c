/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Rex board-specific USB-C mux configuration */

#include "console.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "hooks.h"
#include "ioexpander.h"
#include "usb_mux.h"
#include "usbc/ppc.h"
#include "usbc/tcpci.h"
#include "usbc/usb_muxes.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_ZTEST

#undef USB_MUX_ENABLE_ALTERNATIVE
#define USB_MUX_ENABLE_ALTERNATIVE(x)

#undef TCPC_ENABLE_ALTERNATE_BY_NODELABEL
#define TCPC_ENABLE_ALTERNATE_BY_NODELABEL(x, y)

#undef PPC_ENABLE_ALTERNATE_BY_NODELABEL
#define PPC_ENABLE_ALTERNATE_BY_NODELABEL(x, y)

#endif /* CONFIG_ZTEST */

LOG_MODULE_DECLARE(rex, CONFIG_REX_LOG_LEVEL);

static void setup_mux(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FW_USB_DB, &val);
	if (ret != 0) {
		return;
	}

	if (val == FW_USB_DB_NOT_CONNECTED) {
		LOG_INF("USB DB: not connected");
	}
	if (val == FW_USB_DB_USB3) {
		LOG_INF("USB DB: Setting USB3 mux");
	}
	if (val == FW_USB_DB_USB4_ANX7452) {
		LOG_INF("USB DB: Setting ANX7452 mux");
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_chain_anx7452_port1);
		TCPC_ENABLE_ALTERNATE_BY_NODELABEL(1, rt1716_tcpc_port1);
		PPC_ENABLE_ALTERNATE_BY_NODELABEL(1, ppc_port1_syv);
	}
}
DECLARE_HOOK(HOOK_INIT, setup_mux, HOOK_PRIO_INIT_I2C);
