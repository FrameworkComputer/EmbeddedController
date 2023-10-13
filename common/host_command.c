/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for Chrome EC */

#include "builtin/assert.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "link_defs.h"
#include "lpc.h"
#include "power.h"
#include "printf.h"
#include "shared_mem.h"
#include "system.h"
#include "system_safe_mode.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_HOSTCMD, outstr)
#define CPRINTF(format, args...) cprintf(CC_HOSTCMD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_HOSTCMD, format, ##args)

#ifndef CONFIG_HOSTCMD_X86
/*
 * Simulated memory map.  Must be word-aligned, because some of the elements
 * in the memory map are words.
 */
static uint8_t host_memmap[EC_MEMMAP_SIZE] __aligned(4);
#endif

uint8_t *host_get_memmap(int offset)
{
#ifdef CONFIG_HOSTCMD_X86
	return lpc_get_memmap_range() + offset;
#else
	return host_memmap + offset;
#endif
}

void host_command_init(void)
{
	/* Initialize memory map ID area */
	host_get_memmap(EC_MEMMAP_ID)[0] = 'E';
	host_get_memmap(EC_MEMMAP_ID)[1] = 'C';
	*host_get_memmap(EC_MEMMAP_ID_VERSION) = 1;
	*host_get_memmap(EC_MEMMAP_EVENTS_VERSION) = 1;

#ifdef CONFIG_HOSTCMD_EVENTS
	host_set_single_event(EC_HOST_EVENT_INTERFACE_READY);
	HOST_EVENT_CPRINTS("hostcmd init", host_get_events());
#endif
}

int host_request_expected_size(const struct ec_host_request *r)
{
	/* Check host request version */
	if (r->struct_version != EC_HOST_REQUEST_VERSION)
		return 0;

	/* Reserved byte should be 0 */
	if (r->reserved)
		return 0;

	return sizeof(*r) + r->data_len;
}
/*****************************************************************************/
/* Host commands */

/* TODO(crosbug.com/p/11223): Remove this once the kernel no longer cares */
static enum ec_status
host_command_proto_version(struct host_cmd_handler_args *args)
{
	struct ec_response_proto_version *r = args->response;

	r->version = EC_PROTO_VERSION;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PROTO_VERSION, host_command_proto_version,
		     EC_VER_MASK(0));

static enum ec_status host_command_hello(struct host_cmd_handler_args *args)
{
	const struct ec_params_hello *p = args->params;
	struct ec_response_hello *r = args->response;
	uint32_t d = p->in_data;

	r->out_data = d + 0x01020304;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HELLO, host_command_hello, EC_VER_MASK(0));

#ifndef CONFIG_HOSTCMD_X86
/*
 * Host command to read memory map is not needed on LPC, because LPC can
 * directly map the data to the host's memory space.
 */
static enum ec_status
host_command_read_memmap(struct host_cmd_handler_args *args)
{
	const struct ec_params_read_memmap *p = args->params;

	/* Copy params out of data before we overwrite it with output */
	uint8_t offset = p->offset;
	uint8_t size = p->size;

	if (size > EC_MEMMAP_SIZE || offset > EC_MEMMAP_SIZE ||
	    offset + size > EC_MEMMAP_SIZE || size > args->response_max)
		return EC_RES_INVALID_PARAM;

	/* Make sure switch data is initialized */
	if (offset == EC_MEMMAP_SWITCHES &&
	    *host_get_memmap(EC_MEMMAP_SWITCHES_VERSION) == 0)
		return EC_RES_UNAVAILABLE;

	memcpy(args->response, host_get_memmap(offset), size);
	args->response_size = size;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_READ_MEMMAP, host_command_read_memmap,
		     EC_VER_MASK(0));
#endif

