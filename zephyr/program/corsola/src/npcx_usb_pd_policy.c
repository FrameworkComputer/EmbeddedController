/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "baseboard_usbc_config.h"
#include "driver/tcpm/rt1718s.h"
#include "driver/tcpm/tcpm.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

int pd_snk_is_vbus_provided(int port)
{
	/* TODO: use ADC? */
	return tcpm_check_vbus_level(port, VBUS_PRESENT);
}
