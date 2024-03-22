/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/retimer/kb8010.h"
#include "driver/retimer/kb8010_public.h"
#include "emul/emul_kb8010.h"
#include "test/drivers/test_state.h"
#include "usb_mux.h"
#include "usb_pd.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(uint32_t, pd_get_tbt_mode_vdo, int, enum tcpci_msg_type);

#define FFF_FAKES_LIST(FAKE) FAKE(pd_get_tbt_mode_vdo)

#define KB8010_PORT 1
#define KB8010_NODE DT_NODELABEL(kb8010_emul)

const struct emul *kb8010_emul = EMUL_DT_GET(KB8010_NODE);

ZTEST(rt_kb8010, test_usb3_modes)
{
	/* enter USB mode */

	usb_mux_set(KB8010_PORT, USB_PD_MUX_USB_ENABLED, USB_SWITCH_CONNECT, 0);

	/* enter USB mode (flipped cable) */

	usb_mux_set(KB8010_PORT, USB_PD_MUX_USB_ENABLED, USB_SWITCH_CONNECT, 1);

	/* enter DP mode */

	usb_mux_set(KB8010_PORT, USB_PD_MUX_DP_ENABLED, USB_SWITCH_CONNECT, 0);

	/* enter USB+DP (a.k.a DPMF) mode */

	usb_mux_set(KB8010_PORT, USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED,
		    USB_SWITCH_CONNECT, 0);

	/* enter USB4 mode */

	usb_mux_set(KB8010_PORT, USB_PD_MUX_USB4_ENABLED, USB_SWITCH_CONNECT,
		    0);

	/* trigger .enter_low_power_mode() */

	usb_mux_set(KB8010_PORT, USB_PD_MUX_NONE, USB_SWITCH_DISCONNECT, 0);
}

ZTEST(rt_kb8010, test_usb4_modes)
{
	union tbt_mode_resp_cable r = { .raw_value = 0 };

	/* enter USB4 mode, passive cable */

	r.tbt_active_passive = TBT_CABLE_PASSIVE;
	pd_get_tbt_mode_vdo_fake.return_val = r.raw_value;

	usb_mux_set(KB8010_PORT, USB_PD_MUX_USB4_ENABLED, USB_SWITCH_CONNECT,
		    0);

	/* enter USB4 mode, active, bi-directional cable */

	r.tbt_active_passive = TBT_CABLE_ACTIVE;
	r.lsrx_comm = BIDIR_LSRX_COMM;
	pd_get_tbt_mode_vdo_fake.return_val = r.raw_value;

	usb_mux_set(KB8010_PORT, USB_PD_MUX_USB4_ENABLED, USB_SWITCH_CONNECT,
		    0);

	/* enter USB4 mode, active, uni-directional cable */

	r.tbt_active_passive = TBT_CABLE_ACTIVE;
	r.lsrx_comm = UNIDIR_LSRX_COMM;
	pd_get_tbt_mode_vdo_fake.return_val = r.raw_value;

	usb_mux_set(KB8010_PORT, USB_PD_MUX_USB4_ENABLED, USB_SWITCH_CONNECT,
		    0);

	/* trigger .enter_low_power_mode() */

	usb_mux_set(KB8010_PORT, USB_PD_MUX_NONE, USB_SWITCH_DISCONNECT, 0);
}

static void kb8010_test_before(void *data)
{
	FFF_FAKES_LIST(RESET_FAKE);

	kb8010_emul_set_reset(kb8010_emul, false);

	usb_mux_init(KB8010_PORT);

	usb_mux_set(KB8010_PORT, USB_PD_MUX_NONE, USB_SWITCH_DISCONNECT, 0);
}

static void kb8010_test_after(void *data)
{
	usb_mux_set(KB8010_PORT, USB_PD_MUX_NONE, USB_SWITCH_DISCONNECT, 0);
}

ZTEST_SUITE(rt_kb8010, drivers_predicate_post_main, NULL, kb8010_test_before,
	    kb8010_test_after, NULL);
