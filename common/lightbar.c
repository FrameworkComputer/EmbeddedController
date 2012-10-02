/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * LED controls.
 */

#include "battery.h"
#include "battery_pack.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "lightbar.h"
#include "pwm.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LIGHTBAR, outstr)
#define CPRINTF(format, args...) cprintf(CC_LIGHTBAR, format, ## args)

#define CONSOLE_COMMAND_LIGHTBAR_HELP

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

/* How many (logical) LEDs do we have? */
#define NUM_LEDS 4

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
static const uint8_t led_to_isc[] = { 0x18, 0x15, 0x18, 0x15 };

/* Scale 0-255 into max value */
static inline uint8_t scale_abs(int val, int max)
{
	return (val * max)/255;
}

/* It will often be simpler to provide an overall brightness control. */
static int brightness = 0xff;


/* So that we can make brightness changes happen instantly, we need to track
 * the current values. The values in the controllers aren't very helpful. */
static uint8_t current[NUM_LEDS][3];

/* Scale 0-255 by brightness */
static inline uint8_t scale(int val, int max)
{
	return scale_abs((val * brightness)/255, max);
}

static void lightbar_init_vals(void)
{
	CPRINTF("[%T LB_init_vals]\n");
	set_from_array(init_vals, ARRAY_SIZE(init_vals));
	memset(current, 0, sizeof(current));
}

/* Change it with this function (defined below). */
static void lightbar_brightness(int newval);


/* Helper function. */
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


/******************************************************************************/
/* Here's some state that we might want to maintain across sysjumps, just to
 * prevent the lightbar from flashing during normal boot as the EC jumps from
 * RO to RW. */
static struct {
	/* What patterns are we showing? */
	enum lightbar_sequence cur_seq;
	enum lightbar_sequence prev_seq;

	/* Quantized battery charge level: 0=low 1=med 2=high 3=full. */
	int battery_level;

	/* It's either charging or discharging. */
	int battery_is_charging;

	/* Pattern variables for state S0. */
	uint8_t w0;				/* primary phase */
	uint8_t amp;				/* amplitude */
	uint8_t ramp;				/* ramp-in for S3->S0 */
} st;

#define LB_SYSJUMP_TAG 0x4c42			/* "LB" */
static int lb_preserve_state(void)
{
	system_add_jump_tag(LB_SYSJUMP_TAG, 0, sizeof(st), &st);
	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_SYSJUMP, lb_preserve_state, HOOK_PRIO_DEFAULT);

static void lb_restore_state(void)
{
	const uint8_t *old_state = 0;
	int size;

	old_state = system_get_jump_tag(LB_SYSJUMP_TAG, 0, &size);
	if (old_state && size == sizeof(st)) {
		memcpy(&st, old_state, size);
	} else {
		st.cur_seq = st.prev_seq = LIGHTBAR_S5;
		st.battery_level = 3;
		st.w0 = 0;
		st.amp = 0;
		st.ramp = 0;
	}
	CPRINTF("[%T LB state: %d %d - %d/%d]\n",
		st.cur_seq, st.prev_seq,
		st.battery_is_charging, st.battery_level);
}

/******************************************************************************/
/* The patterns are generally dependent on the current battery level and AC
 * state. These functions obtain that information, generally by querying the
 * power manager task. In demo mode, the keyboard task forces changes to the
 * state by calling the demo_* functions directly. */
/******************************************************************************/

#ifdef CONFIG_TASK_PWM
static int last_backlight_level;
#endif

static int demo_mode;

/* Update the known state. */
static void get_battery_level(void)
{
	int pct = 0;

	if (demo_mode)
		return;

#ifdef CONFIG_TASK_PWM
	/* With nothing else to go on, use the keyboard backlight level to
	 * set the brightness. If the keyboard backlight is OFF (which it is
	 * when ambient is bright), use max brightness for lightbar. If
	 * keyboard backlight is ON, use keyboard backlight brightness.
	 */
	if (pwm_get_keyboard_backlight_enabled()) {
		pct = pwm_get_keyboard_backlight();
		if (pct != last_backlight_level) {
			last_backlight_level = pct;
			pct = (255 * pct) / 100;
			lightbar_brightness(pct);
		}
	} else
		lightbar_brightness(255);
#endif

#ifdef CONFIG_TASK_POWERSTATE
	pct = charge_get_percent();
	st.battery_is_charging = (PWR_STATE_DISCHARGE != charge_get_state());
#endif

	/* We're only using two of the four levels at the moment. */
	if (pct > LIGHTBAR_POWER_THRESHOLD_MEDIUM)
		st.battery_level = 3;
	else
		st.battery_level = 0;
}


