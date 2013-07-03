/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI TSU6721 USB port switch driver.
 */

#include "board.h"
#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "system.h"
#include "timer.h"
#include "tsu6721.h"
#include "uart.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_USBCHARGE, outstr)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/* 8-bit I2C address */
#define TSU6721_I2C_ADDR (0x25 << 1)

/* Delay values */
#define TSU6721_SW_RESET_DELAY 15

/* Number of retries when reset fails */
#define TSU6721_SW_RESET_RETRY 3

static int saved_interrupts;

uint8_t tsu6721_read(uint8_t reg)
{
	int res;
	int val;

	res = i2c_read8(I2C_PORT_HOST, TSU6721_I2C_ADDR, reg, &val);
	if (res)
		return 0xee;

	return val;
}

int tsu6721_write(uint8_t reg, uint8_t val)
{
	int res;

	res = i2c_write8(I2C_PORT_HOST, TSU6721_I2C_ADDR, reg, val);
	if (res)
		CPRINTF("[%T TSU6721 I2C write failed]\n");
	return res;
}

int tsu6721_enable_interrupts(void)
{
	int ctrl = tsu6721_read(TSU6721_REG_CONTROL);
	return tsu6721_write(TSU6721_REG_CONTROL, ctrl & 0x1e);
}

int  tsu6721_disable_interrupts(void)
{
	int ctrl = tsu6721_read(TSU6721_REG_CONTROL);
	int rv;

	rv = tsu6721_write(TSU6721_REG_CONTROL, ctrl | 0x1);
	tsu6721_get_interrupts();
	return rv;
}

int  tsu6721_set_interrupt_mask(uint16_t mask)
{
	return tsu6721_write(TSU6721_REG_INT_MASK1, (~mask) & 0xff) |
	       tsu6721_write(TSU6721_REG_INT_MASK2, ((~mask) >> 8) & 0xff);
}

int tsu6721_get_interrupts(void)
{
	int ret = tsu6721_peek_interrupts();
	saved_interrupts = 0;
	return ret;
}

int tsu6721_peek_interrupts(void)
{
	saved_interrupts |= (tsu6721_read(TSU6721_REG_INT2) << 8) |
			    (tsu6721_read(TSU6721_REG_INT1));
	return saved_interrupts;
}

int tsu6721_get_device_type(void)
{
	return (tsu6721_read(TSU6721_REG_DEV_TYPE3) << 16) |
	       (tsu6721_read(TSU6721_REG_DEV_TYPE2) << 8) |
	       (tsu6721_read(TSU6721_REG_DEV_TYPE1));
}

void tsu6721_reset(void)
{
	int i;

	for (i = 0; i < TSU6721_SW_RESET_RETRY; ++i) {
		if (i != 0) {
			CPRINTF("[%T TSU6721 init failed. Retrying.]\n");
			msleep(500);
		}
		if (tsu6721_write(TSU6721_REG_RESET, 0x1))
			continue;
		/* TSU6721 reset takes ~10ms. Let's wait for 15ms to be safe. */
		msleep(TSU6721_SW_RESET_DELAY);
		if (tsu6721_init() == EC_SUCCESS)
			break;
	}
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
	if (sel != TSU6721_MUX_AUTO && (id == 0x1f) && !vbus1 && !vbus3) {
		CPRINTF("TSU6721 cannot use manual mode: no VBUS or ID\n");
		return EC_ERROR_INVAL;
	}

	if (sel == TSU6721_MUX_AUTO) {
		tsu6721_write(TSU6721_REG_CONTROL, ctrl | TSU6721_CTRL_AUTO);
	} else {
		tsu6721_write(TSU6721_REG_MANUAL1, sel);
		tsu6721_write(TSU6721_REG_CONTROL, ctrl & ~TSU6721_CTRL_AUTO);
	}

	return EC_SUCCESS;
}

int tsu6721_init(void)
{
	uint8_t settings;
	uint8_t dev_id = tsu6721_read(TSU6721_REG_DEV_ID);
	int res = 0;

	if ((dev_id != 0x0a) && (dev_id != 0x12)) {
		CPRINTF("TSU6721 invalid device ID 0x%02x\n", dev_id);
		return EC_ERROR_UNKNOWN;
	}

	/* set USB charger detection timeout to 600ms */
	settings = tsu6721_read(TSU6721_REG_TIMER);
	if (settings == 0xee)
		return EC_ERROR_UNKNOWN;
	settings = (settings & ~0x38);
	res |= tsu6721_write(TSU6721_REG_TIMER, settings);

	res |= tsu6721_set_interrupt_mask(TSU6721_INT_ATTACH |
					  TSU6721_INT_DETACH |
					  TSU6721_INT_ADC_CHANGE |
					  TSU6721_INT_VBUS);
	res |= tsu6721_enable_interrupts();

	return res ? EC_ERROR_UNKNOWN : EC_SUCCESS;
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

/*****************************************************************************/
/* Console commands */

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
			tsu6721_mux(TSU6721_MUX_AUTO);
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

/*****************************************************************************/
/* Host commands */

static int usb_command_mux(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_mux *p = args->params;

	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	/* Safety check */
	if (p->mux != TSU6721_MUX_AUTO &&
	    p->mux != TSU6721_MUX_USB &&
	    p->mux != TSU6721_MUX_UART &&
	    p->mux != TSU6721_MUX_AUDIO)
		return EC_RES_ERROR;

	if (tsu6721_mux(p->mux))
		return EC_RES_ERROR;
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_MUX, usb_command_mux, EC_VER_MASK(0));
