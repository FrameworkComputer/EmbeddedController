/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * LED controls.
 */

#include "board.h"
#include "console.h"
#include "i2c.h"
#include "timer.h"
#include "uart.h"
#include "util.h"


#define DRIVER_SMART 0x54
#define DRIVER_FUN   0x56

/* We need to limit the total current per ISC to no more than 20mA (5mA per
 * color LED, but we have four LEDs in parallel on each ISC). Any more than
 * that runs the risk of damaging the LED component. A value of 0x67 is as high
 * as we want (assuming Square Law), but the blue LED is the least bright, so
 * I've lowered the other colors until they all appear approximately equal
 * brightness when full on. That's still pretty bright and a lot of current
 * drain on the battery, so we'll probably rarely go that high. */
#define MAX_RED   0x5c
#define MAX_GREEN 0x38
#define MAX_BLUE  0x67
/* Macro to scale 0-255 into max value */
#define SCALE(VAL, MAX) ((VAL * MAX)/255 + MAX/256)

/* How we'd like to see the driver chips initialized. */
static const struct {
	uint8_t reg;
	uint8_t val;
} ready_vals[] = {
	{0x01, 0x00},				/* standby mode */
	{0x04, 0x00},				/* no backlight */
	{0x05, 0x3f},				/* xRGBRGB per chip */
	{0x0f, 0x01},				/* square acts more linear */
	{0x10, 0x3f},				/* enable independent LEDs */
	{0x11, 0x00},				/* no auto cycling */
	{0x12, 0x00},				/* no auto cycling */
	{0x13, 0x00},				/* instant fade in/out */
	{0x14, 0x00},				/* not using LED 7 */
	{0x15, 0x00},				/* current for LED 6 (blue) */
	{0x16, 0x00},				/* current for LED 5 (red) */
	{0x17, 0x00},				/* current for LED 4 (green) */
	{0x18, 0x00},				/* current for LED 3 (blue) */
	{0x19, 0x00},				/* current for LED 2 (red) */
	{0x1a, 0x00},				/* current for LED 1 (green) */
	{0x01, 0x20},				/* active (no charge pump) */
};

/* NOTE: Since there's absolutely nothing we can do about it if an I2C access
 * isn't working, we're completely ignoring those results. */

/****************************************************************************/
/* External interface functions. */

/* Shut down the driver chips, so everything is off. */
void lightbar_off(void)
{
	/* FIXME: nRST is attached to a GPIO on link, so just assert it. For
	 * now we'll just put the drivers into standby, but won't reset. */
	i2c_write8(I2C_PORT_LIGHTBAR, DRIVER_FUN, 0x01, 0x00);
	i2c_write8(I2C_PORT_LIGHTBAR, DRIVER_SMART, 0x01, 0x00);
}

/* Initialize & activate driver chips, but no LEDs on yet. */
void lightbar_on(void)
{
	int i;
	/* FIXME: If nRST is asserted, deassert it. */
	for (i = 0; i < ARRAY_SIZE(ready_vals); i++) {
		i2c_write8(I2C_PORT_LIGHTBAR, DRIVER_FUN, ready_vals[i].reg,
			   ready_vals[i].val);
		i2c_write8(I2C_PORT_LIGHTBAR, DRIVER_SMART, ready_vals[i].reg,
			   ready_vals[i].val);
	}
}

/* Set the color for an LED (0-3). RGB values should be in 0-255. They'll be
 * scaled so that maxium brightness is at 255. */
void lightbar_setcolor(int led, int red, int green, int blue)
{
	int driver, bank;
	driver = DRIVER_SMART + (led/2) * 2;
	bank = (led % 2) * 3 + 0x15;
	i2c_write8(I2C_PORT_LIGHTBAR, driver, bank, SCALE(blue, MAX_BLUE));
	i2c_write8(I2C_PORT_LIGHTBAR, driver, bank+1, SCALE(red, MAX_RED));
	i2c_write8(I2C_PORT_LIGHTBAR, driver, bank+2, SCALE(green, MAX_GREEN));
}

