/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "driver/retimer/anx7483.h"
#include "driver/retimer/anx7483_public.h"
#include "usb_mux.h"
#include "usbc/usb_muxes.h"

#define ANX7483_CFG2_CUSTOM 0x6E

int board_anx7483_c1_mux_set(const struct usb_mux *me, mux_state_t mux_state)
{
	RETURN_ERROR(anx7483_set_default_tuning(me, mux_state));

	i2c_write8(me->i2c_port, me->i2c_addr_flags, ANX7483_URX1_PORT_CFG2_REG,
		   ANX7483_CFG2_CUSTOM);
	i2c_write8(me->i2c_port, me->i2c_addr_flags, ANX7483_URX2_PORT_CFG2_REG,
		   ANX7483_CFG2_CUSTOM);
	i2c_write8(me->i2c_port, me->i2c_addr_flags, ANX7483_DRX1_PORT_CFG2_REG,
		   ANX7483_CFG2_CUSTOM);
	i2c_write8(me->i2c_port, me->i2c_addr_flags, ANX7483_DRX2_PORT_CFG2_REG,
		   ANX7483_CFG2_CUSTOM);

	return EC_SUCCESS;
}
