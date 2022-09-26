/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "i2c_bitbang.h"
#include "i2c_private.h"
#include "i2c.h"
#include "console.h"
#include "watchdog.h"
#include "printf.h"
#include "util.h"

/* This source file contains I2C controller code that is used only in legacy
 * (CrOS EC) builds.
 */

#ifdef CONFIG_CMD_I2C_SCAN
static void scan_bus(int port, const char *desc)
{
	int level;
	uint8_t tmp;
	uint16_t addr_flags;

	ccprintf("Scanning %d %s", port, desc);

	i2c_lock(port, 1);

	/* Don't scan a busy port, since reads will just fail / time out */
	level = i2c_get_line_levels(port);
	if (level != I2C_LINE_IDLE) {
		ccprintf(": port busy (SDA=%d, SCL=%d)",
			 (level & I2C_LINE_SDA_HIGH) ? 1 : 0,
			 (level & I2C_LINE_SCL_HIGH) ? 1 : 0);
		goto scan_bus_exit;
	}
	/*
	 * Only scan in the valid client device address range, otherwise some
	 * client devices stretch the clock in weird ways that prevent the
	 * discovery of other devices.
	 */
	for (addr_flags = I2C_FIRST_VALID_ADDR;
	     addr_flags <= I2C_LAST_VALID_ADDR; ++addr_flags) {
		watchdog_reload(); /* Otherwise a full scan trips watchdog */
		ccputs(".");

		/* Do a single read */
		if (!i2c_xfer_unlocked(port, addr_flags, NULL, 0, &tmp, 1,
				       I2C_XFER_SINGLE))
			ccprintf("\n  0x%02x", addr_flags);
	}

scan_bus_exit:
	i2c_lock(port, 0);
	ccputs("\n");
}

static int command_scan(int argc, const char **argv)
{
	int port;
	char *e;
	const struct i2c_port_t *i2c_port;

	if (argc == 1) {
		for (port = 0; port < i2c_ports_used; port++)
			scan_bus(i2c_ports[port].port, i2c_ports[port].name);

		if (IS_ENABLED(CONFIG_I2C_BITBANG))
			for (port = 0; port < i2c_bitbang_ports_used; port++)
				scan_bus(i2c_bitbang_ports[port].port,
					 i2c_bitbang_ports[port].name);

		return EC_SUCCESS;
	}

	port = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	i2c_port = get_i2c_port(port);
	if (!i2c_port)
		return EC_ERROR_PARAM2;

	scan_bus(port, i2c_port->name);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2cscan, command_scan, "i2cscan [port]",
			"Scan I2C ports for devices");
#endif

