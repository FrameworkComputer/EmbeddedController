/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_cbi.h"
#include "driver/ppc/ktu1125_public.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "ppc/syv682x_public.h"
#include "system.h"
#include "usb_mux_config.h"
#include "usbc_config.h"

void board_reset_pd_mcu(void)
{
	/* Nothing to do */
}

__override bool board_is_tbt_usb4_port(int port)
{
	return true;
}
