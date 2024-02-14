/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Stub functions accessed by the TYPEC_CONTROL host command that are only used
 * in TCPMv2 and not under a PDC.
 */

#include "atomic.h"
#include "ec_commands.h"
#include "host_command.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dpm_sm.h"

enum ec_status board_set_tbt_ufp_reply(int port, enum typec_tbt_ufp_reply reply)
{
	ARG_UNUSED(port);
	ARG_UNUSED(reply);

	return EC_RES_UNAVAILABLE;
}

void pd_clear_events(int port, uint32_t clear_mask)
{
	ARG_UNUSED(port);
	ARG_UNUSED(clear_mask);
}

void pd_dpm_request(int port, enum pd_dpm_request req)
{
	ARG_UNUSED(port);
	ARG_UNUSED(req);
}

enum ec_status pd_request_enter_mode(int port, enum typec_mode mode)
{
	ARG_UNUSED(port);
	ARG_UNUSED(mode);

	return EC_RES_UNAVAILABLE;
}

enum ec_status pd_set_bist_share_mode(uint8_t enable)
{
	ARG_UNUSED(enable);

	return EC_RES_UNAVAILABLE;
}