/* CONFIG_EC_HOST_CMD enables the upstream Host Command support */
#ifndef CONFIG_EC_HOST_CMD
static enum ec_status
host_command_get_cmd_versions(struct host_cmd_handler_args *args)
{
	const struct ec_params_get_cmd_versions *p = args->params;
	const struct ec_params_get_cmd_versions_v1 *p_v1 = args->params;
	struct ec_response_get_cmd_versions *r = args->response;

	const struct host_command *cmd = (args->version == 1) ?
						 find_host_command(p_v1->cmd) :
						 find_host_command(p->cmd);

	if (!cmd)
		return EC_RES_INVALID_PARAM;

	r->version_mask = cmd->version_mask;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_CMD_VERSIONS, host_command_get_cmd_versions,
		     EC_VER_MASK(0) | EC_VER_MASK(1));
#endif /* CONFIG_EC_HOST_CMD */

/* Returns what we tell it to. */
static enum ec_status
host_command_test_protocol(struct host_cmd_handler_args *args)
{
	const struct ec_params_test_protocol *p = args->params;
	struct ec_response_test_protocol *r = args->response;
	int copy_len = MIN(p->ret_len, sizeof(r->buf)); /* p,r bufs same size */

	memset(r->buf, 0, sizeof(r->buf));
	memcpy(r->buf, p->buf, copy_len);
	args->response_size = copy_len;

	return p->ec_result;
}
DECLARE_HOST_COMMAND(EC_CMD_TEST_PROTOCOL, host_command_test_protocol,
		     EC_VER_MASK(0));

/* Returns supported features. */
static enum ec_status
host_command_get_features(struct host_cmd_handler_args *args)
{
	struct ec_response_get_features *r = args->response;
	args->response_size = sizeof(*r);

	memset(r, 0, sizeof(*r));
	r->flags[0] = get_feature_flags0();
	r->flags[1] = get_feature_flags1();
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_FEATURES, host_command_get_features,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_HOST_COMMAND_STATUS
/* Returns current command status (busy or not) */
static enum ec_status
host_command_get_comms_status(struct host_cmd_handler_args *args)
{
	struct ec_response_get_comms_status *r = args->response;
	bool command_ended;

#ifndef CONFIG_EC_HOST_CMD
	command_ended = host_command_in_process_ended();
#else
	command_ended = ec_host_cmd_send_in_progress_ended();
#endif

	r->flags = command_ended ? 0 : EC_COMMS_STATUS_PROCESSING;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_COMMS_STATUS, host_command_get_comms_status,
		     EC_VER_MASK(0));

/* Resend the last saved response */
static enum ec_status
host_command_resend_response(struct host_cmd_handler_args *args)
{
	uint16_t result;

#ifndef CONFIG_EC_HOST_CMD
	result = host_command_get_saved_result();
#else
	result = ec_host_cmd_send_in_progress_status();
#endif

	/* Handle resending response */
	args->response_size = 0;

#ifndef CONFIG_EC_HOST_CMD
	args->result = result;
	return EC_RES_SUCCESS;
#else
	return result;
#endif
}
DECLARE_HOST_COMMAND(EC_CMD_RESEND_RESPONSE, host_command_resend_response,
		     EC_VER_MASK(0));
#endif /* CONFIG_HOST_COMMAND_STATUS */

#if defined(CONFIG_AP_PWRSEQ_S0IX_COUNTER) || \
	defined(CONFIG_POWERSEQ_S0IX_COUNTER)
static enum ec_status
host_command_get_s0ix_cnt(struct host_cmd_handler_args *args)
{
	const struct ec_params_s0ix_cnt *p = args->params;
	struct ec_response_s0ix_cnt *r = args->response;

	if (p->flags & EC_S0IX_COUNTER_RESET) {
		atomic_clear(&s0ix_counter);
	}

	r->s0ix_counter = atomic_get(&s0ix_counter);

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_S0IX_COUNTER, host_command_get_s0ix_cnt,
		     EC_VER_MASK(0));
#endif

static enum ec_status
host_command_ap_fw_state(struct host_cmd_handler_args *args)
{
	const struct ec_params_ap_fw_state *p = args->params;

	ccprintf("AP_FW %x\n", p->state);
	args->response_size = 0;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_AP_FW_STATE, host_command_ap_fw_state,
		     EC_VER_MASK(0));
