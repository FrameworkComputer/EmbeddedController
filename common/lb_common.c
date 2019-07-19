/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Lightbar IC interface
 *
 * Here's the API provided by this file.
 *
 * Looking at it from the outside, the lightbar has four "segments", each of
 * which can be independently adjusted to display a unique color such as blue,
 * purple, yellow, pinkish-white, etc. Segment 0 is on the left (looking
 * straight at it from behind).
 *
 * The lb_set_rgb() and lb_get_rgb() functions let you specify the color of a
 * segment using individual Red, Green, and Blue values in the 0x00 to 0xFF
 * range (see https://en.wikipedia.org/wiki/Web_color for background info).
 *
 * The lb_set_brightness() function provides a simple way to set the intensity,
 * over a range of 0x00 (off) to 0xFF (full brightness). It does this by
 * scaling each RGB value proportionally. For example, an RGB value of #FF8000
 * appears orange. To make the segment half as bright, you could specify a RGB
 * value of #7f4000, or you could leave the RGB value unchanged and just set
 * the brightness to 0x80.
 *
 * That covers most of the lb_* functions found in include/lb_common.h, and
 * those functions are what are used to implement the various colors and
 * sequences for displaying power state changes and other events.
 *
 * The internals are a little more messy.
 *
 * Each segment has three individual color emitters - red, green, and blue. A
 * single emitter may consist of 3 to 7 physical LEDs, but they are all wired
 * in parallel so there is only one wire that provides current for any one
 * color emitter. That makes a total of 12 current control wires for the
 * lightbar: four segments, three color emitters per segment.
 *
 * The ICs that we use each have seven independently adjustable
 * current-limiters. We use six of those current limiters (called "Independent
 * Sink Controls", or "ISC"s ) from each of two ICs to control the 12 color
 * emitters in the lightbar. The ICs are not identical, but they're close
 * enough that we can treat them the same. We call the ICs "controller 0" and
 * "controller 1".
 *
 * For no apparent reason, each Chromebook has wired the ICs and the ISCs
 * differently, so there are a couple of lookup tables that ensure that when we
 * call lb_set_rgb() to make segment 1 yellow, it looks the same on all
 * Chromebooks.
 *
 * Each ISC has a control register to set the amount of current that passes
 * through the color emitter control wire. We need to limit the max current so
 * that the current through each of the emitter's LEDs doesn't exceed the
 * manufacturer's specifications. For example, if a particular LED can't handle
 * more than 5 mA, and the emitter is made up of four LEDs in parallel, the
 * maxiumum limit for that particular ISC would be 20 mA.
 *
 * Although the specified maximum currents are usually similar, the three
 * different colors of LEDs have different brightnesses. For any given current,
 * green LEDs are pretty bright, red LEDS are medium, and blue are fairly dim.
 * So we calibrate the max current per ISC differently, depending on which
 * color it controls.
 *
 * First we set one segment to red, one to green, and one to blue, using the
 * ISC register to allow the max current per LED that the LED manufacturer
 * recommends. Then we adjust the current of the brighter segments downward
 * until all three segments appear equally bright to the eye. The MAX_RED,
 * MAX_BLUE, and MAX_GREEN values are the ISC control register values at this
 * point. This means that if we set all ISCs to their MAX_* values, all
 * segments should appear white.
 *
 * To translate the RGB values passed to lb_set_rgb() into ISC values, we
 * perform two transformations. The color value is first scaled according to
 * the current brightness setting, and then that intensity is scaled according
 * to the MAX_* value for the particular color. The result is the ISC register
 * value to use.
 *
 * To add lightbar support for a new Chromebook, you do the following:
 *
 * 1. Figure out the segment-to-IC and color-to-ISC mappings so that
 *    lb_set_rgb() does the same thing as on the other Chromebooks.
 *
 * 2. Calibrate the MAX_RED, MAX_GREEN, and MAX_BLUE values so that white looks
 *    white, and solid red, green, and blue all appear to be the same
 *    brightness.
 *
 * 3. Use lb_set_rgb() to set the colors to what *should be* the Google colors
 *    (at maximum brightness). Tweak the RGB values until the colors match,
 *    then edit common/lightbar.c to set them as the defaults.
 *
 * 4. Curse because the physical variation between the LEDs prevents you from
 *    getting everything exactly right: white looks bluish, yellow turns
 *    orange at lower brightness, segment 3 has a bright spot when displaying
 *    solid red, etc. Go back to step 2, and repeat until deadline.
 */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "i2c.h"
#include "lb_common.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LIGHTBAR, outstr)
#define CPRINTF(format, args...) cprintf(CC_LIGHTBAR, format, ## args)
#define CPRINTS(format, args...) cprints(CC_LIGHTBAR, format, ## args)

/******************************************************************************/
/* How to talk to the controller */
/******************************************************************************/

/* Since there's absolutely nothing we can do about it if an I2C access
 * isn't working, we're completely ignoring any failures. */

static const uint16_t i2c_addr_flags[] = { 0x2A, 0x2B };

static inline void controller_write(int ctrl_num, uint8_t reg, uint8_t val)
{
	uint8_t buf[2];

	buf[0] = reg;
	buf[1] = val;
	ctrl_num = ctrl_num % ARRAY_SIZE(i2c_addr_flags);
	i2c_xfer_unlocked(I2C_PORT_LIGHTBAR, i2c_addr_flags[ctrl_num],
			buf, 2, 0, 0,
			I2C_XFER_SINGLE);
}

static inline uint8_t controller_read(int ctrl_num, uint8_t reg)
{
	uint8_t buf[1];
	int rv;

	ctrl_num = ctrl_num % ARRAY_SIZE(i2c_addr_flags);
	rv = i2c_xfer_unlocked(I2C_PORT_LIGHTBAR, i2c_addr_flags[ctrl_num],
			&reg, 1, buf, 1, I2C_XFER_SINGLE);
	return rv ? 0 : buf[0];
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
#if defined(BOARD_SAMUS)
/* Samus uses completely different LEDs, so the numbers are different. The
 * Samus LEDs can handle much higher currents, but these constants were
 * calibrated to provide uniform intensity at the level used by Link.
 * See crosbug.com/p/33017 before making any changes. */
#define MAX_RED   0x34
#define MAX_GREEN 0x2c
#define MAX_BLUE  0x40
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

/* Controller register lookup tables. */
static const uint8_t led_to_ctrl[] = { 1, 1, 0, 0 };
#ifdef BOARD_BDS
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
	i2c_lock(I2C_PORT_LIGHTBAR, 1);
	controller_write(ctrl, bank, scale(blue, MAX_BLUE));
	controller_write(ctrl, bank+1, scale(red, MAX_RED));
	controller_write(ctrl, bank+2, scale(green, MAX_GREEN));
	i2c_lock(I2C_PORT_LIGHTBAR, 0);
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
void lb_init(int use_lock)
{
	int i;

	CPRINTF("[%pT LB_init_vals ", PRINTF_TIMESTAMP_NOW);
	for (i = 0; i < ARRAY_SIZE(init_vals); i++) {
		CPRINTF("%c", '0' + i % 10);
		if (use_lock)
			i2c_lock(I2C_PORT_LIGHTBAR, 1);
		controller_write(0, init_vals[i].reg, init_vals[i].val);
		controller_write(1, init_vals[i].reg, init_vals[i].val);
		if (use_lock)
			i2c_lock(I2C_PORT_LIGHTBAR, 0);
	}
	CPRINTF("]\n");
	memset(current, 0, sizeof(current));
}

/* Just go into standby mode. No register values should change. */
void lb_off(void)
{
	CPRINTS("LB_off");
	i2c_lock(I2C_PORT_LIGHTBAR, 1);
	controller_write(0, 0x01, 0x00);
	controller_write(1, 0x01, 0x00);
	i2c_lock(I2C_PORT_LIGHTBAR, 0);
}

/* Come out of standby mode. */
void lb_on(void)
{
	CPRINTS("LB_on");
	i2c_lock(I2C_PORT_LIGHTBAR, 1);
	controller_write(0, 0x01, 0x20);
	controller_write(1, 0x01, 0x20);
	i2c_lock(I2C_PORT_LIGHTBAR, 0);
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
		i2c_lock(I2C_PORT_LIGHTBAR, 1);
		out->dump.vals[i].ic0 = controller_read(0, reg);
		out->dump.vals[i].ic1 = controller_read(1, reg);
		i2c_lock(I2C_PORT_LIGHTBAR, 0);
	}
}

/* Helper for host command to write controller registers directly */
void lb_hc_cmd_reg(const struct ec_params_lightbar *in)
{
	i2c_lock(I2C_PORT_LIGHTBAR, 1);
	controller_write(in->reg.ctrl, in->reg.reg, in->reg.value);
	i2c_lock(I2C_PORT_LIGHTBAR, 0);
}