/* Forcing functions for demo mode */

void demo_battery_level(int inc)
{
	if (!demo_mode)
		return;

	st.battery_level += inc;
	if (st.battery_level > 3)
		st.battery_level = 3;
	else if (st.battery_level < 0)
		st.battery_level = 0;

	CPRINTF("[%T LB demo: battery_level=%d]\n", st.battery_level);
}

void demo_is_charging(int ischarge)
{
	if (!demo_mode)
		return;

	st.battery_is_charging = ischarge;
	CPRINTF("[%T LB demo: battery_is_charging=%d]\n",
		st.battery_is_charging);
}

void demo_brightness(int inc)
{
	int b;

	if (!demo_mode)
		return;

	b = brightness + (inc * 16);
	if (b > 0xff)
		b = 0xff;
	else if (b < 0)
		b = 0;
	lightbar_brightness(b);
}


/******************************************************************************/
/* Basic LED control functions. Use these to implement the pretty patterns. */
/******************************************************************************/

/* Just go into standby mode. No register values should change. */
static void lightbar_off(void)
{
	CPRINTF("[%T LB_off]\n");
	controller_write(0, 0x01, 0x00);
	controller_write(1, 0x01, 0x00);
}

/* Come out of standby mode. */
static void lightbar_on(void)
{
	CPRINTF("[%T LB_on]\n");
	controller_write(0, 0x01, 0x20);
	controller_write(1, 0x01, 0x20);
}

/* LEDs are numbered 0-3, RGB values should be in 0-255.
 * If you specify too large an LED, it sets them all. */
static void lightbar_setrgb(int led, int red, int green, int blue)
{
	int i;
	if (led >= NUM_LEDS)
		for (i = 0; i < NUM_LEDS; i++)
			setrgb(i, red, green, blue);
	else
		setrgb(led, red, green, blue);
}

/* Change current display brightness (0-255) */
static void lightbar_brightness(int newval)
{
	int i;
	CPRINTF("[%T LB_bright 0x%02x]\n", newval);
	brightness = newval;
	for (i = 0; i < NUM_LEDS; i++)
		lightbar_setrgb(i, current[i][0],
				current[i][1], current[i][2]);
}

/******************************************************************************/
/* Helper functions and data. */
/******************************************************************************/

struct rgb_s {
	uint8_t r, g, b;
};

/* These are the official Google colors, in order. */
enum { BLUE = 0, RED, YELLOW, GREEN, INVALID };
static const struct rgb_s google[] = {
	{0x33, 0x69, 0xe8},			/* blue */
	{0xd5, 0x0f, 0x25},			/* red */
	{0xee, 0xb2, 0x11},			/* yellow */
	{0x00, 0x99, 0x25},			/* green */

	{0xff, 0x00, 0xFF},			/* invalid */
};

/* These are used for test patterns. */
static const struct rgb_s colors[] = {
	{0xff, 0x00, 0x00},
	{0xff, 0xff, 0x00},
	{0x00, 0xff, 0x00},
	{0x00, 0x00, 0xff},
	{0x00, 0xff, 0xff},
	{0xff, 0x00, 0xff},
	{0x00, 0x00, 0x00},
};

/* Map battery_level to one of the google colors */
static const int battery_color[] = { RED, YELLOW, GREEN, BLUE };

