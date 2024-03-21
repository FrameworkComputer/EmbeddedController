/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "heci_system_state.h"
#include "sedi_driver_pm.h"

#define SYSTEM_STATE_SUBSCRIBE 0x1
#define SYSTEM_STATE_STATUS 0x2
#define SYSTEM_STATE_QUERY_SUBSCRIBERS 0x3
#define SYSTEM_STATE_STATE_CHANGE_REQ 0x4

#define SUSPEND_STATE_BIT BIT(1)

struct ss_header {
	uint32_t cmd;
	uint32_t cmd_status;
} __packed;

struct ss_query_subscribers {
	struct ss_header hdr;
} __packed;

struct ss_subscribe {
	struct ss_header hdr;
	uint32_t states;
} __packed;

struct ss_status {
	struct ss_header hdr;
	uint32_t supported_states;
	uint32_t states_status;
} __packed;

struct ss_state_change_req {
	struct ss_header hdr;
	uint32_t requested_states;
	uint32_t states_status;
} __packed;

struct ss_subsystem_context {
	uint32_t registered_state;

	int num_of_ss_client;
	struct ss_subsys_device *clients[2];
};

static struct ss_subsystem_context ss_subsys_ctx;

void heci_handle_system_state_msg(uint8_t *msg, const size_t length)
{
	struct ss_header *hdr = (struct ss_header *)msg;
	struct ss_subscribe subscribe;
	struct ss_status *status;

	switch (hdr->cmd) {
	case SYSTEM_STATE_QUERY_SUBSCRIBERS:
		subscribe.hdr.cmd = SYSTEM_STATE_SUBSCRIBE;
		subscribe.hdr.cmd_status = 0;
		ss_subsys_ctx.registered_state |= SUSPEND_STATE_BIT;
		subscribe.states = ss_subsys_ctx.registered_state;

		heci_send_proto_msg(0, HECI_SYSTEM_STATE_CLIENT_ADDR, true,
				    (uint8_t *)&subscribe, sizeof(subscribe));

		break;
	case SYSTEM_STATE_STATUS:
		status = (struct ss_status *)msg;
		if (status->supported_states & SUSPEND_STATE_BIT) {
#ifdef CONFIG_PM
			if (status->states_status & SUSPEND_STATE_BIT)
				sedi_pm_host_suspend(1);
			else
				sedi_pm_host_suspend(0);
#endif
		}

		break;
	}
}
