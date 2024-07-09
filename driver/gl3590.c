/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "gl3590.h"
#include "hooks.h"
#include "i2c.h"
#include "pwr_defs.h"
#include "system.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ##args)

/* GL3590 is unique in terms of i2c_read, since it doesn't support repeated
 * start sequence. One need to issue two separate transactions - first is write
 * with a register offset, then after a delay second transaction is actual read.
 */
int gl3590_read(int hub, uint8_t reg, uint8_t *data, int count)
{
	int rv;
	struct uhub_i2c_iface_t *uhub_p = &uhub_config[hub];

	i2c_lock(uhub_p->i2c_host_port, 1);
	rv = i2c_xfer_unlocked(uhub_p->i2c_host_port, uhub_p->i2c_addr, &reg, 1,
			       NULL, 0, I2C_XFER_SINGLE);
	i2c_lock(uhub_p->i2c_host_port, 0);

	if (rv)
		return rv;

	/* GL3590 requires at least 1ms between consecutive i2c transactions */
	udelay(MSEC);

	i2c_lock(uhub_p->i2c_host_port, 1);
	rv = i2c_xfer_unlocked(uhub_p->i2c_host_port, uhub_p->i2c_addr, NULL, 0,
			       data, count, I2C_XFER_SINGLE);
	i2c_lock(uhub_p->i2c_host_port, 0);

	/*
	 * GL3590 requires at least 1ms between consecutive i2c transactions.
	 * Make sure that we are safe across API calls.
	 */
	udelay(MSEC);

	return rv;
};

int gl3590_write(int hub, uint8_t reg, uint8_t *data, int count)
{
	int rv;
	uint8_t buf[5];
	struct uhub_i2c_iface_t *uhub_p = &uhub_config[hub];

	/* GL3590 registers accept 4 bytes at max */
	if (count > (sizeof(buf) - 1)) {
		CPRINTF("Too many bytes to write");
		return EC_ERROR_INVAL;
	}

	buf[0] = reg;
	memcpy(&buf[1], data, count);

	i2c_lock(uhub_p->i2c_host_port, 1);
	rv = i2c_xfer_unlocked(uhub_p->i2c_host_port, uhub_p->i2c_addr, buf,
			       count + 1, NULL, 0, I2C_XFER_SINGLE);
	i2c_lock(uhub_p->i2c_host_port, 0);

	/*
	 * GL3590 requires at least 1ms between consecutive i2c transactions.
	 * Make sure that we are safe across API calls.
	 */
	udelay(MSEC);

	return rv;
}

/*
 * Basic initialization of GL3590 I2C interface.
 *
 * Please note, that I2C interface is online not earlier than ~50ms after
 * RESETJ# is deasserted. Platform should check that PGREEN_A_SMD pin is
 * asserted. This init function shouldn't be invoked until that time.
 */
void gl3590_init(int hub)
{
	uint8_t tmp;
	struct uhub_i2c_iface_t *uhub_p = &uhub_config[hub];

	if (uhub_p->initialized)
		return;

	if (gl3590_read(hub, GL3590_HUB_MODE_REG, &tmp, 1)) {
		CPRINTF("GL3590: Cannot read HUB_MODE register");
		return;
	}
	if ((tmp & GL3590_HUB_MODE_I2C_READY) == 0)
		CPRINTF("GL3590 interface isn't ready, consider deferring "
			"this init\n");

	/* Deassert INTR# signal */
	tmp = GL3590_INT_CLEAR;
	if (gl3590_write(hub, GL3590_INT_REG, &tmp, 1)) {
		CPRINTF("GL3590: Cannot write to INT register");
		return;
	};

	uhub_p->initialized = 1;
}

/*
 * GL3590 chip may drive I2C_SDA and I2C_SCL lines for 200ms (max) after it is
 * released from reset (through gpio de-assertion in main()). In order to avoid
 * broken I2C transactions, we need to add an extra delay before any activity on
 * the I2C bus in the system.
 */
static void gl3590_delay_on_init(void)
{
	CPRINTS("Applying 200ms delay for GL3590 to release I2C lines");
	udelay(200 * MSEC);
}
DECLARE_HOOK(HOOK_INIT, gl3590_delay_on_init, HOOK_PRIO_INIT_I2C - 1);

void gl3590_irq_handler(int hub)
{
	uint8_t buf = 0;
	uint8_t res_reg[2];
	struct uhub_i2c_iface_t *uhub_p = &uhub_config[hub];

	if (!uhub_p->initialized)
		return;

	/* Verify that irq is pending */
	if (gl3590_read(hub, GL3590_INT_REG, &buf, sizeof(buf))) {
		CPRINTF("Cannot read from the host hub i2c\n");
		goto exit;
	}

	if ((buf & GL3590_INT_PENDING) == 0) {
		CPRINTF("Invalid hub event\n");
		goto exit;
	}

	/* Get the hub event reason */
	if (gl3590_read(hub, GL3590_RESPONSE_REG, res_reg, sizeof(res_reg))) {
		CPRINTF("Cannot read from the host hub i2c\n");
		goto exit;
	}

	if ((res_reg[0] & GL3590_RESPONSE_REG_SYNC_MASK) == 0)
		CPRINTF("Host hub response: ");
	else
		CPRINTF("Host hub event! ");

	switch (res_reg[0]) {
	case 0x0:
		CPRINTF("No response");
		break;
	case 0x1:
		CPRINTF("Successful");
		break;
	case 0x2:
		CPRINTF("Invalid command");
		break;
	case 0x3:
		CPRINTF("Invalid arguments");
		break;
	case 0x4:
		CPRINTF("Invalid port: %d", res_reg[1]);
		break;
	case 0x5:
		CPRINTF("Command not completed");
		break;
	case 0x80:
		CPRINTF("Reset complete");
		break;
	case 0x81:
		CPRINTF("Power operation mode change");
		break;
	case 0x82:
		CPRINTF("Connect change");
		break;
	case 0x83:
		CPRINTF("Error on the specific port");
		break;
	case 0x84:
		CPRINTF("Hub state change");
		break;
	case 0x85:
		CPRINTF("SetFeature PORT_POWER failure");
		break;
	default:
		CPRINTF("Unknown value: 0x%0x", res_reg[0]);
	}
	CPRINTF("\n");

	if (res_reg[1])
		CPRINTF("Affected port %d\n", res_reg[1]);

exit:
	/* Try to clear interrupt */
	buf = GL3590_INT_CLEAR;
	gl3590_write(hub, GL3590_INT_REG, &buf, sizeof(buf));
}