const float _ramp_table[] = {
	0.000000f, 0.000151f, 0.000602f, 0.001355f, 0.002408f, 0.003760f,
	0.005412f, 0.007361f, 0.009607f, 0.012149f, 0.014984f, 0.018112f,
	0.021530f, 0.025236f, 0.029228f, 0.033504f, 0.038060f, 0.042895f,
	0.048005f, 0.053388f, 0.059039f, 0.064957f, 0.071136f, 0.077573f,
	0.084265f, 0.091208f, 0.098396f, 0.105827f, 0.113495f, 0.121396f,
	0.129524f, 0.137876f, 0.146447f, 0.155230f, 0.164221f, 0.173414f,
	0.182803f, 0.192384f, 0.202150f, 0.212096f, 0.222215f, 0.232501f,
	0.242949f, 0.253551f, 0.264302f, 0.275194f, 0.286222f, 0.297379f,
	0.308658f, 0.320052f, 0.331555f, 0.343159f, 0.354858f, 0.366644f,
	0.378510f, 0.390449f, 0.402455f, 0.414519f, 0.426635f, 0.438795f,
	0.450991f, 0.463218f, 0.475466f, 0.487729f, 0.500000f, 0.512271f,
	0.524534f, 0.536782f, 0.549009f, 0.561205f, 0.573365f, 0.585481f,
	0.597545f, 0.609551f, 0.621490f, 0.633356f, 0.645142f, 0.656841f,
	0.668445f, 0.679947f, 0.691342f, 0.702621f, 0.713778f, 0.724806f,
	0.735698f, 0.746449f, 0.757051f, 0.767499f, 0.777785f, 0.787904f,
	0.797850f, 0.807616f, 0.817197f, 0.826586f, 0.835780f, 0.844770f,
	0.853553f, 0.862124f, 0.870476f, 0.878604f, 0.886505f, 0.894173f,
	0.901604f, 0.908792f, 0.915735f, 0.922427f, 0.928864f, 0.935044f,
	0.940961f, 0.946612f, 0.951995f, 0.957105f, 0.961940f, 0.966496f,
	0.970772f, 0.974764f, 0.978470f, 0.981888f, 0.985016f, 0.987851f,
	0.990393f, 0.992639f, 0.994588f, 0.996240f, 0.997592f, 0.998645f,
	0.999398f, 0.999849f, 1.000000f,
};

/* This function provides a smooth ramp up from 0.0 to 1.0 and back to 0.0,
 * for input from 0x00 to 0xff. */
static inline float cycle_010(uint8_t i)
{
	return i < 128 ? _ramp_table[i] : _ramp_table[256-i];
}

/* This function provides a smooth oscillation between -0.5 and +0.5.
 * Zero starts at 0x00. */
static inline float cycle_0P0N0(uint8_t i)
{
	return cycle_010(i+64) - 0.5f;
}

/******************************************************************************/
/* Here's where we keep messages waiting to be delivered to the lightbar task.
 * If more than one is sent before the task responds, we only want to deliver
 * the latest one. */
static uint32_t pending_msg;
/* And here's the task event that we use to trigger delivery. */
#define PENDING_MSG 1

/* Interruptible delay. */
#define WAIT_OR_RET(A) do { \
	uint32_t msg = task_wait_event(A); \
	if (TASK_EVENT_CUSTOM(msg) == PENDING_MSG) \
		return PENDING_MSG; } while (0)

/* Handy conversions */
#define MSECS(a) ((a) * 1000)
#define SEC(a) ((a) * 1000000)


/******************************************************************************/
/* Here are the preprogrammed sequences. */
/******************************************************************************/

/* Pulse google colors once, off to on to off. */
static uint32_t pulse_google_colors(void)
{
	int w, i, r, g, b;
	float f;

	for (w = 0; w < 128; w += 2) {
		f = cycle_010(w);
		for (i = 0; i < NUM_LEDS; i++) {
			r = google[i].r * f;
			g = google[i].g * f;
			b = google[i].b * f;
			lightbar_setrgb(i, r, g, b);
		}
		WAIT_OR_RET(2500);
	}
	for (w = 128; w <= 256; w++) {
		f = cycle_010(w);
		for (i = 0; i < NUM_LEDS; i++) {
			r = google[i].r * f;
			g = google[i].g * f;
			b = google[i].b * f;
			lightbar_setrgb(i, r, g, b);
		}
		WAIT_OR_RET(10000);
	}

	return 0;
}

/* Constants */
#define MIN_S0 0.25f
#define MAX_S0 1.0f
#define BASE_S0 ((MIN_S0 + MAX_S0) * 0.5f)
#define OSC_S0 (MAX_S0 - MIN_S0)

/* CPU is waking from sleep. */
static uint32_t sequence_S3S0(void)
{
	int w, r, g, b;
	float f;
	int ci;
	uint32_t res;

	lightbar_init_vals();
	lightbar_on();

	res = pulse_google_colors();
	if (res)
		return res;

	/* Ramp up to base brightness. */
	get_battery_level();
	ci = battery_color[st.battery_level];
	for (w = 0; w <= 128; w++) {
		f = cycle_010(w) * BASE_S0;
		r = google[ci].r * f;
		g = google[ci].g * f;
		b = google[ci].b * f;
		lightbar_setrgb(NUM_LEDS, r, g, b);
		WAIT_OR_RET(2000);
	}

	/* Initial conditions */
	st.w0 = 0;
	st.amp = 0;
	st.ramp = 0;

	/* Ready for S0 */
	return 0;
}

