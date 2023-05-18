/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Rex board-specific USB-C mux configuration */

#include "console.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "hooks.h"
#include "usb_mux.h"
#include "usb_mux_config.h"
#include "usbc/ppc.h"
#include "usbc/tcpci.h"
#include "usbc/usb_muxes.h"
#include "usbc_config.h"

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

uint32_t usb_db_type;

static void setup_usb_db(void)
{
	int ret;

	ret = cros_cbi_get_fw_config(FW_USB_DB, &usb_db_type);
	if (ret != 0) {
		LOG_INF("USB DB: Failed to get FW_USB_DB from CBI");
		usb_db_type = -1;
		return;
	}

	switch (usb_db_type) {
	case FW_USB_DB_NOT_CONNECTED:
		LOG_INF("USB DB: not connected");
		break;
	case FW_USB_DB_USB3:
		LOG_INF("USB DB: Setting USB3 mux");
		break;
	case FW_USB_DB_USB4_ANX7452:
		LOG_INF("USB DB: Setting ANX7452 mux");
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_chain_anx7452_port1);
		TCPC_ENABLE_ALTERNATE_BY_NODELABEL(USBC_PORT_C1,
						   tcpc_rt1716_port1);
		PPC_ENABLE_ALTERNATE_BY_NODELABEL(USBC_PORT_C1, ppc_syv_port1);
		break;
	case FW_USB_DB_USB4_KB8010:
		LOG_INF("USB DB: Setting KB8010 mux");
		USB_MUX_ENABLE_ALTERNATIVE(usb_mux_chain_kb8010_port1);
		TCPC_ENABLE_ALTERNATE_BY_NODELABEL(USBC_PORT_C1,
						   tcpc_rt1716_port1);
		PPC_ENABLE_ALTERNATE_BY_NODELABEL(USBC_PORT_C1,
						  ppc_ktu1125_port1);
		break;
	default:
		LOG_INF("USB DB: No known USB DB found");
	}
}
DECLARE_HOOK(HOOK_INIT, setup_usb_db, HOOK_PRIO_POST_I2C);
