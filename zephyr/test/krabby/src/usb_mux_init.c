/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/usb_mux/tusb1064.h"
#include "emul/emul_tusb1064.h"
#include "usb_mux.h"
#include "usbc/usb_muxes.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/ztest.h>

const static struct emul *emul = EMUL_DT_GET(DT_NODELABEL(tusb1064_mux_1));

const static int tusb1064_port = USB_MUX_PORT(DT_NODELABEL(tusb1064_mux_1));

ZTEST(usb_mux_init, test_mux_init_value)
{
	usb_mux_set(tusb1064_port, USB_PD_MUX_DP_ENABLED, USB_SWITCH_CONNECT,
		    0);
	zassert_equal(tusb1064_emul_peek_reg(emul, TUSB1064_REG_DP1DP3EQ_SEL),
		      TUSB1064_DP1EQ(TUSB1064_DP_EQ_RX_8_9_DB) |
			      TUSB1064_DP3EQ(TUSB1064_DP_EQ_RX_5_4_DB));
}

ZTEST_SUITE(usb_mux_init, NULL, NULL, NULL, NULL, NULL);
