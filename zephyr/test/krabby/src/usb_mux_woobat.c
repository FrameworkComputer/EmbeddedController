/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "baseboard_usbc_config.h"
#include "driver/usb_mux/ps8743.h"
#include "emul/emul_ps8743.h"
#include "usb_mux.h"
#include "usbc/usb_muxes.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/ztest.h>

const static struct emul *emul = EMUL_DT_GET(DT_NODELABEL(ps8743_mux_1));

ZTEST(usb_mux_woobat_init, test_mux_init_value)
{
	ps8743_eq_c1_setting();
	zassert_equal(ps8743_emul_peek_reg(emul, PS8743_REG_USB_EQ_RX), 0x90);
}

ZTEST_SUITE(usb_mux_woobat_init, NULL, NULL, NULL, NULL, NULL);
