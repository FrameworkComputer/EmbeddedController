/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Lightbar IC interface
 */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "i2c.h"
#include "lb_common.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LIGHTBAR, outstr)
#define CPRINTS(format, args...) cprints(CC_LIGHTBAR, format, ## args)

/******************************************************************************/
/* How to talk to the controller */
/******************************************************************************/

/* Since there's absolutely nothing we can do about it if an I2C access
 * isn't working, we're completely ignoring any failures. */

static const uint8_t i2c_addr[] = { 0x54, 0x56 };

static inline void controller_write(int ctrl_num, uint8_t reg, uint8_t val)
{
	ctrl_num = ctrl_num % ARRAY_SIZE(i2c_addr);
	i2c_write8(I2C_PORT_LIGHTBAR, i2c_addr[ctrl_num], reg, val);
}

static inline uint8_t controller_read(int ctrl_num, uint8_t reg)
{
	int val = 0;
	ctrl_num = ctrl_num % ARRAY_SIZE(i2c_addr);
	i2c_read8(I2C_PORT_LIGHTBAR, i2c_addr[ctrl_num], reg, &val);
	return val;
}

/******************************************************************************/
/* Controller details. We have an ADP8861 and and ADP8863, but we can treat
 * them identically for our purposes */
/******************************************************************************/

#ifdef BOARD_BDS
/* We need to limit the total current per ISC to no more than 20mA (5mA per
 * color LED, but we have four LEDs in parallel on each ISC). Any more than
 * that runs the risk of damaging the LED component. A value of 0x67 is as high
 * as we want (assuming Square Law), but the blue LED is the least bright, so
 * I've lowered the other colors until they all appear approximately equal
 * brightness when full on. That's still pretty bright and a lot of current
 * drain on the battery, so we'll probably rarely go that high. */
#define MAX_RED   0x5c
#define MAX_GREEN 0x30
#define MAX_BLUE  0x67
#endif
#ifdef BOARD_LINK
/* Link uses seven segments, not four, but keep the same limits anyway */
#define MAX_RED   0x5c
#define MAX_GREEN 0x30
#define MAX_BLUE  0x67
#endif
#ifdef BOARD_SAMUS
/* Samus uses completely different LEDs, so the numbers are different */
#define MAX_RED   0x4f
#define MAX_GREEN 0x55
#define MAX_BLUE  0x67
#endif
#ifdef BOARD_HOST
/* For testing only */
#define MAX_RED   0xff
#define MAX_GREEN 0xff
#define MAX_BLUE  0xff
#endif

/* How we'd like to see the driver chips initialized. The controllers have some
 * auto-cycling capability, but it's not much use for our purposes. For now,
 * we'll just control all color changes actively. */
struct initdata_s {
	uint8_t reg;
	uint8_t val;
};

static const struct initdata_s init_vals[] = {
	{0x04, 0x00},				/* no backlight function */
	{0x05, 0x3f},				/* xRGBRGB per chip */
	{0x0f, 0x01},				/* square law looks better */
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
};

static void set_from_array(const struct initdata_s *data, int count)
{
	int i;
	for (i = 0; i < count; i++) {
		controller_write(0, data[i].reg, data[i].val);
		controller_write(1, data[i].reg, data[i].val);
	}
}

/* Controller register lookup tables. */
static const uint8_t led_to_ctrl[] = { 1, 1, 0, 0 };
#ifdef BOARD_BDS
static const uint8_t led_to_isc[] = { 0x18, 0x15, 0x18, 0x15 };
#endif
#ifdef BOARD_LINK
static const uint8_t led_to_isc[] = { 0x18, 0x15, 0x18, 0x15 };
#endif
#ifdef BOARD_SAMUS
static const uint8_t led_to_isc[] = { 0x15, 0x18, 0x15, 0x18 };
#endif
#ifdef BOARD_HOST
/* For testing only */
static const uint8_t led_to_isc[] = { 0x15, 0x18, 0x15, 0x18 };
#endif