#ifdef CONFIG_CMD_I2C_XFER
static int command_i2cxfer(int argc, const char **argv)
{
	int port;
	uint16_t addr_flags;
	uint16_t offset = 0;
	uint8_t offset_size = 0;
	int v = 0;
	uint8_t data[32];
	char *e;
	int rv = 0;

	if (argc < 5)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	addr_flags = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;

	offset = strtoi(argv[4], &e, 0);
	if (*e)
		return EC_ERROR_PARAM4;

	offset_size = (strlen(argv[4]) == 6) ? 2 : 1;

	if (argc >= 6) {
		v = strtoi(argv[5], &e, 0);
		if (*e)
			return EC_ERROR_PARAM5;
	}

	if (strcasecmp(argv[1], "r") == 0) {
		/* 8-bit read */
		if (offset_size == 2)
			rv = i2c_read_offset16(port, addr_flags, offset, &v, 1);
		else
			rv = i2c_read8(port, addr_flags, offset, &v);
		if (!rv)
			ccprintf("0x%02x [%d]\n", v, v);

	} else if (strcasecmp(argv[1], "r16") == 0) {
		/* 16-bit read */
		if (offset_size == 2)
			rv = i2c_read_offset16(port, addr_flags, offset, &v, 2);
		else
			rv = i2c_read16(port, addr_flags, offset, &v);
		if (!rv)
			ccprintf("0x%04x [%d]\n", v, v);

	} else if (strcasecmp(argv[1], "rlen") == 0) {
		/* Arbitrary length read; param5 = len */
		if (argc < 6 || v < 0 || v > sizeof(data))
			return EC_ERROR_PARAM5;

		rv = i2c_xfer(port, addr_flags, (uint8_t *)&offset, 1, data, v);

		if (!rv) {
			char str_buf[hex_str_buf_size(v)];

			snprintf_hex_buffer(str_buf, sizeof(str_buf),
					    HEX_BUF(data, v));
			ccprintf("Data: %s\n", str_buf);
		}

	} else if (strcasecmp(argv[1], "w") == 0) {
		/* 8-bit write */
		if (argc < 6)
			return EC_ERROR_PARAM5;
		if (offset_size == 2)
			rv = i2c_write_offset16(port, addr_flags, offset, v, 1);
		else
			rv = i2c_write8(port, addr_flags, offset, v);

	} else if (strcasecmp(argv[1], "w16") == 0) {
		/* 16-bit write */
		if (argc < 6)
			return EC_ERROR_PARAM5;
		if (offset_size == 2)
			rv = i2c_write_offset16(port, addr_flags, offset, v, 2);
		else
			rv = i2c_write16(port, addr_flags, offset, v);
#ifdef CONFIG_CMD_I2C_XFER_RAW
	} else if (strcasecmp(argv[1], "raw") == 0) {
		/* <port> <addr_flags> <read_count> [write_bytes..] */
		int i;
		int write_count = 0, read_count = 0;
		int xferflags = I2C_XFER_START;

		read_count = offset;
		if (read_count < 0 || read_count > sizeof(data))
			return EC_ERROR_PARAM5;

		if (argc >= 6) {
			/* Parse bytes to write */
			argc -= 5;
			argv += 5;
			write_count = argc;
			if (write_count > sizeof(data)) {
				ccprintf("Too many bytes to write\n");
				return EC_ERROR_PARAM_COUNT;
			}

			for (i = 0; i < write_count; i++) {
				data[i] = strtoi(argv[i], &e, 0);
				if (*e) {
					ccprintf("Bad write byte %d\n", i);
					return EC_ERROR_INVAL;
				}
			}
		}

		if (write_count) {
			if (read_count == 0)
				xferflags |= I2C_XFER_STOP;
			ccprintf("Writing %d bytes\n", write_count);
			i2c_lock(port, 1);
			rv = i2c_xfer_unlocked(port, addr_flags, data,
					       write_count, NULL, 0, xferflags);
			if (rv || read_count == 0) {
				i2c_lock(port, 0);
				return rv;
			}
		}
		if (read_count) {
			ccprintf("Reading %d bytes\n", read_count);
			if (write_count == 0)
				i2c_lock(port, 1);
			rv = i2c_xfer_unlocked(port, addr_flags, NULL, 0, data,
					       read_count,
					       I2C_XFER_START | I2C_XFER_STOP);
			i2c_lock(port, 0);
			if (!rv) {
				char str_buf[hex_str_buf_size(read_count)];

				snprintf_hex_buffer(str_buf, sizeof(str_buf),
						    HEX_BUF(data, read_count));
				ccprintf("Data: %s\n", str_buf);
			}
		}
#endif /* CONFIG_CMD_I2C_XFER_RAW */
	} else {
		return EC_ERROR_PARAM1;
	}

	return rv;
}
DECLARE_CONSOLE_COMMAND(i2cxfer, command_i2cxfer,
			"r/r16/rlen/w/w16 port addr offset [value | len]"
#ifdef CONFIG_CMD_I2C_XFER_RAW
			"\nraw port addr read_count [bytes_to_write..]"
#endif /* CONFIG_CMD_I2C_XFER_RAW */
			,
			"Read write I2C");
#endif

#ifdef CONFIG_CMD_I2C_SPEED

static const char *const i2c_freq_str[] = {
	[I2C_FREQ_1000KHZ] = "1000 kHz",
	[I2C_FREQ_400KHZ] = "400 kHz",
	[I2C_FREQ_100KHZ] = "100 kHz",
	[I2C_FREQ_COUNT] = "unknown",
};

BUILD_ASSERT(ARRAY_SIZE(i2c_freq_str) == I2C_FREQ_COUNT + 1);

static int command_i2c_speed(int argc, const char **argv)
{
	int port;
	char *e;
	enum i2c_freq freq;
	enum i2c_freq new_freq = I2C_FREQ_COUNT;

	if (argc < 2 || argc > 3)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	if (port < 0 || port >= I2C_PORT_COUNT)
		return EC_ERROR_INVAL;

	freq = i2c_get_freq(port);
	if (freq < 0 || freq > I2C_FREQ_COUNT)
		return EC_ERROR_UNKNOWN;

	if (argc == 3) {
		int khz;
		int rv;

		khz = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		switch (khz) {
		case 100:
			new_freq = I2C_FREQ_100KHZ;
			break;
		case 400:
			new_freq = I2C_FREQ_400KHZ;
			break;
		case 1000:
			new_freq = I2C_FREQ_1000KHZ;
			break;
		default:
			return EC_ERROR_PARAM2;
		}
		rv = i2c_set_freq(port, new_freq);
		if (rv != EC_SUCCESS)
			return rv;
	}

	if (new_freq != I2C_FREQ_COUNT)
		ccprintf("Port %d speed changed from %s to %s\n", port,
			 i2c_freq_str[freq], i2c_freq_str[new_freq]);
	else
		ccprintf("Port %d speed is %s\n", port, i2c_freq_str[freq]);

	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(i2cspeed, command_i2c_speed, "port [speed in kHz]",
			"Get or set I2C port speed");

#endif /* CONFIG_CMD_I2C_SPEED */
