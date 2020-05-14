/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Mock USB Type-C mux */

#include "common.h"
#include "console.h"
#include "usb_mux.h"
#include "mock/usb_mux_mock.h"
#include "memory.h"

#ifndef CONFIG_COMMON_RUNTIME
#define cprints(format, args...)
#endif

/* Public API for controlling/inspecting this mock */
struct mock_usb_mux_ctrl mock_usb_mux;

void mock_usb_mux_reset(void)
{
	memset(&mock_usb_mux, 0, sizeof(mock_usb_mux));
}

static int mock_init(const struct usb_mux *me)
{
	return EC_SUCCESS;
}

static int mock_set(const struct usb_mux *me, mux_state_t mux_state)
{
	mock_usb_mux.state = mux_state;
	++mock_usb_mux.num_set_calls;
	ccprints("[MUX] Set to 0x%02x", mux_state);

	return EC_SUCCESS;
}

int mock_get(const struct usb_mux *me, mux_state_t *mux_state)
{
	*mux_state = mock_usb_mux.state;
	return EC_SUCCESS;
}

static int mock_enter_low_power_mode(const struct usb_mux *me)
{
	return EC_SUCCESS;
}

const struct usb_mux_driver mock_usb_mux_driver = {
	.init = &mock_init,
	.set = &mock_set,
	.get = &mock_get,
	.enter_low_power_mode = &mock_enter_low_power_mode,
};
