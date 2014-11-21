/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command module for Chrome EC */

#include "ap_hang_detect.h"
#include "common.h"
#include "console.h"
#include "host_command.h"
#include "link_defs.h"
#include "lpc.h"
#include "shared_mem.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_HOSTCMD, outstr)
#define CPRINTS(format, args...) cprints(CC_HOSTCMD, format, ## args)

#define TASK_EVENT_CMD_PENDING TASK_EVENT_CUSTOM(1)

/* Maximum delay to skip printing repeated host command debug output */
#define HCDEBUG_MAX_REPEAT_DELAY (50 * MSEC)

static struct host_cmd_handler_args *pending_args;

/* Verify Boot Mode */
static int g_vboot_mode;

#ifndef CONFIG_LPC
/*
 * Simulated memory map.  Must be word-aligned, because some of the elements
 * in the memory map are words.
 */
static uint8_t host_memmap[EC_MEMMAP_SIZE] __aligned(4);
#endif

static enum {
	HCDEBUG_OFF,     /* No host command debug output */
	HCDEBUG_NORMAL,  /* Normal output mode; skips repeated commands */
	HCDEBUG_EVERY,   /* Print every command */
	HCDEBUG_PARAMS,  /* ... and print params for request/response */

	/* Number of host command debug modes */
	HCDEBUG_MODES
} hcdebug = HCDEBUG_NORMAL;

static const char * const hcdebug_mode_names[HCDEBUG_MODES] = {
	"off", "normal", "every", "params"};

#ifdef CONFIG_HOST_COMMAND_STATUS
/*
 * Indicates that a 'slow' command has sent EC_RES_IN_PROGRESS but hasn't
 * sent a final status (i.e. it is in progress)
 */
static uint8_t command_pending;

/* The result of the last 'slow' operation */
static uint8_t saved_result = EC_RES_UNAVAILABLE;
#endif

/*
 * Host command args passed to command handler.  Static to keep it off the
 * stack.  Note this means we can handle only one host command at a time.
 */
static struct host_cmd_handler_args args0;

/* Current host command packet from host, for protocol version 3+ */
static struct host_packet *pkt0;

uint8_t *host_get_memmap(int offset)
{
#ifdef CONFIG_LPC
	return lpc_get_memmap_range() + offset;
#else
	return host_memmap + offset;
#endif
}

int host_get_vboot_mode(void)
{
	return g_vboot_mode;
}

test_mockable void host_send_response(struct host_cmd_handler_args *args)
{
#ifdef CONFIG_HOST_COMMAND_STATUS
	/*
	 *
	 * If we are in interrupt context, then we are handling a get_status
	 * response or an immediate error which prevented us from processing
	 * the command. Note we can't check for the GET_COMMS_STATUS command in
	 * args->command because the original command value has now been
	 * overwritten.
	 *
	 * When a EC_CMD_RESEND_RESPONSE arrives we will supply this response
	 * to that command.
	 */
	if (!in_interrupt_context()) {
		if (command_pending) {
			/*
			 * We previously got EC_RES_IN_PROGRESS.  This must be
			 * the completion of that command, so stash the result
			 * code.
			 */
			CPRINTS("HC pending done, size=%d, result=%d",
				args->response_size, args->result);

			/*
			 * We don't support stashing response data, so mark the
			 * response as unavailable in that case.
			 */
			if (args->response_size != 0)
				saved_result = EC_RES_UNAVAILABLE;
			else
				saved_result = args->result;

			/*
			 * We can't send the response back to the host now
			 * since we already sent the in-progress response and
			 * the host is on to other things now.
			 */
			command_pending = 0;
			return;

		} else if (args->result == EC_RES_IN_PROGRESS) {
			command_pending = 1;
			CPRINTS("HC pending");
		}
	}
#endif
	args->send_response(args);
}

