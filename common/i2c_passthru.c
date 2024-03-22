/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C passthru support for Chrome EC */

#include "battery.h"
#include "battery_smart.h"
#include "console.h"
#include "host_command.h"
#include "i2c.h"
#include "system.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"
#include "virtual_battery.h"

#ifdef CONFIG_ZEPHYR
#include "i2c/i2c.h"
#endif

#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ##args)

#ifdef CONFIG_I2C_DEBUG_PASSTHRU
#define PTHRUPRINTS(format, args...) CPRINTS("I2C_PTHRU " format, ##args)
#define PTHRUPRINTF(format, args...) CPRINTF(format, ##args)
#else
#define PTHRUPRINTS(format, args...)
#define PTHRUPRINTF(format, args...)
#endif

#define EC_PARAMS_I2C_PASSTHRU_PORT(args) \
	(((struct ec_params_i2c_passthru *)(args->params))->port)

static uint8_t port_protected[I2C_PORT_COUNT + I2C_BITBANG_PORT_COUNT];

/**
 * Perform the voluminous checking required for this message
 *
 * @param args	Arguments
 * @return 0 if OK, EC_RES_INVALID_PARAM on error
 */
static int check_i2c_params(const struct host_cmd_handler_args *args)
{
	const struct ec_params_i2c_passthru *params = args->params;
	const struct ec_params_i2c_passthru_msg *msg;
	int read_len = 0, write_len = 0;
	unsigned int size;
	int msgnum;

#ifdef CONFIG_I2C_PASSTHRU_RESTRICTED
	uint8_t cmd_id = 0xff;
	const uint8_t *out;
#endif

	if (args->params_size < sizeof(*params)) {
		PTHRUPRINTS("no params, params_size=%d, need at least %d",
			    args->params_size, sizeof(*params));
		return EC_RES_INVALID_PARAM;
	}
	size = sizeof(*params) + params->num_msgs * sizeof(*msg);
	if (args->params_size < size) {
		PTHRUPRINTS("params_size=%d, need at least %d",
			    args->params_size, size);
		return EC_RES_INVALID_PARAM;
	}

#ifdef CONFIG_I2C_PASSTHRU_RESTRICTED
	out = (uint8_t *)args->params + size;
#endif

	/* Loop and process messages */;
	for (msgnum = 0, msg = params->msg; msgnum < params->num_msgs;
	     msgnum++, msg++) {
		unsigned int addr_flags = msg->addr_flags;

		PTHRUPRINTS("port=%d, %s, addr=0x%x(7-bit), len=%d",
			    params->port,
			    addr_flags & EC_I2C_FLAG_READ ? "read" : "write",
			    addr_flags & EC_I2C_ADDR_MASK, msg->len);

		if (addr_flags & EC_I2C_FLAG_READ) {
			read_len += msg->len;
		} else {
#ifdef CONFIG_I2C_PASSTHRU_RESTRICTED
			cmd_id = out[write_len];
#endif
			write_len += msg->len;
		}
#ifdef CONFIG_I2C_PASSTHRU_RESTRICTED
		if (system_is_locked()) {
			const struct i2c_cmd_desc_t cmd_desc = {
				.port = params->port,
				.addr_flags = addr_flags,
				.cmd = cmd_id,
			};
			if (!board_allow_i2c_passthru(&cmd_desc))
				return EC_RES_ACCESS_DENIED;
		}
#endif
	}

	/* Check there is room for the data */
	if (args->response_max <
	    sizeof(struct ec_response_i2c_passthru) + read_len) {
		PTHRUPRINTS("overflow1");
		return EC_RES_INVALID_PARAM;
	}

	/* Must have bytes to write */
	if (args->params_size < size + write_len) {
		PTHRUPRINTS("overflow2");
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}

#ifdef CONFIG_I2C_VIRTUAL_BATTERY
static inline int is_i2c_port_virtual_battery(int port)
{
#ifdef CONFIG_ZEPHYR
	/* For Zephyr compare the actual device, which will be used in
	 * i2c_transfer function.
	 */
	return (i2c_get_device_for_port(port) ==
		i2c_get_device_for_port(I2C_PORT_VIRTUAL_BATTERY));
#else
	return (port == I2C_PORT_VIRTUAL_BATTERY);
#endif
}
#endif /* CONFIG_I2C_VIRTUAL_BATTERY */

static enum ec_status i2c_command_passthru(struct host_cmd_handler_args *args)
{
#ifdef CONFIG_ZEPHYR
	/* For Zephyr, convert the received remote port number to a port number
	 * used in EC.
	 */
	EC_PARAMS_I2C_PASSTHRU_PORT(args) = i2c_get_port_from_remote_port(
		EC_PARAMS_I2C_PASSTHRU_PORT(args));
#endif
	const struct ec_params_i2c_passthru *params = args->params;
	const struct ec_params_i2c_passthru_msg *msg;
	struct ec_response_i2c_passthru *resp = args->response;
	const struct i2c_port_t *i2c_port;
	const uint8_t *out;
	int in_len;
	int ret, i;
	int port_is_locked = 0;

#ifdef CONFIG_BATTERY_CUT_OFF
	/*
	 * Some batteries would wake up after cut-off if we talk to it.
	 */
	if (battery_is_cut_off())
		return EC_RES_ACCESS_DENIED;
#endif

	i2c_port = get_i2c_port(params->port);
	if (!i2c_port)
		return EC_RES_INVALID_PARAM;

	ret = check_i2c_params(args);
	if (ret)
		return ret;

	if (port_protected[params->port]) {
		if (!i2c_port->passthru_allowed)
			return EC_RES_ACCESS_DENIED;

		for (i = 0; i < params->num_msgs; i++) {
			if (!i2c_port->passthru_allowed(
				    i2c_port, params->msg[i].addr_flags))
				return EC_RES_ACCESS_DENIED;
		}
	}

	/* Loop and process messages */
	resp->i2c_status = 0;
	out = (uint8_t *)args->params + sizeof(*params) +
	      params->num_msgs * sizeof(*msg);
	in_len = 0;

	for (resp->num_msgs = 0, msg = params->msg;
	     resp->num_msgs < params->num_msgs; resp->num_msgs++, msg++) {
		int xferflags = I2C_XFER_START;
		int read_len = 0, write_len = 0;
		int rv = 1;

		/* Have to remove the EC flags from the address flags */
		uint16_t addr_flags = msg->addr_flags & EC_I2C_ADDR_MASK;

		if (msg->addr_flags & EC_I2C_FLAG_READ)
			read_len = msg->len;
		else
			write_len = msg->len;

		/* Set stop bit for last message */
		if (resp->num_msgs == params->num_msgs - 1)
			xferflags |= I2C_XFER_STOP;

#ifdef CONFIG_I2C_VIRTUAL_BATTERY
		if (is_i2c_port_virtual_battery(params->port) &&
		    addr_flags == VIRTUAL_BATTERY_ADDR_FLAGS) {
			if (virtual_battery_handler(resp, in_len, &rv,
						    xferflags, read_len,
						    write_len, out))
				break;
		}
#endif
		/* Transfer next message */
		PTHRUPRINTS("xfer port=%x addr=0x%x rlen=%d flags=0x%x",
			    params->port, addr_flags, read_len, xferflags);
		if (write_len) {
			PTHRUPRINTF("  out:");
			for (i = 0; i < write_len; i++)
				PTHRUPRINTF(" 0x%02x", out[i]);
			PTHRUPRINTF("\n");
		}
		if (rv) {
			if (!port_is_locked)
				i2c_lock(params->port, (port_is_locked = 1));
			rv = i2c_xfer_unlocked(params->port, addr_flags, out,
					       write_len, &resp->data[in_len],
					       read_len, xferflags);
		}

		if (rv) {
			/* Driver will have sent a stop bit here */
			if (rv == EC_ERROR_TIMEOUT)
				resp->i2c_status = EC_I2C_STATUS_TIMEOUT;
			else
				resp->i2c_status = EC_I2C_STATUS_NAK;
			break;
		}

		in_len += read_len;
		out += write_len;
	}
	args->response_size = sizeof(*resp) + in_len;

	/* Unlock port */
	if (port_is_locked)
		i2c_lock(params->port, 0);

	/*
	 * Return success even if transfer failed so response is sent.  Host
	 * will check message status to determine the transfer result.
	 */
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_I2C_PASSTHRU, i2c_command_passthru, EC_VER_MASK(0));

__test_only void i2c_passthru_protect_reset(void)
{
	memset(port_protected, 0, sizeof(port_protected));
}

static void i2c_passthru_protect_port(uint32_t port)
{
	if (port < ARRAY_SIZE(port_protected))
		port_protected[port] = 1;
	else
		PTHRUPRINTS("Invalid I2C port %d to be protected\n", port);
}

static void i2c_passthru_protect_tcpc_ports(void)
{
#ifdef CONFIG_USB_PD_PORT_MAX_COUNT
	int i;

	/*
	 * If WP is not enabled i.e. system is not locked leave the tunnels open
	 * so that factory line can do updates without a new RO BIOS.
	 */
	if (!system_is_locked()) {
		CPRINTS("System unlocked, TCPC I2C tunnels may be unprotected");
		return;
	}

	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
#ifdef CONFIG_USB_PD_CONTROLLER
		/* TODO:b/294550823 - Create an allow list for I2C passthru
		 * commands */
#else
		/* TCPC tunnel not configured. No need to protect anything */
		if (!I2C_STRIP_FLAGS(tcpc_config[i].i2c_info.addr_flags))
			continue;
		i2c_passthru_protect_port(tcpc_config[i].i2c_info.port);
#endif
	}
#endif
}

