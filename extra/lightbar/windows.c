/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xcb/xcb.h>

#include "simulation.h"

/*****************************************************************************/
/* Window drawing stuff */

/* Dimensions - may change */
static int win_w = 1024;
static int win_h = 32;

static xcb_connection_t *c;
static xcb_screen_t *screen;
static xcb_drawable_t win;
static xcb_gcontext_t foreground;
static xcb_colormap_t colormap_id;

static int fake_power;

void init_windows(void)
{
	uint32_t mask = 0;
	uint32_t values[2];

	/* Open the connection to the X server */
	c = xcb_connect(NULL, NULL);

	/* Get the first screen */
	screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;

	/* Get a colormap */
	colormap_id = xcb_generate_id(c);
	xcb_create_colormap(c, XCB_COLORMAP_ALLOC_NONE,
			    colormap_id, screen->root, screen->root_visual);

	/* Create foreground GC */
	foreground = xcb_generate_id(c);
	mask = XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES;
	values[0] = screen->white_pixel;
	values[1] = 0;
	xcb_create_gc(c, foreground, screen->root, mask, values);

	/* Create the window */
	win = xcb_generate_id(c);
	mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	values[0] = screen->black_pixel;
	values[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS;
	xcb_create_window(c,				 /* Connection */
			  XCB_COPY_FROM_PARENT,		 /* depth */
			  win,				 /* window Id */
			  screen->root,			 /* parent window */
			  0, 0,				 /* x, y */
			  win_w, win_h,			 /* width, height */
			  10,				 /* border_width */
			  XCB_WINDOW_CLASS_INPUT_OUTPUT, /* class */
			  screen->root_visual,		 /* visual */
			  mask, values);		 /* masks */

	/* Map the window on the screen */
	xcb_map_window(c, win);

	/* We flush the request */
	xcb_flush(c);
}

void cleanup(void)
{
	xcb_destroy_window(c, win);
	xcb_free_gc(c, foreground);
	xcb_free_colormap(c, colormap_id);
	xcb_disconnect(c);
}

/*****************************************************************************/
/* Draw the lightbar elements */

/* xcb likes 16-bit colors */
uint16_t leds[NUM_LEDS][3] = {
	{0xffff, 0x0000, 0x0000},
	{0x0000, 0xffff, 0x0000},
	{0x0000, 0x0000, 0xffff},
	{0xffff, 0xffff, 0x0000},
};
pthread_mutex_t leds_mutex = PTHREAD_MUTEX_INITIALIZER;

void change_gc_color(uint16_t red, uint16_t green, uint16_t blue)
{
	uint32_t mask = 0;
	uint32_t values[2];
	xcb_alloc_color_reply_t *reply;

	reply = xcb_alloc_color_reply(c,
				      xcb_alloc_color(c, colormap_id,
						      red, green, blue),
				      NULL);
	assert(reply);

	mask = XCB_GC_FOREGROUND;
	values[0] = reply->pixel;
	xcb_change_gc(c, foreground, mask, values);
	free(reply);
}

void update_window(void)
{
	xcb_segment_t segments[] = {
		{0, 0, win_w, win_h},
		{0, win_h, win_w, 0},
	};
	xcb_rectangle_t rect;
	int w = win_w / NUM_LEDS;
	int i;
	uint16_t copyleds[NUM_LEDS][3];

	if (fake_power) {
		pthread_mutex_lock(&leds_mutex);
		memcpy(copyleds, leds, sizeof(leds));
		pthread_mutex_unlock(&leds_mutex);

		for (i = 0; i < NUM_LEDS; i++) {
			rect.x = i * w;
			rect.y = 0;
			rect.width = w;
			rect.height = win_h;

			change_gc_color(copyleds[i][0],
					copyleds[i][1],
					copyleds[i][2]);

			xcb_poly_fill_rectangle(c, win, foreground, 1, &rect);
		}
	} else {
		rect.x = 0;
		rect.y = 0;
		rect.width = win_w;
		rect.height = win_h;

		change_gc_color(0, 0, 0);
		xcb_poly_fill_rectangle(c, win, foreground, 1, &rect);

		change_gc_color(0x8080, 0, 0);

		for (i = 0; i < NUM_LEDS; i++) {
			segments[0].x1 = i * w;
			segments[0].y1 = 0;
			segments[0].x2 = segments[0].x1 + w;
			segments[0].y2 = win_h;
			segments[1].x1 = segments[0].x1;
			segments[1].y1 = win_h;
			segments[1].x2 = segments[0].x2;
			segments[1].y2 = 0;
			xcb_poly_segment(c, win, foreground, 2, segments);
		}
	}

	xcb_flush(c);
}

void setrgb(int led, int red, int green, int blue)
{
	led %= NUM_LEDS;

	pthread_mutex_lock(&leds_mutex);
	leds[led][0] = red << 8 | red;
	leds[led][1] = green << 8 | green;
	leds[led][2] = blue << 8 | blue;
	pthread_mutex_unlock(&leds_mutex);

	update_window();
}

/*****************************************************************************/
/* lb_common stubs */



/* Brightness serves no purpose here. It's automatic on the Chromebook. */
static int brightness = 0xc0;
void lb_set_brightness(unsigned int newval)
{
	brightness = newval;
}
uint8_t lb_get_brightness(void)
{
	return brightness;
}

void lb_set_rgb(unsigned int led, int red, int green, int blue)
{
	int i;
	if (led >= NUM_LEDS)
		for (i = 0; i < NUM_LEDS; i++)
			setrgb(i, red, green, blue);
	else
		setrgb(led, red, green, blue);
}

int lb_get_rgb(unsigned int led, uint8_t *red, uint8_t *green, uint8_t *blue)
{
	led %= NUM_LEDS;
	pthread_mutex_lock(&leds_mutex);
	*red = leds[led][0];
	*green = leds[led][1];
	*blue = leds[led][2];
	pthread_mutex_unlock(&leds_mutex);
	return 0;
}

void lb_init(void)
{
	if (fake_power)
		lb_set_rgb(NUM_LEDS, 0, 0, 0);
};
void lb_off(void)
{
	fake_power = 0;
	update_window();
};
void lb_on(void)
{
	fake_power = 1;
	update_window();
};
void lb_hc_cmd_dump(struct ec_response_lightbar *out)
{
	printf("lightbar is %s\n", fake_power ? "on" : "off");
	memset(out, fake_power, sizeof(*out));
};
void lb_hc_cmd_reg(const struct ec_params_lightbar *in) { };

int lb_power(int enabled)
{
	return fake_power;
}


/*****************************************************************************/
/* Event handling stuff */

void *entry_windows(void *ptr)
{
	xcb_generic_event_t *e;
	xcb_expose_event_t *ev;
	xcb_button_press_event_t *bv;
	int chg = 1;

	while ((e = xcb_wait_for_event(c))) {

		switch (e->response_type & ~0x80) {
		case XCB_EXPOSE:
			ev = (xcb_expose_event_t *)e;
			if (win_w != ev->width || win_h != ev->height) {
				win_w = ev->width;
				win_h = ev->height;
			}
			update_window();
			break;
		case XCB_BUTTON_PRESS:
			bv = (xcb_button_press_event_t *)e;
			switch (bv->detail) {
			case 1:
				demo_battery_level(-1);
				break;
			case 3:
				demo_battery_level(+1);
				break;
			case 2:
				chg = !chg;
				demo_is_charging(chg);
				break;
			}
			break;
		}

		free(e);
	}

	cleanup();
	exit(0);
	return 0;
}