void host_command_received(struct host_cmd_handler_args *args)
{
	/*
	 * TODO(crosbug.com/p/23806): should warn if we already think we're in
	 * a command.
	 */

	/*
	 * If this is the reboot command, reboot immediately.  This gives the
	 * host processor a way to unwedge the EC even if it's busy with some
	 * other command.
	 */
	if (args->command == EC_CMD_REBOOT) {
		system_reset(SYSTEM_RESET_HARD);
		/* Reset should never return; if it does, post an error */
		args->result = EC_RES_ERROR;
	}

#ifdef CONFIG_AP_HANG_DETECT
	/* If hang detection is enabled, check stop on host command */
	hang_detect_stop_on_host_command();
#endif

	if (args->result) {
		; /* driver has signalled an error, respond now */
#ifdef CONFIG_HOST_COMMAND_STATUS
	} else if (args->command == EC_CMD_GET_COMMS_STATUS) {
		args->result = host_command_process(args);
#endif
	} else {
		/* Save the command */
		pending_args = args;

		/* Wake up the task to handle the command */
		task_set_event(TASK_ID_HOSTCMD, TASK_EVENT_CMD_PENDING, 0);
		return;
	}

	/*
	 * TODO (crosbug.com/p/29315): This is typically running in interrupt
	 * context, so it woud be better not to send the response here, and to
	 * let the host command task send the response.
	 */
	/* Send the response now */
	host_send_response(args);
}

