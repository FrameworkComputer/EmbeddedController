/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "heci_client.h"
#include "registers.h"
#include "system_state.h"
#include "console.h"

#ifdef SS_SUBSYSTEM_DEBUG
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_LPC, format, ## args)
#else
#define CPUTS(outstr)
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif


/* the following "define"s and structures are from host driver
 * and they are slightly modified for look&feel purpose.
 */
#define SYSTEM_STATE_SUBSCRIBE		0x1
#define SYSTEM_STATE_STATUS		0x2
#define SYSTEM_STATE_QUERY_SUBSCRIBERS	0x3
#define SYSTEM_STATE_STATE_CHANGE_REQ	0x4

#define SUSPEND_STATE_BIT		BIT(1) /* suspend/resume */

/* Cached state of ISH's requested power rails when AP suspends */
static uint32_t cached_vnn_request;

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

/* change request from device but host doesn't support it */
struct ss_state_change_req {
	struct ss_header hdr;
	uint32_t requested_states;
	uint32_t states_status;
} __packed;

/*
 * TODO: For now, every HECI client with valid .suspend or .resume callback is
 * automatically registered as client of system state subsystem.
 * so MAX_SS_CLIENTS should be HECI_MAX_NUM_OF_CLIENTS.
 * if an object wants to get system state event then it can embeds
 * "struct ss_subsys_device" in it and calls ss_subsys_register_client() like
 * HECI client.
 */
#define MAX_SS_CLIENTS				HECI_MAX_NUM_OF_CLIENTS

struct ss_subsystem_context {
	uint32_t registered_state;

	int num_of_ss_client;
	struct ss_subsys_device *clients[MAX_SS_CLIENTS];
};

static struct ss_subsystem_context ss_subsys_ctx;

int ss_subsys_register_client(struct ss_subsys_device *ss_device)
{
	int handle;

	if (ss_subsys_ctx.num_of_ss_client == MAX_SS_CLIENTS)
		return -1;

	if (ss_device->cbs->resume || ss_device->cbs->suspend) {
		handle = ss_subsys_ctx.num_of_ss_client++;
		ss_subsys_ctx.registered_state |= SUSPEND_STATE_BIT;
		ss_subsys_ctx.clients[handle] = ss_device;
	} else {
		return -1;
	}

	return handle;
}

static int ss_subsys_suspend(void)
{
	int i;

	for (i = ss_subsys_ctx.num_of_ss_client - 1; i >= 0; i--) {
		if (ss_subsys_ctx.clients[i]->cbs->suspend)
			ss_subsys_ctx.clients[i]->cbs->suspend(
						ss_subsys_ctx.clients[i]);
	}

	/*
	 * PMU_VNN_REQ is used by ISH FW to assert power requirements of ISH to
	 * PMC. The system won't enter S0ix if ISH is requesting any power
	 * rails. Setting a bit to 1 both sets and clears a requested value.
	 * Cache the value of request power so we can restore it on resume.
	 */
	if (IS_ENABLED(CHIP_FAMILY_ISH5)) {
		cached_vnn_request = PMU_VNN_REQ;
		PMU_VNN_REQ = cached_vnn_request;
	}
	return EC_SUCCESS;
}

static int ss_subsys_resume(void)
{
	int i;

	/*
	 * Restore VNN power request from before suspend.
	 */
	if (IS_ENABLED(CHIP_FAMILY_ISH5) &&
	    cached_vnn_request) {
		/* Request all cached power rails that are not already on. */
		PMU_VNN_REQ = cached_vnn_request & ~PMU_VNN_REQ;
		/* Wait for power request to get acknowledged */
		while (!(PMU_VNN_REQ_ACK & PMU_VNN_REQ_ACK_STATUS))
			continue;
	}

	for (i = 0; i < ss_subsys_ctx.num_of_ss_client; i++) {
		if (ss_subsys_ctx.clients[i]->cbs->resume)
			ss_subsys_ctx.clients[i]->cbs->resume(
						ss_subsys_ctx.clients[i]);
	}

	return EC_SUCCESS;
}

void heci_handle_system_state_msg(uint8_t *msg, const size_t length)
{
	struct ss_header *hdr = (struct ss_header *)msg;
	struct ss_subscribe subscribe;
	struct ss_status *status;

	switch (hdr->cmd) {
	case SYSTEM_STATE_QUERY_SUBSCRIBERS:
		subscribe.hdr.cmd = SYSTEM_STATE_SUBSCRIBE;
		subscribe.hdr.cmd_status = 0;
		subscribe.states = ss_subsys_ctx.registered_state;

		heci_send_fixed_client_msg(HECI_FIXED_SYSTEM_STATE_ADDR,
					   (uint8_t *)&subscribe,
					   sizeof(subscribe));

		break;
	case SYSTEM_STATE_STATUS:
		status = (struct ss_status *)msg;
		if (status->supported_states & SUSPEND_STATE_BIT) {
			if (status->states_status & SUSPEND_STATE_BIT)
				ss_subsys_suspend();
			else
				ss_subsys_resume();
		}

		break;
	}
}