/* CPU is fully on */
static uint32_t sequence_S0(void)
{
	int tick, last_tick;
	timestamp_t start, now;
	uint32_t r, g, b;
	int i, ci;
	uint8_t w, target_amp;
	float f, ff;

	start = get_time();
	tick = last_tick = 0;

	lightbar_setrgb(NUM_LEDS, 0, 0, 0);
	lightbar_on();

	while (1) {
		now = get_time();

		/* Only check the battery state every few seconds. The battery
		 * charging task doesn't update as quickly as we do, and isn't
		 * always valid for a bit after jumping from RO->RW. */
		tick = (now.le.lo - start.le.lo) / SEC(1);
		if (tick % 4 == 3 && tick != last_tick) {
			get_battery_level();
			last_tick = tick;
		}

		/* Calculate the colors */
		ci = battery_color[st.battery_level];
		ff = st.amp / 255.0f;
		for (i = 0; i < NUM_LEDS; i++) {
			w = st.w0 - i * 24 * st.ramp / 255;
			f = BASE_S0 + OSC_S0 * cycle_0P0N0(w) * ff;
			r = google[ci].r * f;
			g = google[ci].g * f;
			b = google[ci].b * f;
			lightbar_setrgb(i, r, g, b);
		}

		/* Move gradually towards the target amplitude */
		target_amp = st.battery_is_charging ? 0xff : 0x80;
		if (st.amp > target_amp)
			st.amp--;
		else if (st.amp < target_amp)
			st.amp++;

		/* Increment the phase */
		if (st.battery_is_charging) {
			st.w0--;
			WAIT_OR_RET(MSECS(2 * 15));
		} else {
			st.w0++;
			WAIT_OR_RET(MSECS(3 * 15));
		}

		/* Continue ramping in if needed */
		if (st.ramp < 0xff)
			st.ramp++ ;
	}
	return 0;
}

/* CPU is going to sleep. */
static uint32_t sequence_S0S3(void)
{
	int w, i, r, g, b;
	float f;
	uint8_t drop[NUM_LEDS][3];

	/* Grab current colors */
	memcpy(drop, current, sizeof(drop));

	/* Fade down to black */
	for (w = 128; w <= 256; w++) {
		f = cycle_010(w);
		for (i = 0; i < NUM_LEDS; i++) {
			r = drop[i][0] * f;
			g = drop[i][1] * f;
			b = drop[i][2] * f;
			lightbar_setrgb(i, r, g, b);
		}
		WAIT_OR_RET(2000);
	}

	/* pulse once and done */
	return pulse_google_colors();
}

/* CPU is sleeping */
static uint32_t sequence_S3(void)
{
	int r, g, b;
	int w;
	float f;
	int ci;

	lightbar_off();
	lightbar_init_vals();
	lightbar_setrgb(NUM_LEDS, 0, 0, 0);
	while (1) {
		WAIT_OR_RET(SEC(15));
		get_battery_level();

		/* only pulse if we're off AC and the battery level is low */
		if (st.battery_is_charging || st.battery_level > 0)
			continue;

		/* pulse once */
		ci = battery_color[st.battery_level];
		lightbar_on();
		for (w = 0; w < 255; w += 5) {
			f = cycle_010(w);
			r = google[ci].r * f;
			g = google[ci].g * f;
			b = google[ci].b * f;
			lightbar_setrgb(NUM_LEDS, r, g, b);
			WAIT_OR_RET(15000);
		}
		lightbar_setrgb(NUM_LEDS, 0, 0, 0);
		lightbar_off();
	}
	return 0;
}


/* CPU is powering up. We generally boot fast enough that we don't have time
 * to do anything interesting in the S3 state, but go straight on to S0. */
static uint32_t sequence_S5S3(void)
{
	/* The controllers need 100us after power is applied before they'll
	 * respond. Don't return early, because we still want to initialize the
	 * lightbar even if another message comes along while we're waiting. */
	usleep(100);
	lightbar_init_vals();
	lightbar_setrgb(NUM_LEDS, 0, 0, 0);
	lightbar_on();
	return 0;
}

/* Sleep to off. The S3->S5 transition takes about 10msec, so just wait. */
static uint32_t sequence_S3S5(void)
{
	lightbar_off();
	WAIT_OR_RET(-1);
	return 0;
}

/* CPU is off. The lightbar loses power when the CPU is in S5, so there's
 * nothing to do. We'll just wait here until the state changes. */
