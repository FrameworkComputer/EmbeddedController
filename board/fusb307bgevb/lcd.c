/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * LCD driver for I2C LCD 2004.
 */

#include "i2c.h"
#include "lcd.h"
#include "timer.h"

struct lcd_state_info {
	uint8_t addr;
	uint8_t displayfunction;
	uint8_t displaycontrol;
	uint8_t backlightval;
};

static struct lcd_state_info state = {
	.addr = LCD_SLAVE_ADDR,
	.backlightval = LCD_BACKLIGHT,
	.displayfunction = LCD_4BITMODE | LCD_1LINE | LCD_5X8DOTS,
};

/************ low level data pushing commands **********/
/* write either command or data */
static void expander_write(uint8_t data)
{
	i2c_write8(I2C_PORT_TCPC, LCD_SLAVE_ADDR, 0x00,
		   data | state.backlightval);
}

static void pulse_enable(uint8_t data)
{
	expander_write(data | LCD_EN); /* En high */
	crec_usleep(1); /* enable pulse must be >450ns */

	expander_write(data & ~LCD_EN); /* En low */
	crec_usleep(50); /* commands need > 37us to settle */
}

static void write_4bits(uint8_t value)
{
	expander_write(value);
	pulse_enable(value);
}

static void send(uint8_t value, uint8_t mode)
{
	uint8_t highnib = value & 0xf0;
	uint8_t lownib = (value << 4) & 0xf0;

	write_4bits(highnib | mode);
	write_4bits(lownib | mode);
}

/*********** mid level commands, for sending data/cmds */
static void command(uint8_t value)
{
	send(value, 0);
}

/********** high level commands, for the user! */
void lcd_clear(void)
{
	command(LCD_CLEAR_DISPLAY); /* clear display, set cursor to zero */
	crec_usleep(2000); /* this command takes a long time! */
}

void lcd_set_cursor(uint8_t col, uint8_t row)
{
	int row_offsets[] = { 0x00, 0x40, 0x14, 0x54 };

	command(LCD_SET_DDRAMADDR | (col + row_offsets[row]));
}

void lcd_print_char(char data)
{
	send(data, LCD_RS);
}

void lcd_print_string(const char *str)
{
	while (*str)
		lcd_print_char(*str++);
}

/* Turn the display on/off (quickly) */
void lcd_disable_display(void)
{
	state.displaycontrol &= ~LCD_DISPLAY_ON;
	command(LCD_DISPLAY_CONTROL | state.displaycontrol);
}
void lcd_enable_display(void)
{
	state.displaycontrol |= LCD_DISPLAY_ON;
	command(LCD_DISPLAY_CONTROL | state.displaycontrol);
}

/* Turn the (optional) backlight off/on */
void lcd_disable_backlight(void)
{
	state.backlightval = LCD_NO_BACKLIGHT;
	expander_write(0);
}

void lcd_enable_backlight(void)
{
	state.backlightval = LCD_BACKLIGHT;
	expander_write(0);
}

void lcd_init(uint8_t cols, uint8_t rows, uint8_t dotsize)
{
	if (rows > 1)
		state.displayfunction |= LCD_2LINE;

	/* for some 1 line displays you can select a 10 pixel high font */
	if ((dotsize != 0) && (rows == 1))
		state.displayfunction |= LCD_5X10DOTS;

	/* SEE PAGE 45/46 FOR INITIALIZATION SPECIFICATION!
	 * according to datasheet, we need at least 40ms after power rises
	 * above 2.7V before sending commands. Arduino can turn on way
	 * before 4.5V so we'll wait 50
	 */
	crec_usleep(50);

	/* Now we pull both RS and R/W low to begin commands */
	/* reset expanderand turn backlight off (Bit 8 =1) */
	expander_write(state.backlightval);
	crec_usleep(1000);

	/* put the LCD into 4 bit mode
	 * this is according to the hitachi HD44780 datasheet
	 * figure 24, pg 46
	 * we start in 8bit mode, try to set 4 bit mode
	 */
	write_4bits(0x03 << 4);
	crec_usleep(4500); /* wait min 4.1ms */
	/*second try */
	write_4bits(0x03 << 4);
	crec_usleep(4500); /* wait min 4.1ms */
	/* third go! */
	write_4bits(0x03 << 4);
	crec_usleep(150);
	/* finally, set to 4-bit interface */
	write_4bits(0x02 << 4);

	/* set # lines, font size, etc. */
	command(LCD_FUNCTION_SET | state.displayfunction);

	/* turn the display on with no cursor or blinking default */
	state.displaycontrol = LCD_DISPLAY_ON | LCD_CURSOR_OFF | LCD_BLINK_OFF;
	lcd_enable_display();

	/* clear it off */
	lcd_clear();

	/* Initialize to default text direction (for roman languages)
	 * and set the entry mode
	 */
	command(LCD_ENTRYMODE_SET | LCD_ENTRY_LEFT | LCD_ENTRY_SHIFT_DECREMENT);

	lcd_set_cursor(0, 0);
}