void host_packet_respond(struct host_cmd_handler_args *args)
{
	struct ec_host_response *r = (struct ec_host_response *)pkt0->response;
	uint8_t *out = (uint8_t *)pkt0->response;
	int csum = 0;
	int i;

	/* Clip result size to what we can accept */
	if (args->result) {
		/* Error results don't have data */
		args->response_size = 0;
	} else if (args->response_size > pkt0->response_max - sizeof(*r)) {
		/* Too much data */
		args->result = EC_RES_RESPONSE_TOO_BIG;
		args->response_size = 0;
	}

	/* Fill in response struct */
	r->struct_version = EC_HOST_RESPONSE_VERSION;
	r->checksum = 0;
	r->result = args->result;
	r->data_len = args->response_size;
	r->reserved = 0;

	/* Start checksum; this also advances *out to end of response */
	for (i = sizeof(*r); i > 0; i--)
		csum += *out++;

	/* Checksum response data, if any */
	for (i = args->response_size; i > 0; i--)
		csum += *out++;

	/* Write checksum field so the entire packet sums to 0 */
	r->checksum = (uint8_t)(-csum);

	pkt0->response_size = sizeof(*r) + r->data_len;
	pkt0->driver_result = args->result;
	pkt0->send_response(pkt0);
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

void host_packet_receive(struct host_packet *pkt)
{
	const struct ec_host_request *r =
		(const struct ec_host_request *)pkt->request;
	const uint8_t *in = (const uint8_t *)pkt->request;
	uint8_t *itmp = (uint8_t *)pkt->request_temp;
	int csum = 0;
	int i;

	/* Track the packet we're handling */
	pkt0 = pkt;

	/* If driver indicates error, don't even look at the data */
	if (pkt->driver_result) {
		args0.result = pkt->driver_result;
		goto host_packet_bad;
	}

	if (pkt->request_size < sizeof(*r)) {
		/* Packet too small for even a header */
		args0.result = EC_RES_REQUEST_TRUNCATED;
		goto host_packet_bad;
	}

	if (pkt->request_size > pkt->request_max) {
		/* Got a bigger request than the interface can handle */
		args0.result = EC_RES_REQUEST_TRUNCATED;
		goto host_packet_bad;
	}

	/*
	 * Response buffer needs to be big enough for a header.  If it's not
	 * we can't even return an error packet.
	 */
	ASSERT(pkt->response_max >= sizeof(struct ec_host_response));

	/* Start checksum and copy request header if necessary */
	if (pkt->request_temp) {
		/* Copy to temp buffer and checksum */
		for (i = sizeof(*r); i > 0; i--) {
			*itmp = *in++;
			csum += *itmp++;
		}
		r = (const struct ec_host_request *)pkt->request_temp;
	} else {
		/* Just checksum */
		for (i = sizeof(*r); i > 0; i--)
			csum += *in++;
	}

	if (r->struct_version != EC_HOST_REQUEST_VERSION) {
		/* Request header we don't know how to handle */
		args0.result = EC_RES_INVALID_HEADER;
		goto host_packet_bad;
	}

	if (pkt->request_size < sizeof(*r) + r->data_len) {
		/*
		 * Packet too small for expected params.  Note that it's ok if
		 * the received packet data is too big; some interfaces may pad
		 * the data at the end (SPI) or may not know how big the
		 * received data is (LPC).
		 */
		args0.result = EC_RES_REQUEST_TRUNCATED;
		goto host_packet_bad;
	}

	/* Copy request data and validate checksum */
	if (pkt->request_temp) {
		/* Params go in temporary buffer */
		args0.params = itmp;

		/* Copy request data and checksum */
		for (i = r->data_len; i > 0; i--) {
			*itmp = *in++;
			csum += *itmp++;
		}
	} else {
		/* Params read directly from request */
		args0.params = in;

		/* Just checksum */
		for (i = r->data_len; i > 0; i--)
			csum += *in++;
	}

	/* Validate checksum */
	if ((uint8_t)csum) {
		args0.result = EC_RES_INVALID_CHECKSUM;
		goto host_packet_bad;
	}

	/* Set up host command handler args */
	args0.send_response = host_packet_respond;
	args0.command = r->command;
	args0.version = r->command_version;
	args0.params_size = r->data_len;
	args0.response = (struct ec_host_response *)(pkt->response) + 1;
	args0.response_max = pkt->response_max -
		sizeof(struct ec_host_response);
	args0.response_size = 0;
	args0.result = EC_RES_SUCCESS;

	/* Chain to host command received */
	host_command_received(&args0);
	return;

host_packet_bad:
	/*
	 * TODO (crosbug.com/p/29315): This is typically running in interrupt
	 * context, so it woud be better not to send the response here, and to
	 * let the host command task send the response.
	 */
	/* Improperly formed packet from host, so send an error response */
	host_packet_respond(&args0);
}

/**
 * Find a command by command number.
 *
 * @param command	Command number to find
 * @return The command structure, or NULL if no match found.
 */
static const struct host_command *find_host_command(int command)
{
	const struct host_command *cmd;

	for (cmd = __hcmds; cmd < __hcmds_end; cmd++) {
		if (command == cmd->command)
			return cmd;
	}

	return NULL;
}

static void host_command_init(void)
{
	/* Initialize memory map ID area */
	host_get_memmap(EC_MEMMAP_ID)[0] = 'E';
	host_get_memmap(EC_MEMMAP_ID)[1] = 'C';
	*host_get_memmap(EC_MEMMAP_ID_VERSION) = 1;
	*host_get_memmap(EC_MEMMAP_EVENTS_VERSION) = 1;

#ifdef CONFIG_HOSTCMD_EVENTS
	host_set_single_event(EC_HOST_EVENT_INTERFACE_READY);
	CPRINTS("hostcmd init 0x%x", host_get_events());
#endif
}

void host_command_task(void)
{
	host_command_init();

	while (1) {
		/* Wait for the next command event */
		int evt = task_wait_event(-1);

		/* Process it */
		if ((evt & TASK_EVENT_CMD_PENDING) && pending_args) {
			pending_args->result =
					host_command_process(pending_args);
			host_send_response(pending_args);
		}
	}
}

/*****************************************************************************/
/* Host commands */

/* TODO(crosbug.com/p/11223): Remove this once the kernel no longer cares */
static int host_command_proto_version(struct host_cmd_handler_args *args)
{
	struct ec_response_proto_version *r = args->response;

	r->version = EC_PROTO_VERSION;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PROTO_VERSION,
		     host_command_proto_version,
		     EC_VER_MASK(0));

static int host_command_hello(struct host_cmd_handler_args *args)
{
	const struct ec_params_hello *p = args->params;
	struct ec_response_hello *r = args->response;
	uint32_t d = p->in_data;

	r->out_data = d + 0x01020304;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_HELLO,
		     host_command_hello,
		     EC_VER_MASK(0));

static int host_command_read_test(struct host_cmd_handler_args *args)
{
	const struct ec_params_read_test *p = args->params;
	struct ec_response_read_test *r = args->response;

	int offset = p->offset;
	int size = p->size / sizeof(uint32_t);
	int i;

	if (size > ARRAY_SIZE(r->data))
		return EC_RES_ERROR;

	for (i = 0; i < size; i++)
		r->data[i] = offset + i;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_READ_TEST,
		     host_command_read_test,
		     EC_VER_MASK(0));

#ifndef CONFIG_LPC
/*
 * Host command to read memory map is not needed on LPC, because LPC can
 * directly map the data to the host's memory space.
 */
static int host_command_read_memmap(struct host_cmd_handler_args *args)
{
	const struct ec_params_read_memmap *p = args->params;

	/* Copy params out of data before we overwrite it with output */
	uint8_t offset = p->offset;
	uint8_t size = p->size;

	if (size > EC_MEMMAP_SIZE || offset > EC_MEMMAP_SIZE ||
	    offset + size > EC_MEMMAP_SIZE)
		return EC_RES_INVALID_PARAM;

	memcpy(args->response, host_get_memmap(offset), size);
	args->response_size = size;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_READ_MEMMAP,
		     host_command_read_memmap,
		     EC_VER_MASK(0));
#endif

static int host_command_get_cmd_versions(struct host_cmd_handler_args *args)
{
	const struct ec_params_get_cmd_versions *p = args->params;
	struct ec_response_get_cmd_versions *r = args->response;

	const struct host_command *cmd = find_host_command(p->cmd);

	if (!cmd)
		return EC_RES_INVALID_PARAM;

	r->version_mask = cmd->version_mask;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_CMD_VERSIONS,
		     host_command_get_cmd_versions,
		     EC_VER_MASK(0));

/**
 * Print debug output for the host command request, before it's processed.
 *
 * @param args		Host command args
 */
static void host_command_debug_request(struct host_cmd_handler_args *args)
{
	static int hc_prev_cmd;
	static uint64_t hc_prev_time;

	/*
	 * In normal output mode, skip printing repeats of the same command
	 * that occur in rapid succession - such as flash commands during
	 * software sync.
	 */
	if (hcdebug == HCDEBUG_NORMAL) {
		uint64_t t = get_time().val;
		if (args->command == hc_prev_cmd &&
		    t - hc_prev_time < HCDEBUG_MAX_REPEAT_DELAY) {
			hc_prev_time = t;
			CPUTS("+");
			return;
		}
		hc_prev_time = t;
		hc_prev_cmd = args->command;
	}

	if (hcdebug >= HCDEBUG_PARAMS && args->params_size)
		CPRINTS("HC 0x%02x.%d:%.*h", args->command,
			args->version, args->params_size, args->params);
	else
		CPRINTS("HC 0x%02x", args->command);
}

enum ec_status host_command_process(struct host_cmd_handler_args *args)
{
	const struct host_command *cmd;
	int rv;

	if (hcdebug)
		host_command_debug_request(args);

#ifdef HAS_TASK_PDCMD
	if (args->command >= EC_CMD_PASSTHRU_OFFSET(1) &&
	    args->command <= EC_CMD_PASSTHRU_MAX(1)) {
		rv = pd_host_command(args->command - EC_CMD_PASSTHRU_OFFSET(1),
				     args->version,
				     args->params, args->params_size,
				     args->response, args->response_max);
		if (rv >= 0) {
			/* Success; store actual response size */
			args->response_size = rv;
			rv = EC_SUCCESS;
		} else {
			/* Failure, returned as negative error code */
			rv = -rv;
		}
	} else
#endif
	{
		cmd = find_host_command(args->command);
		if (!cmd)
			rv = EC_RES_INVALID_COMMAND;
		else if (!(EC_VER_MASK(args->version) & cmd->version_mask))
			rv = EC_RES_INVALID_VERSION;
		else
			rv = cmd->handler(args);
	}

	if (rv != EC_RES_SUCCESS)
		CPRINTS("HC err %d", rv);

	if (hcdebug >= HCDEBUG_PARAMS && args->response_size)
		CPRINTS("HC resp:%.*h", args->response_size,
			args->response);

	return rv;
}

#ifdef CONFIG_HOST_COMMAND_STATUS
/* Returns current command status (busy or not) */
static int host_command_get_comms_status(struct host_cmd_handler_args *args)
{
	struct ec_response_get_comms_status *r = args->response;

	r->flags = command_pending ? EC_COMMS_STATUS_PROCESSING : 0;
	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_GET_COMMS_STATUS,
		     host_command_get_comms_status,
		     EC_VER_MASK(0));

/* Resend the last saved response */
static int host_command_resend_response(struct host_cmd_handler_args *args)
{
	/* Handle resending response */
	args->result = saved_result;
	args->response_size = 0;

	saved_result = EC_RES_UNAVAILABLE;

	return EC_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_RESEND_RESPONSE,
		     host_command_resend_response,
		     EC_VER_MASK(0));
#endif /* CONFIG_HOST_COMMAND_STATUS */


static int host_command_entering_mode(struct host_cmd_handler_args *args)
{
	struct ec_params_entering_mode *param =
		(struct ec_params_entering_mode *)args->params;
	args->response_size = 0;
	g_vboot_mode = param->vboot_mode;
	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_ENTERING_MODE,
		host_command_entering_mode, EC_VER_MASK(0));

/* Returns what we tell it to. */
static int host_command_test_protocol(struct host_cmd_handler_args *args)
{
	const struct ec_params_test_protocol *p = args->params;
	struct ec_response_test_protocol *r = args->response;
	int copy_len = MIN(p->ret_len, sizeof(r->buf)); /* p,r bufs same size */

	memset(r->buf, 0, sizeof(r->buf));
	memcpy(r->buf, p->buf, copy_len);
	args->response_size = p->ret_len;

	return p->ec_result;
}
DECLARE_HOST_COMMAND(EC_CMD_TEST_PROTOCOL,
		     host_command_test_protocol,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_HOSTCMD
static int parse_byte(char *b, uint8_t *out)
{
	int i;
	*out = 0;
	for (i = 0; i < 2; ++i) {
		*out *= 16;
		if (*b >= '0' && *b <= '9')
			*out += *b - '0';
		else if (*b >= 'a' && *b <= 'f')
			*out += *b - 'a' + 10;
		else if (*b >= 'A' && *b <= 'F')
			*out += *b - 'A' + 10;
		else
			return EC_ERROR_INVAL;
		++b;
	}
	return EC_SUCCESS;
}

static int parse_params(char *s, uint8_t *params)
{
	int len = 0;

	while (*s) {
		if (parse_byte(s, params))
			return -1;
		s += 2;
		params++;
		len++;
	}
	return len;
}

static int command_host_command(int argc, char **argv)
{
	struct host_cmd_handler_args args;
	char *cmd_params;
	enum ec_status res;
	char *e;
	int rv;

	/* Use shared memory for command params space */
	if (shared_mem_acquire(EC_PROTO2_MAX_PARAM_SIZE, &cmd_params)) {
		ccputs("Can't acquire shared memory buffer.\n");
		return EC_ERROR_UNKNOWN;
	}

	/* Assume no version or params unless proven otherwise */
	args.version = 0;
	args.params_size = 0;
	args.params = cmd_params;

	if (argc < 2) {
		shared_mem_release(cmd_params);
		return EC_ERROR_PARAM_COUNT;
	}

	args.command = strtoi(argv[1], &e, 0);
	if (*e) {
		shared_mem_release(cmd_params);
		return EC_ERROR_PARAM1;
	}

	if (argc > 2) {
		args.version = strtoi(argv[2], &e, 0);
		if (*e) {
			shared_mem_release(cmd_params);
			return EC_ERROR_PARAM2;
		}
	}

	if (argc > 3) {
		rv = parse_params(argv[3], cmd_params);
		if (rv < 0) {
			shared_mem_release(cmd_params);
			return EC_ERROR_PARAM3;
		}
		args.params_size = rv;
	}

	args.response = cmd_params;
	args.response_max = EC_PROTO2_MAX_PARAM_SIZE;
	args.response_size = 0;

	res = host_command_process(&args);

	if (res != EC_RES_SUCCESS)
		ccprintf("Command returned %d\n", res);
	else if (args.response_size)
		ccprintf("Response: %.*h\n", args.response_size, cmd_params);
	else
		ccprintf("Command succeeded; no response.\n");

	shared_mem_release(cmd_params);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hostcmd, command_host_command,
			"cmd ver param",
			"Fake host command",
			NULL);
#endif /* CONFIG_CMD_HOSTCMD */

static int command_hcdebug(int argc, char **argv)
{
	if (argc > 1) {
		int i;

		for (i = 0; i < HCDEBUG_MODES; i++) {
			if (!strcasecmp(argv[1], hcdebug_mode_names[i])) {
				hcdebug = i;
				break;
			}
		}
		if (i == HCDEBUG_MODES)
			return EC_ERROR_PARAM1;
	}

	ccprintf("Host command debug mode is %s\n",
		 hcdebug_mode_names[hcdebug]);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hcdebug, command_hcdebug,
			"hcdebug [off | normal | every | params]",
			"Set host command debug output mode",
			NULL);