static enum ec_status
i2c_command_passthru_protect(struct host_cmd_handler_args *args)
{
#ifdef CONFIG_ZEPHYR
	/* For Zephyr, convert the received remote port number to a port number
	 * used in EC.
	 */
	EC_PARAMS_I2C_PASSTHRU_PORT(args) = i2c_get_port_from_remote_port(
		EC_PARAMS_I2C_PASSTHRU_PORT(args));
#endif
	const struct ec_params_i2c_passthru_protect *params = args->params;
	struct ec_response_i2c_passthru_protect *resp = args->response;

	if (args->params_size < sizeof(*params)) {
		PTHRUPRINTS("protect no params, params_size=%d, ",
			    args->params_size);
		return EC_RES_INVALID_PARAM;
	}

	/*
	 * When calling the subcmd to protect all tcpcs, the i2c port isn't
	 * expected to be set in the args. So, putting a check here to avoid
	 * the get_i2c_port return error.
	 */
	if (params->subcmd == EC_CMD_I2C_PASSTHRU_PROTECT_ENABLE_TCPCS) {
		if (IS_ENABLED(CONFIG_USB_POWER_DELIVERY) &&
		    !IS_ENABLED(CONFIG_USB_PD_TCPM_STUB))
			i2c_passthru_protect_tcpc_ports();
		return EC_RES_SUCCESS;
	}

	if (!get_i2c_port(params->port)) {
		PTHRUPRINTS("protect invalid port %d", params->port);
		return EC_RES_INVALID_PARAM;
	}

	if (params->subcmd == EC_CMD_I2C_PASSTHRU_PROTECT_STATUS) {
		if (args->response_max < sizeof(*resp)) {
			PTHRUPRINTS("protect no response, "
				    "response_max=%d, need at least %d",
				    args->response_max, sizeof(*resp));
			return EC_RES_INVALID_PARAM;
		}

		resp->status = port_protected[params->port];
		args->response_size = sizeof(*resp);
	} else if (params->subcmd == EC_CMD_I2C_PASSTHRU_PROTECT_ENABLE) {
		i2c_passthru_protect_port(params->port);
	} else {
		return EC_RES_INVALID_COMMAND;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_I2C_PASSTHRU_PROTECT, i2c_command_passthru_protect,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_I2C_PROTECT
static int command_i2cprotect(int argc, const char **argv)
{
	if (argc == 1) {
		int i, port;

		for (i = 0; i < i2c_ports_used; i++) {
			port = i2c_ports[i].port;
			ccprintf("Port %d: %s\n", port,
				 port_protected[port] ? "Protected" :
							"Unprotected");
		}
	} else if (argc == 2) {
		int port;
		char *e;

		port = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		if (!get_i2c_port(port)) {
			ccprintf("i2c passthru protect invalid port %d\n",
				 port);
			return EC_RES_INVALID_PARAM;
		}

		port_protected[port] = 1;
	} else {
		return EC_ERROR_PARAM_COUNT;
	}

	return EC_RES_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2cprotect, command_i2cprotect, "[port]",
			"Protect I2C bus");
#endif