static uint32_t sequence_S5(void)
{
	WAIT_OR_RET(-1);
	return 0;
}

/* Used by factory. */
static uint32_t sequence_TEST_inner(void)
{
	int i, k, r, g, b;
	int kmax = 254;
	int kstep = 8;

	lightbar_init_vals();
	lightbar_on();
	for (i = 0; i < ARRAY_SIZE(colors); i++) {
		for (k = 0; k <= kmax; k += kstep) {
			r = colors[i].r ? k : 0;
			g = colors[i].g ? k : 0;
			b = colors[i].b ? k : 0;
			lightbar_setrgb(NUM_LEDS, r, g, b);
		}
		WAIT_OR_RET(10000);
	}
	for (k = kmax; k >= 0; k -= kstep) {
		r = colors[i].r ? k : 0;
		g = colors[i].g ? k : 0;
		b = colors[i].b ? k : 0;
		lightbar_setrgb(NUM_LEDS, r, g, b);
		WAIT_OR_RET(10000);
	}

	lightbar_setrgb(NUM_LEDS, r, g, b);
	return 0;
}

static uint32_t sequence_TEST(void)
{
	int tmp;
	uint32_t r;

	/* Force brightness to max, then restore it */
	tmp = brightness;
	brightness = 255;
	r = sequence_TEST_inner();
	brightness = tmp;
	return r;
}

/* This uses the auto-cycling features of the controllers to make a semi-random
 * pattern of slowly fading colors. This is interesting only because it doesn't
 * require any effort from the EC. */
static uint32_t sequence_PULSE(void)
{
	uint32_t msg;
	int r = scale(255, MAX_RED);
	int g = scale(255, MAX_BLUE);
	int b = scale(255, MAX_GREEN);
	struct initdata_s pulse_vals[] = {
		{0x11, 0xce},
		{0x12, 0x67},
		{0x13, 0xef},
		{0x15, b},
		{0x16, r},
		{0x17, g},
		{0x18, b},
		{0x19, r},
		{0x1a, g},
	};

	lightbar_init_vals();
	lightbar_on();

	set_from_array(pulse_vals, ARRAY_SIZE(pulse_vals));
	controller_write(1, 0x13, 0xcd);	/* this one's different */

	/* Not using WAIT_OR_RET() here, because we want to clean up when we're
	 * done. The only way out is to get a message. */
	msg = task_wait_event(-1);
	lightbar_init_vals();
	return TASK_EVENT_CUSTOM(msg);
}



/* The host CPU (or someone) is going to poke at the lightbar directly, so we
 * don't want the EC messing with it. We'll just sit here and ignore all
 * other messages until we're told to continue. */
static uint32_t sequence_STOP(void)
{
	uint32_t msg;

	do {
		msg = TASK_EVENT_CUSTOM(task_wait_event(-1));
		CPRINTF("[%T LB_stop got pending_msg %d]\n", pending_msg);
	} while (msg != PENDING_MSG || pending_msg != LIGHTBAR_RUN);

	/* Q: What should we do if the host shuts down? */
	/* A: Nothing. We could be driving from the EC console. */

	CPRINTF("[%T LB_stop->running]\n");
	return 0;
}

/* Telling us to run when we're already running should do nothing. */
static uint32_t sequence_RUN(void)
{
	return 0;
}

/* We shouldn't come here, but if we do it shouldn't hurt anything */
static uint32_t sequence_ERROR(void)
{
	lightbar_init_vals();
	lightbar_on();

	lightbar_setrgb(0, 255, 255, 255);
	lightbar_setrgb(1, 255, 0, 255);
	lightbar_setrgb(2, 0, 255, 255);
	lightbar_setrgb(3, 255, 255, 255);

	WAIT_OR_RET(10000000);
	return 0;
}


