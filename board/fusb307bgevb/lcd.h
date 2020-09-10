/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * LCD driver for I2C LCD 2004.
 */

#ifndef __CROS_EC_LCD_H
#define __CROS_EC_LCD_H

#include "common.h"

/* commands */
#define LCD_CLEAR_DISPLAY	BIT(0)
#define LCD_RETURN_HOME		BIT(1)
#define LCD_ENTRYMODE_SET	BIT(2)
#define LCD_DISPLAY_CONTROL	BIT(3)
#define LCD_CURSOR_SHIFT	BIT(4)
#define LCD_FUNCTION_SET	BIT(5)
#define LCD_SET_CGRAMADDR	BIT(6)
#define LCD_SET_DDRAMADDR	BIT(7)

/* flags for display entry mode */
#define LCD_ENTRY_RIGHT			0x00
#define LCD_ENTRY_LEFT			BIT(1)
#define LCD_ENTRY_SHIFT_INCREMENT	BIT(0)
#define LCD_ENTRY_SHIFT_DECREMENT	0x00

/* flags for display on/off control */
#define LCD_DISPLAY_ON	BIT(2)
#define LCD_DISPLAY_OFF	0x00
#define LCD_CURSOR_ON	BIT(1)
#define LCD_CURSOR_OFF	0x00
#define LCD_BLINK_ON	BIT(0)
#define LCD_BLINK_OFF	0x00

/* flags for display/cursor shift */
#define LCD_DISPLAY_MOVE	BIT(3)
#define LCD_CURSOR_MOVE		0x00
#define LCD_MOVE_RIGHT		BIT(2)
#define LCD_MOVE_LEFT		0x00

/* flags for function set */
#define LCD_8BITMODE	BIT(4)
#define LCD_4BITMODE	0x00
#define LCD_2LINE	BIT(3)
#define LCD_1LINE	0x00
#define LCD_5X10DOTS	BIT(2)
#define LCD_5X8DOTS	0x00

/* flags for backlight control */
#define LCD_BACKLIGHT		BIT(3)
#define LCD_NO_BACKLIGHT	0x00

#define LCD_EN	BIT(2) /* Enable bit */
#define LCD_RW	BIT(1) /* Read/Write bit */
#define LCD_RS	BIT(0) /* Register select bit */

void lcd_init(uint8_t cols, uint8_t rows, uint8_t dotsize);
void lcd_set_cursor(uint8_t col, uint8_t row);
void lcd_set_char(char data);
void lcd_print_string(const char *str);
void lcd_clear(void);
void lcd_enable_display(void);
void lcd_disable_display(void);
void lcd_enable_backlight(void);
void lcd_disable_backlight(void);

#endif /*__CROS_EC_LCD_H */
