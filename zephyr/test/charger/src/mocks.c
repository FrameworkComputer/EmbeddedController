/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_pd.h"

__weak uint8_t board_get_usb_pd_port_count(void)
{
	return 2;
}

__weak int isl923x_set_ac_prochot(int chgnum, uint16_t ma)
{
	return EC_ERROR_UNIMPLEMENTED;
}

__weak int isl923x_set_dc_prochot(int chgnum, uint16_t ma)
{
	return EC_ERROR_UNIMPLEMENTED;
}