enum ec_error_list gl3590_ufp_pwr(int hub, struct pwr_con_t *pwr)
{
	uint8_t hub_sts, hub_mode;
	int rv = 0;
	struct uhub_i2c_iface_t *uhub_p = &uhub_config[hub];

	if (!uhub_p->initialized)
		return EC_ERROR_HW_INTERNAL;

	if (gl3590_read(hub, GL3590_HUB_STS_REG, &hub_sts, sizeof(hub_sts))) {
		CPRINTF("Error reading HUB_STS %d\n", rv);
		return EC_ERROR_BUSY;
	}

	pwr->volts = 5;

	switch ((hub_sts & GL3590_HUB_STS_HOST_PWR_MASK) >>
		GL3590_HUB_STS_HOST_PWR_SHIFT) {
	case GL3590_DEFAULT_HOST_PWR_SRC:
		if (gl3590_read(hub, GL3590_HUB_MODE_REG, &hub_mode,
				sizeof(hub_mode))) {
			CPRINTF("Error reading HUB_MODE %d\n", rv);
			return EC_ERROR_BUSY;
		}
		if (hub_mode & GL3590_HUB_MODE_USB3_EN) {
			pwr->milli_amps = 900;
			return EC_SUCCESS;
		} else if (hub_mode & GL3590_HUB_MODE_USB2_EN) {
			pwr->milli_amps = 500;
			return EC_SUCCESS;
		} else {
			CPRINTF("GL3590: Neither USB3 nor USB2 hubs "
				"configured\n");
			return EC_ERROR_HW_INTERNAL;
		}
	case GL3590_1_5_A_HOST_PWR_SRC:
		pwr->milli_amps = 1500;
		return EC_SUCCESS;
	case GL3590_3_0_A_HOST_PWR_SRC:
		pwr->milli_amps = 3000;
		return EC_SUCCESS;
	default:
		CPRINTF("GL3590: Unkown host power source %d\n", hub_sts);
		return EC_ERROR_UNKNOWN;
	}
}

#define GL3590_EN_PORT_MAX_RETRY_COUNT 10

int gl3590_enable_ports(int hub, uint8_t port_mask, bool enable)
{
	uint8_t buf[4] = { 0 };
	uint8_t en_mask = 0;
	uint8_t tmp;
	int rv, i;
	struct uhub_i2c_iface_t *uhub_p = &uhub_config[hub];

	if (!uhub_p->initialized)
		return EC_ERROR_HW_INTERNAL;

	if (!enable)
		en_mask = port_mask;

	buf[0] = en_mask;
	buf[2] = port_mask;

	for (i = 1; i <= GL3590_EN_PORT_MAX_RETRY_COUNT; i++) {
		rv = gl3590_write(hub, GL3590_PORT_DISABLED_REG, buf,
				  sizeof(buf));
		if (rv)
			return rv;

		crec_usleep(200 * MSEC);

		/* Verify whether port is enabled/disabled */
		rv = gl3590_read(hub, GL3590_PORT_EN_STS_REG, &tmp, 1);
		if (rv)
			return rv;

		if (enable && ((tmp & port_mask) == port_mask))
			break;
		if (!enable && ((tmp & port_mask) == 0))
			break;

		if (i > GL3590_EN_PORT_MAX_RETRY_COUNT) {
			CPRINTF("GL3590: Failed to %s port 0x%x\n",
				enable ? "enable" : "disable", port_mask);
			return EC_ERROR_HW_INTERNAL;
		}

		CPRINTF("GL3590: Port %s retrying.. %d/%d\n"
			"Port status is 0x%x\n",
			enable ? "enable" : "disable", i,
			GL3590_EN_PORT_MAX_RETRY_COUNT, tmp);
	}

	return EC_SUCCESS;
}

#ifdef CONFIG_CMD_GL3590
static int command_gl3590(int argc, const char **argv)
{
	char *e;
	int port;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	if (strcasecmp(argv[1], "enable") == 0) {
		if (!gl3590_enable_ports(0, port, 1))
			return EC_SUCCESS;
		else
			return EC_ERROR_HW_INTERNAL;
	} else if (strcasecmp(argv[1], "disable") == 0) {
		if (!gl3590_enable_ports(0, port, 0))
			return EC_SUCCESS;
		else
			return EC_ERROR_HW_INTERNAL;
	}

	return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND(gl3590, command_gl3590,
			"<enable | disable> <port_bitmask>",
			"Manage GL3590 USB3.1 hub and its ports");
#endif /* CONFIG_CMD_GL3590 */
