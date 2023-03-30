/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Function stubs needed for building Nissa board code, but that aren't
 * meaningful to testing.
 */

#include "common.h"

__overridable void pd_power_supply_reset(int port)
{
}

__overridable int pd_set_power_supply_ready(int port)
{
	return 0;
}

__overridable void pd_set_input_current_limit(int port, uint32_t max_ma,
					      uint32_t supply_voltage)
{
}

__overridable void usb_interrupt_c0(enum gpio_signal signal)
{
}