static const struct {
	uint8_t led;
	uint8_t r, g, b;
	unsigned int delay;
} konami[] = {

	{1, 0xff, 0xff, 0x00, 0},
	{2, 0xff, 0xff, 0x00, 100000},
	{1, 0x00, 0x00, 0x00, 0},
	{2, 0x00, 0x00, 0x00, 100000},

	{1, 0xff, 0xff, 0x00, 0},
	{2, 0xff, 0xff, 0x00, 100000},
	{1, 0x00, 0x00, 0x00, 0},
	{2, 0x00, 0x00, 0x00, 100000},

	{0, 0x00, 0x00, 0xff, 0},
	{3, 0x00, 0x00, 0xff, 100000},
	{0, 0x00, 0x00, 0x00, 0},
	{3, 0x00, 0x00, 0x00, 100000},

	{0, 0x00, 0x00, 0xff, 0},
	{3, 0x00, 0x00, 0xff, 100000},
	{0, 0x00, 0x00, 0x00, 0},
	{3, 0x00, 0x00, 0x00, 100000},

	{0, 0xff, 0x00, 0x00, 0},
	{1, 0xff, 0x00, 0x00, 100000},
	{0, 0x00, 0x00, 0x00, 0},
	{1, 0x00, 0x00, 0x00, 100000},

	{2, 0x00, 0xff, 0x00, 0},
	{3, 0x00, 0xff, 0x00, 100000},
	{2, 0x00, 0x00, 0x00, 0},
	{3, 0x00, 0x00, 0x00, 100000},

	{0, 0xff, 0x00, 0x00, 0},
	{1, 0xff, 0x00, 0x00, 100000},
	{0, 0x00, 0x00, 0x00, 0},
	{1, 0x00, 0x00, 0x00, 100000},

	{2, 0x00, 0xff, 0x00, 0},
	{3, 0x00, 0xff, 0x00, 100000},
	{2, 0x00, 0x00, 0x00, 0},
	{3, 0x00, 0x00, 0x00, 100000},

	{0, 0x00, 0xff, 0xff, 0},
	{2, 0x00, 0xff, 0xff, 100000},
	{0, 0x00, 0x00, 0x00, 0},
	{2, 0x00, 0x00, 0x00, 150000},

	{1, 0xff, 0x00, 0xff, 0},
	{3, 0xff, 0x00, 0xff, 100000},
	{1, 0x00, 0x00, 0x00, 0},
	{3, 0x00, 0x00, 0x00, 250000},

	{4, 0xff, 0xff, 0xff, 100000},
	{4, 0x00, 0x00, 0x00, 100000},

	{4, 0xff, 0xff, 0xff, 100000},
	{4, 0x00, 0x00, 0x00, 100000},

	{4, 0xff, 0xff, 0xff, 100000},
	{4, 0x00, 0x00, 0x00, 100000},

	{4, 0xff, 0xff, 0xff, 100000},
	{4, 0x00, 0x00, 0x00, 100000},

	{4, 0xff, 0xff, 0xff, 100000},
	{4, 0x00, 0x00, 0x00, 100000},

	{4, 0xff, 0xff, 0xff, 100000},
	{4, 0x00, 0x00, 0x00, 100000},

};

static uint32_t sequence_KONAMI(void)
{
	int i;
	int tmp;

	lightbar_off();
	lightbar_init_vals();
	lightbar_on();

	tmp = brightness;
	brightness = 255;

	for (i = 0; i < ARRAY_SIZE(konami); i++) {
		lightbar_setrgb(konami[i].led,
				  konami[i].r, konami[i].g, konami[i].b);
		if (konami[i].delay)
			usleep(konami[i].delay);
	}

	brightness = tmp;
	return 0;
}

/****************************************************************************/
/* The main lightbar task. It just cycles between various pretty patterns. */
/****************************************************************************/

/* Link each sequence with a command to invoke it. */
struct lightbar_cmd_t {
	const char * const string;
	uint32_t (*sequence)(void);
};

#define LBMSG(state) { #state, sequence_##state }
#include "lightbar_msg_list.h"
static struct lightbar_cmd_t lightbar_cmds[] = {
	LIGHTBAR_MSG_LIST
};
#undef LBMSG

void lightbar_task(void)
{
	uint32_t msg;

	CPRINTF("[%T LB task starting]\n");

	lb_restore_state();

	while (1) {
		CPRINTF("[%T LB task %d = %s]\n",
			st.cur_seq, lightbar_cmds[st.cur_seq].string);
		msg = lightbar_cmds[st.cur_seq].sequence();
		if (TASK_EVENT_CUSTOM(msg) == PENDING_MSG) {
			CPRINTF("[%T LB msg %d = %s]\n", pending_msg,
				lightbar_cmds[pending_msg].string);
			st.prev_seq = st.cur_seq;
			st.cur_seq = pending_msg;
		} else {
			CPRINTF("[%T LB msg 0x%x]\n", msg);
			switch (st.cur_seq) {
			case LIGHTBAR_S5S3:
				st.cur_seq = LIGHTBAR_S3;
				break;
			case LIGHTBAR_S3S0:
				st.cur_seq = LIGHTBAR_S0;
				break;
			case LIGHTBAR_S0S3:
				st.cur_seq = LIGHTBAR_S3;
				break;
			case LIGHTBAR_S3S5:
				st.cur_seq = LIGHTBAR_S5;
				break;
			case LIGHTBAR_TEST:
			case LIGHTBAR_STOP:
			case LIGHTBAR_RUN:
			case LIGHTBAR_ERROR:
			case LIGHTBAR_KONAMI:
				st.cur_seq = st.prev_seq;
			default:
				break;
			}
		}
	}
}

