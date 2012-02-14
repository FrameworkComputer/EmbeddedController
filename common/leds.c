/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * LED controls.
 */

#include "board.h"
#include "console.h"
#include "i2c.h"
#include "uart.h"
#include "util.h"

static int command_lightsaber(int argc, char **argv)
{
	static int addr = 0x54;		/* FIXME: 54 and 56 are valid */
	int port, reg;
	char *e;
	int rv;
	int d, i;
        int reglist[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
			  0x08, 0x09, 0x0a,                         0x0f,
			  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
			  0x18, 0x19, 0x1a };

	port = I2C_PORT_LIGHTBAR;

	if (1 == argc) {		/* dump 'em all */
		uart_printf("addr %02x:\n", addr);
		for( i=0; i<sizeof(reglist)/sizeof(reglist[0]); i++ ) {
			reg = reglist[i];
			rv = i2c_read8(port, addr, reg, &d);
			if (rv)
				return rv;
			uart_printf("reg %02x = %02x\n", reg, d);
		}
		return EC_SUCCESS;
	} else if (2 == argc ) {	/* read just one */
		reg = strtoi(argv[1], &e, 16);
		if (*e) {
			uart_puts("Invalid reg\n");
			return EC_ERROR_INVAL;
		}
		rv = i2c_read8(port, addr, reg, &d);
		if (rv)
			return rv;
		uart_printf("0x%02x\n", d);
		return EC_SUCCESS;
	} else if (3 == argc) {		/* write one */
		if (!strcasecmp(argv[1],"addr")) {
			addr = strtoi(argv[2], &e, 16);
			uart_printf("addr now %02x\n", addr);
			return EC_SUCCESS;
		}
		reg = strtoi(argv[1], &e, 16);
		if (*e) {
			uart_puts("Invalid reg\n");
			return EC_ERROR_INVAL;
		}
		d = strtoi(argv[2], &e, 16);
		if (*e) {
			uart_puts("Invalid data\n");
			return EC_ERROR_INVAL;
		}
		return i2c_write8(port, addr, reg, d);
	}

	uart_printf("Usage:  %s [<reg> [<val]]\n", argv[0]);
	uart_printf("        %s addr <ADDR>\n", argv[0]);
	return EC_ERROR_INVAL;
}
DECLARE_CONSOLE_COMMAND(lightsaber, command_lightsaber);
