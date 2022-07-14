/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "nissa_hdmi.h"

__override void nissa_configure_hdmi_power_gpios(void)
{
	/* Pujjo needs to drive VCC enable but not core rails */
	nissa_configure_hdmi_vcc();
}