/* Function to request a preset sequence from the lightbar task. */
void lightbar_sequence(enum lightbar_sequence num)
{
	if (num > 0 && num < LIGHTBAR_NUM_SEQUENCES) {
		CPRINTF("[%T LB_seq %d = %s]\n", num,
			lightbar_cmds[num].string);
		pending_msg = num;
		task_set_event(TASK_ID_LIGHTBAR,
			       TASK_EVENT_WAKE | TASK_EVENT_CUSTOM(PENDING_MSG),
			       0);
	} else
		CPRINTF("[%T LB_seq %d - ignored]\n", num);
}

/****************************************************************************/
/* Get notifications from other parts of the system */

static int lightbar_startup(void)
{
	lightbar_sequence(LIGHTBAR_S5S3);
	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, lightbar_startup, HOOK_PRIO_DEFAULT);

static int lightbar_resume(void)
{
	lightbar_sequence(LIGHTBAR_S3S0);
	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, lightbar_resume, HOOK_PRIO_DEFAULT);

static int lightbar_suspend(void)
{
	lightbar_sequence(LIGHTBAR_S0S3);
	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, lightbar_suspend, HOOK_PRIO_DEFAULT);

static int lightbar_shutdown(void)
{
	lightbar_sequence(LIGHTBAR_S3S5);
	return EC_SUCCESS;
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, lightbar_shutdown, HOOK_PRIO_DEFAULT);


/****************************************************************************/
/* Generic command-handling (should work the same for both console & LPC) */
/****************************************************************************/

static const uint8_t dump_reglist[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a,                         0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a
};

static void do_cmd_dump(struct ec_response_lightbar *out)
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

static void do_cmd_rgb(uint8_t led,
		       uint8_t red, uint8_t green, uint8_t blue)
{
	int i;

	if (led >= NUM_LEDS)
		for (i = 0; i < NUM_LEDS; i++)
			lightbar_setrgb(i, red, green, blue);
	else
		lightbar_setrgb(led, red, green, blue);
}


/****************************************************************************/
/* Host commands via LPC bus */
/****************************************************************************/

