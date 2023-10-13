/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "string.h"
#include "task.h"

#include <zephyr/kernel.h>
#include <zephyr/mgmt/ec_host_cmd/ec_host_cmd.h>
#include <zephyr/sys/iterable_sections.h>

#ifdef CONFIG_EC_HOST_CMD
#ifndef CONFIG_ZTEST
#if !defined(CONFIG_TASK_HOSTCMD_THREAD_MAIN) || \
	defined(CONFIG_EC_HOST_CMD_DEDICATED_THREAD)
BUILD_ASSERT(0, "The upstream Host Command subsystem is supported only with "
		"reusing the main thread.");
#endif
#else
#if (defined(CONFIG_TASK_HOSTCMD_THREAD_DEDICATED) &&  \
     !defined(CONFIG_EC_HOST_CMD_DEDICATED_THREAD)) || \
	(defined(CONFIG_TASK_HOSTCMD_THREAD_MAIN) &&   \
	 defined(CONFIG_EC_HOST_CMD_DEDICATED_THREAD))
BUILD_ASSERT(
	0,
	"Host command thread setting mismatch between ChromeOS EC and upstream.");
#endif
#endif /* CONFIG_ZTEST */
#ifdef CONFIG_SUPPRESSED_HOST_COMMANDS
__maybe_unused static uint16_t suppressed_cmds[] = {
	CONFIG_SUPPRESSED_HOST_COMMANDS
};
BUILD_ASSERT(ARRAY_SIZE(suppressed_cmds) <=
		     CONFIG_EC_HOST_CMD_LOG_SUPPRESSED_NUMBER,
	     "Increase number of the maximum number.");

BUILD_ASSERT(sizeof(struct host_cmd_handler_args) ==
		     sizeof(struct ec_host_cmd_handler_args),
	     "Incompatible structure.");
BUILD_ASSERT(offsetof(struct host_cmd_handler_args, command) ==
	     offsetof(struct ec_host_cmd_handler_args, command));
BUILD_ASSERT(offsetof(struct host_cmd_handler_args, version) ==
	     offsetof(struct ec_host_cmd_handler_args, version));
BUILD_ASSERT(offsetof(struct host_cmd_handler_args, params) ==
	     offsetof(struct ec_host_cmd_handler_args, input_buf));
BUILD_ASSERT(offsetof(struct host_cmd_handler_args, params_size) ==
	     offsetof(struct ec_host_cmd_handler_args, input_buf_size));
BUILD_ASSERT(offsetof(struct host_cmd_handler_args, response) ==
	     offsetof(struct ec_host_cmd_handler_args, output_buf));
BUILD_ASSERT(offsetof(struct host_cmd_handler_args, response_max) ==
	     offsetof(struct ec_host_cmd_handler_args, output_buf_max));
BUILD_ASSERT(offsetof(struct host_cmd_handler_args, response_size) ==
	     offsetof(struct ec_host_cmd_handler_args, output_buf_size));

BUILD_ASSERT(sizeof(struct ec_host_request) ==
		     sizeof(struct ec_host_cmd_request_header),
	     "Incompatible structure.");
BUILD_ASSERT(offsetof(struct ec_host_request, struct_version) ==
	     offsetof(struct ec_host_cmd_request_header, prtcl_ver));
BUILD_ASSERT(offsetof(struct ec_host_request, checksum) ==
	     offsetof(struct ec_host_cmd_request_header, checksum));
BUILD_ASSERT(offsetof(struct ec_host_request, command) ==
	     offsetof(struct ec_host_cmd_request_header, cmd_id));
BUILD_ASSERT(offsetof(struct ec_host_request, command_version) ==
	     offsetof(struct ec_host_cmd_request_header, cmd_ver));
BUILD_ASSERT(offsetof(struct ec_host_request, reserved) ==
	     offsetof(struct ec_host_cmd_request_header, reserved));
BUILD_ASSERT(offsetof(struct ec_host_request, data_len) ==
	     offsetof(struct ec_host_cmd_request_header, data_len));

BUILD_ASSERT(sizeof(struct ec_host_response) ==
		     sizeof(struct ec_host_cmd_response_header),
	     "Incompatible structure.");
BUILD_ASSERT(offsetof(struct ec_host_response, struct_version) ==
	     offsetof(struct ec_host_cmd_response_header, prtcl_ver));
BUILD_ASSERT(offsetof(struct ec_host_response, checksum) ==
	     offsetof(struct ec_host_cmd_response_header, checksum));
BUILD_ASSERT(offsetof(struct ec_host_response, result) ==
	     offsetof(struct ec_host_cmd_response_header, result));
BUILD_ASSERT(offsetof(struct ec_host_response, data_len) ==
	     offsetof(struct ec_host_cmd_response_header, data_len));
