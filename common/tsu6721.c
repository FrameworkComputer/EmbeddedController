/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI TSU6721 USB port switch driver.
 */

#include "board.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "timer.h"
#include "tsu6721.h"
#include "uart.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_USBCHARGE, outstr)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/* 8-bit I2C address */
#define TSU6721_I2C_ADDR (0x25 << 1)

uint8_t tsu6721_read(uint8_t reg)
{
	int res;
	int val;

	res = i2c_read8(I2C_PORT_HOST, TSU6721_I2C_ADDR, reg, &val);
	if (res)
		return 0xee;

	return val;
}

void tsu6721_write(uint8_t reg, uint8_t val)
{
	int res;

	res = i2c_write8(I2C_PORT_HOST, TSU6721_I2C_ADDR, reg, val);
	if (res)
		CPRINTF("[%T TSU6721 I2C write failed]\n");
}

void tsu6721_enable_interrupts(int mask)
{
	int ctrl = tsu6721_read(TSU6721_REG_CONTROL);
	tsu6721_write(TSU6721_REG_INT_MASK1, (~mask) & 0xff);
	tsu6721_write(TSU6721_REG_INT_MASK2, ((~mask) >> 8) & 0xff);
	tsu6721_write(TSU6721_REG_CONTROL, ctrl & 0x1e);
}

void tsu6721_disable_interrupt(void)
{
	int ctrl = tsu6721_read(TSU6721_REG_CONTROL);
	tsu6721_write(TSU6721_REG_CONTROL, ctrl | 0x1);
}

int tsu6721_get_interrupts(void)
{
	return (tsu6721_read(TSU6721_REG_INT1) << 8) |
	       (tsu6721_read(TSU6721_REG_INT2));
}

int tsu6721_get_device_type(void)
{
	return (tsu6721_read(TSU6721_REG_DEV_TYPE3) << 16) |
	       (tsu6721_read(TSU6721_REG_DEV_TYPE2) << 8) |
	       (tsu6721_read(TSU6721_REG_DEV_TYPE1));
}

int tsu6721_mux(enum tsu6721_mux sel)
{
	uint8_t id = tsu6721_read(TSU6721_REG_ADC);
	uint8_t vbus1 = tsu6721_read(TSU6721_REG_DEV_TYPE1) & 0x74;
	uint8_t vbus3 = tsu6721_read(TSU6721_REG_DEV_TYPE3) & 0x74;
	uint8_t ctrl = tsu6721_read(TSU6721_REG_CONTROL);

	/*
	 * silicon limitation: the chip stays in low power mode and cannot
	 * activate manual mode if it is not detecting either a VBUS or
	 * something known on the ID pin
	 */
	if ((id == 0x1f) && !vbus1 && !vbus3) {
		CPRINTF("TSU6721 cannot use manual mode: no VBUS or ID\n");
		return EC_ERROR_INVAL;
	}

	if (sel == TSU6721_MUX_NONE) {
		tsu6721_write(TSU6721_REG_CONTROL, ctrl | TSU6721_CTRL_AUTO);
	} else {
		tsu6721_write(TSU6721_REG_MANUAL1, sel);
		tsu6721_write(TSU6721_REG_CONTROL, ctrl & ~TSU6721_CTRL_AUTO);
	}

	return EC_SUCCESS;
}

void tsu6721_init(void)
{
	uint8_t settings;
	uint8_t dev_id = tsu6721_read(TSU6721_REG_DEV_ID);

	if (dev_id != 0x0a) {
		CPRINTF("TSU6721 invalid device ID 0x%02x\n", dev_id);
		return;
	}

	/* set USB charger detection timeout to 600ms */
	settings = tsu6721_read(TSU6721_REG_TIMER);
	settings = (settings & ~0x38);
	tsu6721_write(TSU6721_REG_TIMER, settings);

	tsu6721_enable_interrupts(TSU6721_INT_ATTACH |
				  TSU6721_INT_DETACH |
				  TSU6721_INT_ADC_CHANGE |
				  TSU6721_INT_VBUS);
}
/*
 * TODO(vpalatin): using the I2C early in the HOOK_INIT
 * currently triggers all sort of badness, I need to debug
 * this before re-activatin this initialization.
 */
#if 0
DECLARE_HOOK(HOOK_INIT, tsu6721_init, HOOK_PRIO_DEFAULT);
#endif

static void tsu6721_dump(void)
{
	int i;
	uint8_t id = tsu6721_read(TSU6721_REG_ADC);
	uint8_t ctrl = tsu6721_read(TSU6721_REG_CONTROL);

	if (ctrl & TSU6721_CTRL_AUTO)
		ccprintf("Auto: %02x %02x %02x\n",
			tsu6721_read(TSU6721_REG_DEV_TYPE1),
			tsu6721_read(TSU6721_REG_DEV_TYPE2),
			tsu6721_read(TSU6721_REG_DEV_TYPE3));
	else
		ccprintf("Manual: %02x %02x\n",
			tsu6721_read(TSU6721_REG_MANUAL1),
			tsu6721_read(TSU6721_REG_MANUAL2));
	ccprintf("ID: 0x%02x\n", id);
	for (i = 1; i < 0x24; i++)
		ccprintf("%02x ", tsu6721_read(i));
	ccprintf("\n");
}

static int command_usbmux(int argc, char **argv)
{
	if (1 == argc) { /* dump all registers */
		tsu6721_dump();
		return EC_SUCCESS;
	} else if (2 == argc) {
		if (!strcasecmp(argv[1], "usb")) {
			tsu6721_mux(TSU6721_MUX_USB);
		} else if (!strcasecmp(argv[1], "uart1")) {
			tsu6721_mux(TSU6721_MUX_UART);
		} else if (!strcasecmp(argv[1], "uart2")) {
			tsu6721_mux(TSU6721_MUX_AUDIO);
		} else if (!strcasecmp(argv[1], "auto")) {
			tsu6721_mux(TSU6721_MUX_NONE);
		} else { /* read one register */
			ccprintf("Invalid mux value: %s\n", argv[1]);
			return EC_ERROR_INVAL;
		}
		return EC_SUCCESS;
	}

	return EC_ERROR_INVAL;
}
DECLARE_CONSOLE_COMMAND(usbmux, command_usbmux,
			"[usb|uart1|uart2|auto]",
			"TSU6721 USB mux control",
			NULL);
