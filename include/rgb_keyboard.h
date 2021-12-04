/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdbool.h>

#include "common.h"
#include "ec_commands.h"

/* Use this instead of '3' for readability where applicable. */
#define SIZE_OF_RGB		sizeof(struct rgb_s)

enum rgbkbd_demo {
	RGBKBD_DEMO_OFF = 0,
	RGBKBD_DEMO_FLOW = 1,
	RGBKBD_DEMO_DOT = 2,
	RGBKBD_DEMO_COUNT
};

struct rgbkbd_cfg {
	/* Driver for LED IC */
	const struct rgbkbd_drv * const drv;
	/* SPI/I2C port (i.e. index of spi_devices[], i2c_ports[]) */
	union {
		const uint8_t i2c;
		const uint8_t spi;
	};
	/* Grid size */
	const uint8_t col_len;
	const uint8_t row_len;
};

struct rgbkbd {
	/* Static configuration */
	const struct rgbkbd_cfg * const cfg;
	/* Current state of the port */
	enum rgbkbd_state state;
	/* Buffer containing color info for each dot. */
	struct rgb_s *buf;
};

struct rgbkbd_drv {
	/* Reset charger chip. */
	int (*reset)(struct rgbkbd *ctx);
	/* Initialize the charger. */
	int (*init)(struct rgbkbd *ctx);
	/* Enable/disable the charger. Usually disabled means stand-by. */
	int (*enable)(struct rgbkbd *ctx, bool enable);

	/**
	 * Set the colors of multiple RGB-LEDs.
	 *
	 * @param ctx    Context.
	 * @param offset Starting LED position.
	 * @param color  Array of colors to set. Must be as long as <len>.
	 * @param len    Length of <color> array.
	 * @return enum ec_error_list.
	 */
	int (*set_color)(struct rgbkbd *ctx, uint8_t offset,
			 struct rgb_s *color, uint8_t len);
	/**
	 * Set the scale of multiple LEDs
	 *
	 * @param ctx    Context.
	 * @param offset Starting LED position.
	 * @param scale  Scale to be set.
	 * @param len    Length of LEDs to be set.
	 * @return enum ec_error_list
	 */
	int (*set_scale)(struct rgbkbd *ctx, uint8_t offset,
			 uint8_t scale, uint8_t len);
	/**
	 * Set global current control.
	 *
	 * @param level Global current control to set.
	 * @return enum ec_error_list.
	 */
	int (*set_gcc)(struct rgbkbd *ctx, uint8_t level);
};

/*
 * The matrix consists of multiple grids:
 *
 *   +=========-== Matrix =============+
 *   | +--- Grid1 ---+ +--- Grid2 ---+ |    ^
 *   | | A C         | | E           | |    |
 *   | | B           | |             | | rgbkbd_vsize
 *   | |           D | |             | |    |
 *   | +-------------+ +-------------+ |    v
 *   +=================================+
 *
 *     <-------- rgbkbd_hsize ------->
 *
 * Grids are assumed to be horizontally adjacent. That is, the matrix row size
 * is fixed and the matrix column size is a multiple of the grid's column size.
 *
 * Grid coordinate format is (column, row). In the diagram above,
 *   A = (0, 0)
 *   B = (0, 1)
 *   C = (1, 0)
 *
 * In each grid, LEDs are also sequentially indexed. That is,
 *   A = 0
 *   B = 1
 *   C = rgbkbd_vsize
 *   E = 0
 *
 * Matrix coordinate format is (x, y), where x is a horizontal position and y
 * is a vertical position.
 *   E = (grid0_hsize, 0)
 */
extern struct rgbkbd rgbkbds[];
extern const uint8_t rgbkbd_count;
extern const uint8_t rgbkbd_hsize;
extern const uint8_t rgbkbd_vsize;

/*
 * Called to power on or off the RGB keyboard module.
 */
__override_proto void board_enable_rgb_keyboard(bool enable);