BUILD_ASSERT(offsetof(struct ec_host_response, reserved) ==
	     offsetof(struct ec_host_cmd_response_header, reserved));
#endif /* CONFIG_SUPPRESSED_HOST_COMMANDS */
#endif /* CONFIG_EC_HOST_CMD */

struct host_command *zephyr_find_host_command(int command)
{
	STRUCT_SECTION_FOREACH(host_command, cmd)
	{
		if (cmd->command == command)
			return cmd;
	}

	return NULL;
}

#ifdef CONFIG_EC_HOST_CMD
static void ec_host_cmd_user_cb(const struct ec_host_cmd_rx_ctx *rx_ctx,
				void *user_data)
{
	const struct ec_host_cmd_request_header *const rx_header =
		(void *)rx_ctx->buf;

	/*
	 * If this is the reboot command, reboot immediately. This gives the
	 * host processor a way to unwedge the EC even if it's busy with some
	 * other command.
	 */
	if (rx_header->cmd_id == EC_CMD_REBOOT) {
		system_reset(SYSTEM_RESET_HARD);
	}
}
#endif /* CONFIG_EC_HOST_CMD */

void host_command_main(void)
{
	k_thread_priority_set(get_main_thread(),
			      EC_TASK_PRIORITY(EC_TASK_HOSTCMD_PRIO));
	k_thread_name_set(get_main_thread(), "HOSTCMD");
#ifndef CONFIG_EC_HOST_CMD
	host_command_task(NULL);
#else
#ifndef CONFIG_EC_HOST_CMD_DEDICATED_THREAD
	ec_host_cmd_task();
#endif /* CONFIG_EC_HOST_CMD_DEDICATED_THREAD */
#endif
}

#ifdef CONFIG_EC_HOST_CMD
int host_command_upstream_init(void)
{
#ifdef CONFIG_SUPPRESSED_HOST_COMMANDS
	uint16_t hc_suppressed_cmd[] = { CONFIG_SUPPRESSED_HOST_COMMANDS };

	for (int i = 0; i < ARRAY_SIZE(hc_suppressed_cmd); i++) {
		ec_host_cmd_add_suppressed(hc_suppressed_cmd[i]);
	}
#endif

	/* Set the user callback for custom EC procedures */
	ec_host_cmd_set_user_cb(ec_host_cmd_user_cb, NULL);

	return 0;
}
SYS_INIT(host_command_upstream_init, POST_KERNEL,
	 CONFIG_EC_HOST_CMD_INIT_PRIORITY);
DECLARE_HOOK(HOOK_INIT, host_command_init, HOOK_PRIO_DEFAULT);
#endif

#ifdef CONFIG_EC_HOST_CMD
static enum ec_host_cmd_status
host_command_get_cmd_versions(struct ec_host_cmd_handler_args *args)
{
	const struct ec_host_cmd_handler *found_handler = NULL;
	const struct ec_params_get_cmd_versions *p = args->input_buf;
	const struct ec_params_get_cmd_versions_v1 *p_v1 = args->input_buf;
	struct ec_response_get_cmd_versions *r = args->output_buf;

	memset(r, 0, sizeof(*r));
	STRUCT_SECTION_FOREACH(ec_host_cmd_handler, handler)
	{
		int searched_id = (args->version == 1) ? p_v1->cmd : p->cmd;

		if (handler->id == searched_id) {
			found_handler = handler;
			break;
		}
	}

	if (!found_handler)
		return EC_HOST_CMD_INVALID_PARAM;

	r->version_mask = found_handler->version_mask;

	args->output_buf_size = sizeof(*r);

	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER(EC_CMD_GET_CMD_VERSIONS, host_command_get_cmd_versions,
		    EC_VER_MASK(0) | EC_VER_MASK(1),
		    struct ec_params_get_cmd_versions,
		    struct ec_response_get_cmd_versions);

test_export_static enum ec_host_cmd_status
host_command_protocol_info(struct ec_host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->output_buf;
	const struct ec_host_cmd *hc = ec_host_cmd_get_hc();

	r->protocol_versions = BIT(3);
	r->flags = 0
#if defined(CONFIG_HOST_COMMAND_STATUS) && \
	defined(CONFIG_EC_HOST_CMD_IN_PROGRESS_STATUS)
		   | EC_PROTOCOL_INFO_IN_PROGRESS_SUPPORTED
#endif
		;

	r->max_request_packet_size = hc->rx_ctx.len_max;
	r->max_response_packet_size = hc->tx.len_max;

	args->output_buf_size = sizeof(*r);

	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER_UNBOUND(EC_CMD_GET_PROTOCOL_INFO,
			    host_command_protocol_info, EC_VER_MASK(0));
#endif
