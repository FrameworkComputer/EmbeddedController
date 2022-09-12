/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <x86_non_dsx_common_pwrseq_sm_handler.h>

/* Host commands */
static enum ec_status
host_command_reboot_ap_on_g3(struct host_cmd_handler_args *args)
{
	const struct ec_params_reboot_ap_on_g3_v1 *cmd = args->params;

	/* Store request for processing at g3 */
	request_start_from_g3();

	switch (args->version) {
	case 0:
		break;
	case 1:
		/* Store user specified delay to wait in G3 state */
		set_start_from_g3_delay_seconds(cmd->reboot_ap_at_g3_delay);
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_REBOOT_AP_ON_G3, host_command_reboot_ap_on_g3,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

#if CONFIG_AP_PWRSEQ_HOST_SLEEP
/* Track last reported sleep event */
static enum host_sleep_event host_sleep_state;

static enum ec_status
host_command_host_sleep_event(struct host_cmd_handler_args *args)
{
	const struct ec_params_host_sleep_event_v1 *p = args->params;
	struct ec_response_host_sleep_event_v1 *r = args->response;
	struct host_sleep_event_context ctx;
	enum host_sleep_event state = p->sleep_event;

	host_sleep_state = state;
	ctx.sleep_transitions = 0;
	switch (state) {
	case HOST_SLEEP_EVENT_S0IX_SUSPEND:
	case HOST_SLEEP_EVENT_S3_SUSPEND:
	case HOST_SLEEP_EVENT_S3_WAKEABLE_SUSPEND:
		ctx.sleep_timeout_ms = EC_HOST_SLEEP_TIMEOUT_DEFAULT;

		/* The original version contained only state. */
		if (args->version >= 1)
			ctx.sleep_timeout_ms =
				p->suspend_params.sleep_timeout_ms;

		break;

	default:
		break;
	}

	ap_power_chipset_handle_host_sleep_event(host_sleep_state, &ctx);
	switch (state) {
	case HOST_SLEEP_EVENT_S0IX_RESUME:
	case HOST_SLEEP_EVENT_S3_RESUME:
		if (args->version >= 1) {
			r->resume_response.sleep_transitions =
				ctx.sleep_transitions;

			args->response_size = sizeof(*r);
		}

		break;

	default:
		break;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HOST_SLEEP_EVENT, host_command_host_sleep_event,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

void power_set_host_sleep_state(enum host_sleep_event state)
{
	host_sleep_state = state;
}
#endif /* CONFIG_AP_PWRSEQ_HOST_SLEEP */

/* End of host commands */
