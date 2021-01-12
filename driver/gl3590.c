/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "i2c.h"
#include "system.h"
#include "util.h"
#include "pwr_defs.h"

#include "gl3590.h"

#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)

/* GL3590 is unique in terms of i2c_read, since it doesn't support repeated
 * start sequence. One need to issue two separate transactions - first is write
 * with a register offset, then after a delay second transaction is actual read.
 */
int gl3590_read(int hub, uint8_t reg, uint8_t *data, int count)
{
	int rv;
	struct uhub_i2c_iface_t *uhub_p = &uhub_config[hub];

	i2c_lock(uhub_p->i2c_host_port, 1);
	rv = i2c_xfer_unlocked(uhub_p->i2c_host_port,
			       uhub_p->i2c_addr,
			       &reg, 1,
			       NULL, 0,
			       I2C_XFER_SINGLE);
	i2c_lock(uhub_p->i2c_host_port, 0);

	if (rv)
		return rv;

	/* GL3590 requires at least 300us before data is ready */
	udelay(400);

	i2c_lock(uhub_p->i2c_host_port, 1);
	rv = i2c_xfer_unlocked(uhub_p->i2c_host_port,
			       uhub_p->i2c_addr,
			       NULL, 0,
			       data, count,
			       I2C_XFER_SINGLE);
	i2c_lock(uhub_p->i2c_host_port, 0);

	return rv;
};

int gl3590_write(int hub, uint8_t reg, uint8_t *data, int count)
{
	int rv;
	uint8_t buf[5];
	struct uhub_i2c_iface_t *uhub_p = &uhub_config[hub];

	/* GL3590 registers accept 4 bytes at max */
	if (count > (sizeof(buf) - 1)) {
		ccprintf("Too many bytes to write");
		return EC_ERROR_INVAL;
	}

	buf[0] = reg;
	memcpy(&buf[1], data, count);

	i2c_lock(uhub_p->i2c_host_port, 1);
	rv = i2c_xfer_unlocked(uhub_p->i2c_host_port,
			       uhub_p->i2c_addr,
			       buf, count + 1,
			       NULL, 0,
			       I2C_XFER_SINGLE);
	i2c_lock(uhub_p->i2c_host_port, 0);

	return rv;
}

void gl3590_irq_handler(int hub)
{
	uint8_t buf = 0;
	uint8_t res_reg[2];

	/* Verify that irq is pending */
	if (gl3590_read(hub, GL3590_INT_REG, &buf, sizeof(buf))) {
		ccprintf("Cannot read from the host hub i2c\n");
		goto exit;
	}

	if ((buf & GL3590_INT_PENDING) == 0) {
		ccprintf("Invalid hub event\n");
		goto exit;
	}

	/* Get the hub event reason */
	if (gl3590_read(hub, GL3590_RESPONSE_REG, res_reg, sizeof(res_reg))) {
		ccprintf("Cannot read from the host hub i2c\n");
		goto exit;
	}

	if ((res_reg[0] & GL3590_RESPONSE_REG_SYNC_MASK) == 0)
		ccprintf("Host hub response: ");
	else
		ccprintf("Host hub event! ");

	switch(res_reg[0]) {
	case 0x0:
		ccprintf("No response");
		break;
	case 0x1:
		ccprintf("Successful");
		break;
	case 0x2:
		ccprintf("Invalid command");
		break;
	case 0x3:
		ccprintf("Invalid arguments");
		break;
	case 0x4:
		ccprintf("Invalid port: %d", res_reg[1]);
		break;
	case 0x5:
		ccprintf("Command not completed");
		break;
	case 0x80:
		ccprintf("Reset complete");
		break;
	case 0x81:
		ccprintf("Power operation mode change");
		break;
	case 0x82:
		ccprintf("Connect change");
		break;
	case 0x83:
		ccprintf("Error on the specific port");
		break;
	case 0x84:
		ccprintf("Hub state change");
		break;
	case 0x85:
		ccprintf("SetFeature PORT_POWER failure");
		break;
	default:
		ccprintf("Unknown value: 0x%0x", res_reg[0]);
	}
	ccprintf("\n");

	if (res_reg[1])
		ccprintf("Affected port %d\n", res_reg[1]);

exit:
	/* Try to clear interrupt */
	buf = GL3590_INT_CLEAR;
	gl3590_write(hub, GL3590_INT_REG, &buf, sizeof(buf));
}

enum ec_error_list gl3590_ufp_pwr(int hub, struct pwr_con_t *pwr)
{
	uint8_t hub_sts, hub_mode;
	int rv = 0;

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

int gl3590_enable_ports(int hub, uint8_t port_mask, bool enable)
{
	uint8_t buf[4] = {0};
	uint8_t en_mask = 0;
	int rv;

	if (!enable)
		en_mask = port_mask;

	buf[0] = en_mask;
	buf[2] = port_mask;

	rv = gl3590_write(hub, GL3590_PORT_DISABLED_REG, buf, sizeof(buf));

	return rv;
}

#ifdef CONFIG_CMD_GL3590
static int command_gl3590(int argc, char **argv)
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