static int lpc_cmd_lightbar(struct host_cmd_handler_args *args)
{
	const struct ec_params_lightbar *in = args->params;
	struct ec_response_lightbar *out = args->response;

	switch (in->cmd) {
	case LIGHTBAR_CMD_DUMP:
		do_cmd_dump(out);
		args->response_size = sizeof(out->dump);
		break;
	case LIGHTBAR_CMD_OFF:
		lightbar_off();
		break;
	case LIGHTBAR_CMD_ON:
		lightbar_on();
		break;
	case LIGHTBAR_CMD_INIT:
		lightbar_init_vals();
		break;
	case LIGHTBAR_CMD_BRIGHTNESS:
		lightbar_brightness(in->brightness.num);
		break;
	case LIGHTBAR_CMD_SEQ:
		lightbar_sequence(in->seq.num);
		break;
	case LIGHTBAR_CMD_REG:
		controller_write(in->reg.ctrl,
				 in->reg.reg,
				 in->reg.value);
		break;
	case LIGHTBAR_CMD_RGB:
		do_cmd_rgb(in->rgb.led,
			   in->rgb.red,
			   in->rgb.green,
			   in->rgb.blue);
		break;
	case LIGHTBAR_CMD_GET_SEQ:
		out->get_seq.num = st.cur_seq;
		args->response_size = sizeof(out->get_seq);
		break;
	case LIGHTBAR_CMD_DEMO:
		demo_mode = in->demo.num ? 1 : 0;
		CPRINTF("[%T LB_demo %d]\n", demo_mode);
		break;
	default:
		CPRINTF("[%T LB bad cmd 0x%x]\n", in->cmd);
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_LIGHTBAR_CMD,
		     lpc_cmd_lightbar,
		     EC_VER_MASK(0));


/****************************************************************************/
/* EC console commands */
/****************************************************************************/

#ifdef CONSOLE_COMMAND_LIGHTBAR_HELP
static int help(const char *cmd)
{
	ccprintf("Usage:\n");
	ccprintf("  %s                       - dump all regs\n", cmd);
	ccprintf("  %s off                   - enter standby\n", cmd);
	ccprintf("  %s on                    - leave standby\n", cmd);
	ccprintf("  %s init                  - load default vals\n", cmd);
	ccprintf("  %s brightness NUM        - set intensity (0-ff)\n", cmd);
	ccprintf("  %s seq [NUM|SEQUENCE]    - run given pattern"
		 " (no arg for list)\n", cmd);
	ccprintf("  %s CTRL REG VAL          - set LED controller regs\n", cmd);
	ccprintf("  %s LED RED GREEN BLUE    - set color manually"
		 " (LED=4 for all)\n", cmd);
	ccprintf("  %s demo [0|1]            - turn demo mode on & off\n", cmd);
	return EC_SUCCESS;
}
#endif

static uint8_t find_msg_by_name(const char *str)
{
	uint8_t i;
	for (i = 0; i < LIGHTBAR_NUM_SEQUENCES; i++)
		if (!strcasecmp(str, lightbar_cmds[i].string))
			return i;

	return LIGHTBAR_NUM_SEQUENCES;
}

static void show_msg_names(void)
{
	int i;
	ccprintf("Sequences:");
	for (i = 0; i < LIGHTBAR_NUM_SEQUENCES; i++)
		ccprintf(" %s", lightbar_cmds[i].string);
	ccprintf("\nCurrent = 0x%x %s\n", st.cur_seq,
		 lightbar_cmds[st.cur_seq].string);
}

static int command_lightbar(int argc, char **argv)
{
	int i;
	uint8_t num;
	struct ec_response_lightbar out;

	if (argc == 1) {			/* no args = dump 'em all */
		do_cmd_dump(&out);
		for (i = 0; i < ARRAY_SIZE(dump_reglist); i++)
			ccprintf(" %02x     %02x     %02x\n",
				 out.dump.vals[i].reg,
				 out.dump.vals[i].ic0,
				 out.dump.vals[i].ic1);

		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "init")) {
		lightbar_init_vals();
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "off")) {
		lightbar_off();
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "on")) {
		lightbar_on();
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "brightness")) {
		char *e;
		if (argc > 2) {
			num = 0xff & strtoi(argv[2], &e, 16);
			lightbar_brightness(num);
		}
		ccprintf("brightness is %02x\n", brightness);
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "demo")) {
		if (argc > 2) {
			if (!strcasecmp(argv[2], "on") ||
			    argv[2][0] == '1')
				demo_mode = 1;
			else if (!strcasecmp(argv[2], "off") ||
				 argv[2][0] == '0')
				demo_mode = 0;
			else
				return EC_ERROR_PARAM1;
		}
		ccprintf("demo mode is %s\n", demo_mode ? "on" : "off");
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "seq")) {
		char *e;
		uint8_t num;
		if (argc == 2) {
			show_msg_names();
			return 0;
		}
		num = 0xff & strtoi(argv[2], &e, 16);
		if (*e)
			num = find_msg_by_name(argv[2]);
		if (num >= LIGHTBAR_NUM_SEQUENCES)
			return EC_ERROR_PARAM2;
		lightbar_sequence(num);
		return EC_SUCCESS;
	}

	if (argc == 4) {
		char *e;
		uint8_t ctrl, reg, val;
		ctrl = 0xff & strtoi(argv[1], &e, 16);
		reg = 0xff & strtoi(argv[2], &e, 16);
		val = 0xff & strtoi(argv[3], &e, 16);
		controller_write(ctrl, reg, val);
		return EC_SUCCESS;
	}

	if (argc == 5) {
		char *e;
		uint8_t led, r, g, b;
		led = strtoi(argv[1], &e, 16);
		r = strtoi(argv[2], &e, 16);
		g = strtoi(argv[3], &e, 16);
		b = strtoi(argv[4], &e, 16);
		do_cmd_rgb(led, r, g, b);
		return EC_SUCCESS;
	}

#ifdef CONSOLE_COMMAND_LIGHTBAR_HELP
	help(argv[0]);
#endif

	return EC_ERROR_INVAL;
}
DECLARE_CONSOLE_COMMAND(lightbar, command_lightbar,
			"[on | off | init | brightness | seq] | [ctrl reg val]",
			"Get/set lightbar state",
			NULL);