/* FIXME: Do I want some auto-cycling functions? Pulsing, etc.? Investigate to
 * see what's possible and looks nice. */

/****************************************************************************/
/* Console commands */

static int help(char *prog)
{
	uart_printf("Usage:  %s\n", prog);
	uart_printf("        %s off\n", prog);
	uart_printf("        %s on\n", prog);
	uart_printf("        %s LED RED GREEN BLUE\n", prog);
	uart_printf("        %s test [MAX]\n", prog);
	uart_printf("        %s smart REG VAL\n", prog);
	uart_printf("        %s fun REG VAL\n", prog);
	return EC_ERROR_UNKNOWN;
}

static void dump_em(void)
{
	int d1, d2, i;
	int reglist[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
			  0x08, 0x09, 0x0a,                         0x0f,
			  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
			  0x18, 0x19, 0x1a };
	uart_printf("reg    smart  fun\n");
	for (i = 0; i < ARRAY_SIZE(reglist); i++) {
		i2c_read8(I2C_PORT_LIGHTBAR, DRIVER_SMART, reglist[i], &d1);
		i2c_read8(I2C_PORT_LIGHTBAR, DRIVER_FUN, reglist[i], &d2);
		uart_printf(" %02x     %02x     %02x\n", reglist[i], d1, d2);
	}
}

static int set(int driver, char *regstr, char *valstr)
{
	int reg, val;
	char *e;
	reg = strtoi(regstr, &e, 16);
	val = strtoi(valstr, &e, 16);
	i2c_write8(I2C_PORT_LIGHTBAR, driver, reg, val);
	return EC_SUCCESS;
}

static const struct {
	uint8_t r, g, b;
} testy[] = {
	{0xff, 0x00, 0x00},
	{0x00, 0xff, 0x00},
	{0x00, 0x00, 0xff},
	{0xff, 0xff, 0x00},
	{0x00, 0xff, 0xff},
	{0xff, 0x00, 0xff},
	{0xff, 0xff, 0xff},
};


static int command_lightbar(int argc, char **argv)
{
	int led, red, green, blue;
	char *e;


	if (1 == argc) {		/* no args = dump 'em all */
		dump_em();
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "off")) {
		lightbar_off();
		dump_em();
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "on")) {
		lightbar_on();
		dump_em();
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "smart")) {
		if (4 != argc)
			return help(argv[0]);
		return set(DRIVER_SMART, argv[2], argv[3]);
	}

	if (!strcasecmp(argv[1], "fun")) {
		if (4 != argc)
			return help(argv[0]);
		return set(DRIVER_FUN, argv[2], argv[3]);
	}

	if (!strcasecmp(argv[1], "test")) {
		int i, j, k, r, g, b;
		int kmax = 254;
		if (3 == argc)
			kmax = strtoi(argv[2], &e, 16);
		kmax &= 0xfe;
		lightbar_on();
		for (i = 0; i < ARRAY_SIZE(testy); i++) {
			for (k = 0; k <= kmax; k += 2) {
				for (j = 0; j < 4; j++) {
					r = testy[i].r ? k : 0;
					g = testy[i].g ? k : 0;
					b = testy[i].b ? k : 0;
					lightbar_setcolor(j, r, g, b);
				}
				usleep(1000);
			}
			for (k = kmax; k; k -= 2) {
				for (j = 0; j < 4; j++) {
					r = testy[i].r ? k : 0;
					g = testy[i].g ? k : 0;
					b = testy[i].b ? k : 0;
					lightbar_setcolor(j, r, g, b);
				}
				usleep(1000);
			}
		}
		lightbar_on();
		return EC_SUCCESS;
	}

	if (5 != argc)
		return help(argv[0]);

	/* must be LED RED GREEN BLUE */
	led = strtoi(argv[1], &e, 16) & 0x3;
	red = strtoi(argv[2], &e, 16) & 0xff;
	green = strtoi(argv[3], &e, 16) & 0xff;
	blue = strtoi(argv[4], &e, 16) & 0xff;
	lightbar_setcolor(led, red, green, blue);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(lightbar, command_lightbar);