/* Scale 0-255 into max value */
static inline uint8_t scale_abs(int val, int max)
{
	return (val * max)/255;
}

/* This is the overall brightness control. */
static int brightness = 0xc0;

/* So that we can make brightness changes happen instantly, we need to track
 * the current values. The values in the controllers aren't very helpful. */
static uint8_t current[NUM_LEDS][3];

/* Scale 0-255 by brightness */
static inline uint8_t scale(int val, int max)
{
	return scale_abs((val * brightness)/255, max);
}

/* Helper function to set one LED color and remember it for later */
static void setrgb(int led, int red, int green, int blue)
{
	int ctrl, bank;
	current[led][0] = red;
	current[led][1] = green;
	current[led][2] = blue;
	ctrl = led_to_ctrl[led];
	bank = led_to_isc[led];
	controller_write(ctrl, bank, scale(blue, MAX_BLUE));
	controller_write(ctrl, bank+1, scale(red, MAX_RED));
	controller_write(ctrl, bank+2, scale(green, MAX_GREEN));
}

/* LEDs are numbered 0-3, RGB values should be in 0-255.
 * If you specify too large an LED, it sets them all. */
void lb_set_rgb(unsigned int led, int red, int green, int blue)
{
	int i;
	if (led >= NUM_LEDS)
		for (i = 0; i < NUM_LEDS; i++)
			setrgb(i, red, green, blue);
	else
		setrgb(led, red, green, blue);
}

/* Get current LED values, if the LED number is in range. */
int lb_get_rgb(unsigned int led, uint8_t *red, uint8_t *green, uint8_t *blue)
{
	if (led < 0 || led >= NUM_LEDS)
		return EC_RES_INVALID_PARAM;

	*red = current[led][0];
	*green = current[led][1];
	*blue = current[led][2];

	return EC_RES_SUCCESS;
}

/* Change current display brightness (0-255) */
void lb_set_brightness(unsigned int newval)
{
	int i;
	CPRINTS("LB_bright 0x%02x", newval);
	brightness = newval;
	for (i = 0; i < NUM_LEDS; i++)
		setrgb(i, current[i][0], current[i][1], current[i][2]);
}

/* Get current display brightness (0-255) */
uint8_t lb_get_brightness(void)
{
	return brightness;
}

/* Initialize the controller ICs after reset */
void lb_init(void)
{
	CPRINTS("LB_init_vals");
	set_from_array(init_vals, ARRAY_SIZE(init_vals));
	memset(current, 0, sizeof(current));
}

/* Just go into standby mode. No register values should change. */
void lb_off(void)
{
	CPRINTS("LB_off");
	controller_write(0, 0x01, 0x00);
	controller_write(1, 0x01, 0x00);
}

/* Come out of standby mode. */
void lb_on(void)
{
	CPRINTS("LB_on");
	controller_write(0, 0x01, 0x20);
	controller_write(1, 0x01, 0x20);
}

static const uint8_t dump_reglist[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a,			  0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a
};

/* Helper for host command to dump controller registers */
void lb_hc_cmd_dump(struct ec_response_lightbar *out)
{
	int i;
	uint8_t reg;

	BUILD_ASSERT(ARRAY_SIZE(dump_reglist) ==
		     ARRAY_SIZE(out->dump.vals));

	for (i = 0; i < ARRAY_SIZE(dump_reglist); i++) {
		reg = dump_reglist[i];
		out->dump.vals[i].reg = reg;
		out->dump.vals[i].ic0 = controller_read(0, reg);
		out->dump.vals[i].ic1 = controller_read(1, reg);
	}
}

/* Helper for host command to write controller registers directly */
void lb_hc_cmd_reg(const struct ec_params_lightbar *in)
{
	controller_write(in->reg.ctrl, in->reg.reg, in->reg.value);
}
