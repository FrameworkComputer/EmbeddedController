/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_RGB_KEYBOARD_H
#define __CROS_EC_RGB_KEYBOARD_H

#include "common.h"
#include "ec_commands.h"
#include "stddef.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Use this instead of '3' for readability where applicable. */
#define SIZE_OF_RGB sizeof(struct rgb_s)

#define RGBKBD_MAX_GCC_LEVEL 0xff
#define RGBKBD_MAX_SCALE 0xff

#define RGBKBD_CTX_TO_GRID(ctx) ((ctx) - &rgbkbds[0])

struct rgbkbd_cfg {
	/* Driver for LED IC */
	const struct rgbkbd_drv *const drv;
	/* SPI/I2C port (i.e. index of spi_devices[], i2c_ports[]) */
	union {
		const uint8_t i2c;
		const uint8_t spi;
	};
	/* Grid size */
	const uint8_t col_len;
	const uint8_t row_len;
};

struct rgbkbd_init {
	/* Global current control */
	const uint8_t gcc;
	/* LED brightness  */
	const struct rgb_s scale;
	/* Color */
	const struct rgb_s color;
};

/**
 * Register init settings.
 *
 * Must be called before rgbkbd_drv->init() is called.
 *
 * @param setting
 */
void rgbkbd_register_init_setting(const struct rgbkbd_init *setting);

struct rgbkbd {
	/* Static configuration */
	const struct rgbkbd_cfg *const cfg;
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
	int (*set_scale)(struct rgbkbd *ctx, uint8_t offset, struct rgb_s scale,
			 uint8_t len);
	/**
	 * Set global current control.
	 *
	 * @param level Global current control to set.
	 * @return enum ec_error_list.
	 */
	int (*set_gcc)(struct rgbkbd *ctx, uint8_t level);
};

/* Represents a position of an LED in RGB matrix. */
struct rgbkbd_coord {
	uint8_t y : 3;
	uint8_t x : 5;
};

/*
 * For optimization, LED coordinates are encoded in LED IDs. This saves us one
 * translation.
 */
union rgbkbd_coord_u8 {
	uint8_t u8;
	struct rgbkbd_coord coord;
};

#define RGBKBD_COORD(x, y) ((x) << 3 | (y))
/* Delimiter for rgbkbd_map data */
#define RGBKBD_DELM 0xff
/* Non-existent entry indicator for rgbkbd_table */
#define RGBKBD_NONE 0x00

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
 * rgbkbd_type describes the rgb kb type supported.
 * i.e. Number of zones and number of LEDs
 */
extern enum ec_rgbkbd_type rgbkbd_type;

/*
 * rgbkbd_map describes a mapping from key IDs to LED IDs.
 *
 * Multiple keys can be mapped to one LED and one key can be mapped to multiple
 * LEDs. For example, if the keyboard is divided into zones, multiple keys point
 * to the same LED(s). Also, typically larger keys (e.g. space key) should be
 * allocated multiple LEDs.
 *
 * This mapping data (rgbkbd_map[]) is encoded as follows:
 *
 *   FF 01 FF 02 03 FF ... FF xx FF
 *
 * where LED IDs are in groups, separated by a delimiter (0xff). There are
 * supposed to be 127 groups, placed in the order of key ID. The first group
 * (KEY0) is always empty. The above example is interpreted as:
 *
 *   KEY0 = {}, KEY1 = {LED1}, KEY2 = {LED2, LED3}, ..., KEY127 = {LEDxx}
 *
 * Note the size of rgbkbd_map varies because one key can have multiple LEDs.
 *
 * At run time, a translation table is created. This table is an array of the
 * rgbkbd_map indexes and the table itself is indexed by the key IDs. This
 * allows a KBMCU to quickly look up LEDs for a given key ID as follows:
 *
 *   rgbkbd_map[rgbkbd_table[0]] = FF
 *   rgbkbd_map[rgbkbd_table[1]] = 01 (followed by FF)
 *   rgbkbd_map[rgbkbd_table[2]] = 02 (followed by 03 FF)
 *                   ...
 */
extern const uint8_t rgbkbd_map[];
extern const size_t rgbkbd_map_size;

/*
 * Driver for keyboard_backlight.
 */
extern const struct kblight_drv kblight_rgbkbd;

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_RGB_KEYBOARD_H */