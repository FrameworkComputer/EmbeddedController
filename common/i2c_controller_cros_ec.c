/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/assert.h"
#include "console.h"
#include "gpio.h"
#include "i2c.h"
#include "i2c_bitbang.h"
#include "i2c_private.h"
#include "printf.h"
#include "system.h"
#include "util.h"
#include "watchdog.h"

/* This source file contains I2C controller code that is used only in legacy
 * (CrOS EC) builds.
 */

#ifdef CONFIG_ZEPHYR
#error "Don't use this source in Zephyr builds"
#endif

/* Delay for bitbanging i2c corresponds roughly to 100kHz. */
#define I2C_BITBANG_DELAY_US 5

/* Number of attempts to unwedge each pin. */
#define UNWEDGE_SCL_ATTEMPTS 10
#define UNWEDGE_SDA_ATTEMPTS 3

#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ##args)

int get_sda_from_i2c_port(int port, enum gpio_signal *sda)
{
	const struct i2c_port_t *i2c_port = get_i2c_port(port);

	/* Crash if the port given is not in the i2c_ports[] table. */
	ASSERT(i2c_port);

	/* Check if the SCL and SDA pins have been defined for this port. */
	if (i2c_port->scl == 0 && i2c_port->sda == 0)
		return EC_ERROR_INVAL;

	*sda = i2c_port->sda;
	return EC_SUCCESS;
}

int get_scl_from_i2c_port(int port, enum gpio_signal *scl)
{
	const struct i2c_port_t *i2c_port = get_i2c_port(port);

	/* Crash if the port given is not in the i2c_ports[] table. */
	ASSERT(i2c_port);

	/* Check if the SCL and SDA pins have been defined for this port. */
	if (i2c_port->scl == 0 && i2c_port->sda == 0)
		return EC_ERROR_INVAL;

	*scl = i2c_port->scl;
	return EC_SUCCESS;
}

void i2c_raw_set_scl(int port, int level)
{
	enum gpio_signal g;

	if (get_scl_from_i2c_port(port, &g) == EC_SUCCESS)
		gpio_set_level(g, level);
}

void i2c_raw_set_sda(int port, int level)
{
	enum gpio_signal g;

	if (get_sda_from_i2c_port(port, &g) == EC_SUCCESS)
		gpio_set_level(g, level);
}

int i2c_raw_mode(int port, int enable)
{
	enum gpio_signal sda, scl;
	int ret_sda, ret_scl;

	/* Get the SDA and SCL pins for this port. If none, then return. */
	if (get_sda_from_i2c_port(port, &sda) != EC_SUCCESS)
		return EC_ERROR_INVAL;
	if (get_scl_from_i2c_port(port, &scl) != EC_SUCCESS)
		return EC_ERROR_INVAL;

	if (enable) {
		int raw_gpio_mode_flags = GPIO_ODR_HIGH;

		/* If the CLK line is 1.8V, then ensure we set 1.8V mode */
		if ((gpio_list + scl)->flags & GPIO_SEL_1P8V)
			raw_gpio_mode_flags |= GPIO_SEL_1P8V;

		/*
		 * To enable raw mode, take out of alternate function mode and
		 * set the flags to open drain output.
		 */
		ret_sda = gpio_config_pin(MODULE_I2C, sda, 0);
		ret_scl = gpio_config_pin(MODULE_I2C, scl, 0);

		gpio_set_flags(scl, raw_gpio_mode_flags);
		gpio_set_flags(sda, raw_gpio_mode_flags);
	} else {
		/*
		 * Configure the I2C pins to exit raw mode and return
		 * to normal mode.
		 */
		ret_sda = gpio_config_pin(MODULE_I2C, sda, 1);
		ret_scl = gpio_config_pin(MODULE_I2C, scl, 1);
	}

	return ret_sda == EC_SUCCESS ? ret_scl : ret_sda;
}

