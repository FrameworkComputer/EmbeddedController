/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "nissa_hdmi.h"
#include "pujjoga_sub_board.h"

__override void nissa_configure_hdmi_power_gpios(void)
{
	pujjoga_configure_hdmi_vcc();
}