/*
 * Unwedge the i2c bus for the given port.
 *
 * Some devices on our i2c busses keep power even if we get a reset.  That
 * means that they could be part way through a transaction and could be
 * driving the bus in a way that makes it hard for us to talk on the bus.
 * ...or they might listen to the next transaction and interpret it in a
 * weird way.
 *
 * Note that devices could be in one of several states:
 * - If a device got interrupted in a write transaction it will be watching
 *   for additional data to finish its write.  It will probably be looking to
 *   ack the data (drive the data line low) after it gets everything.
 * - If a device got interrupted while responding to a register read, it will
 *   be watching for clocks and will drive data out when it sees clocks.  At
 *   the moment it might be trying to send out a 1 (so both clock and data
 *   may be high) or it might be trying to send out a 0 (so it's driving data
 *   low).
 *
 * We attempt to unwedge the bus by doing:
 * - If SCL is being held low, then a peripheral is clock extending. The only
 *   thing we can do is try to wait until the peripheral stops clock extending.
 * - Otherwise, we will toggle the clock until the peripheral releases the SDA
 *   line. Once the SDA line is released, try to send a STOP bit. Rinse and
 *   repeat until either the bus is normal, or we run out of attempts.
 *
 * Note this should work for most devices, but depending on the peripheral's
 * i2c state machine, it may not be possible to unwedge the bus.
 */
int i2c_unwedge(int port)
{
	int i, j;
	int ret = EC_SUCCESS;

#ifdef CONFIG_I2C_BUS_MAY_BE_UNPOWERED
	/*
	 * Don't try to unwedge the port if we know it's unpowered; it's futile.
	 */
	if (!board_is_i2c_port_powered(port)) {
		CPRINTS("Skipping i2c unwedge, bus not powered.");
		return EC_ERROR_NOT_POWERED;
	}
#endif /* CONFIG_I2C_BUS_MAY_BE_UNPOWERED */

	/* Try to put port in to raw bit bang mode. */
	if (i2c_raw_mode(port, 1) != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	/*
	 * If clock is low, wait for a while in case of clock stretched
	 * by a peripheral.
	 */
	if (!i2c_raw_get_scl(port)) {
		for (i = 0;; i++) {
			if (i >= UNWEDGE_SCL_ATTEMPTS) {
				/*
				 * If we get here, a peripheral is holding the
				 * clock low and there is nothing we can do.
				 */
				CPRINTS("I2C%d unwedge failed, "
					"SCL is held low",
					port);
				ret = EC_ERROR_UNKNOWN;
				goto unwedge_done;
			}
			udelay(I2C_BITBANG_DELAY_US);
			if (i2c_raw_get_scl(port))
				break;
		}
	}

	if (i2c_raw_get_sda(port))
		goto unwedge_done;

	CPRINTS("I2C%d unwedge called with SDA held low", port);

	/* Keep trying to unwedge the SDA line until we run out of attempts. */
	for (i = 0; i < UNWEDGE_SDA_ATTEMPTS; i++) {
		/* Drive the clock high. */
		i2c_raw_set_scl(port, 1);
		udelay(I2C_BITBANG_DELAY_US);

		/*
		 * Clock through the problem by clocking out 9 bits. If
		 * peripheral releases the SDA line, then we can stop clocking
		 * bits and send a STOP.
		 */
		for (j = 0; j < 9; j++) {
			if (i2c_raw_get_sda(port))
				break;

			i2c_raw_set_scl(port, 0);
			udelay(I2C_BITBANG_DELAY_US);
			i2c_raw_set_scl(port, 1);
			udelay(I2C_BITBANG_DELAY_US);
		}

		/* Take control of SDA line and issue a STOP command. */
		i2c_raw_set_sda(port, 0);
		udelay(I2C_BITBANG_DELAY_US);
		i2c_raw_set_sda(port, 1);
		udelay(I2C_BITBANG_DELAY_US);

		/* Check if the bus is unwedged. */
		if (i2c_raw_get_sda(port) && i2c_raw_get_scl(port))
			break;
	}

	if (!i2c_raw_get_sda(port)) {
		CPRINTS("I2C%d unwedge failed, SDA still low", port);
		ret = EC_ERROR_UNKNOWN;
	}
	if (!i2c_raw_get_scl(port)) {
		CPRINTS("I2C%d unwedge failed, SCL still low", port);
		ret = EC_ERROR_UNKNOWN;
	}

unwedge_done:
	/* Take port out of raw bit bang mode. */
	i2c_raw_mode(port, 0);

	return ret;
}

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

		if (IS_ENABLED(CONFIG_I2C_BITBANG_CROS_EC))
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
