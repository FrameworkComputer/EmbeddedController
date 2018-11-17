/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "case_closed_debug.h"  /* For ccd_ext_is_enabled() */
#include "ccd_config.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "rbox.h"
#include "rdd.h"
#include "registers.h"
#include "system.h"
#include "uart_bitbang.h"
#include "uartn.h"
#include "usb_api.h"
#include "usb_console.h"
#include "usb_i2c.h"
#include "usb_spi.h"

/* Include the dazzlingly complex macro to instantiate the USB SPI config */
USB_SPI_CONFIG(ccd_usb_spi, USB_IFACE_SPI, USB_EP_SPI);

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USB, format, ## args)

static enum device_state state = DEVICE_STATE_INIT;

/* Flags for CCD blocking */
enum ccd_block_flags {
	/*
	 * UARTs.  Disabling these can be helpful if the AP or EC is doing
	 * something which creates an interrupt storm on these ports.
	 */
	CCD_BLOCK_AP_UART = BIT(0),
	CCD_BLOCK_EC_UART = BIT(1),

	/*
	 * Any ports shared with servo.  Disabling these will stop CCD from
	 * interfering with servo, in the case where both CCD and servo is
	 * connected but servo isn't properly detected.
	 */
	CCD_BLOCK_SERVO_SHARED = BIT(2),

	/*
	 * In case of broken hardware use IGNORE_SERVO to bypass the "servo
	 * connected check". If cr50 thinks servo is connected, it won't enable
	 * the AP or EC uart. Using IGNORE_SERVO will force cr50 to enable uart
	 * even if it thinks servo is connected.
	 *
	 * ONLY USE THIS IF SERVO IS DISCONNECTED. If you force enable AP and EC
	 * uart while servo is connected, it could break the hardware and the
	 * ccd uart could become permanently unusable.
	 */
	CCD_BLOCK_IGNORE_SERVO = BIT(3)
};

/* Which UARTs are blocked by console command */
static uint8_t ccd_block;

int ccd_ext_is_enabled(void)
{
	return state == DEVICE_STATE_CONNECTED;
}

/* If the UART TX is connected the pinmux select will have a non-zero value */
int uart_tx_is_connected(int uart)
{
	if (uart == UART_AP)
		return GREAD(PINMUX, DIOA7_SEL);

	/*
	 * Enabling bit bang programming mode disconnected the EC UART from
	 * the external pin, but muxed DIOB5 to a different GPIO bit.
	 */
	return !uart_bitbang_is_enabled() && GREAD(PINMUX, DIOB5_SEL);
}

/**
 * Connect the UART pin to the given signal
 *
 * @param uart		the uart peripheral number
 * @param signal	the pinmux selector value for the gpio or peripheral
 *			function. 0 to disable the output.
 */
static void uart_select_tx(int uart, int signal)
{
	if (uart == UART_AP) {
		GWRITE(PINMUX, DIOA7_SEL, signal);
	} else {
		GWRITE(PINMUX, DIOB5_SEL, signal);

		/* Remove the pulldown when we are driving the signal */
		GWRITE_FIELD(PINMUX, DIOB5_CTL, PD, signal ? 0 : 1);
	}
}

void uartn_tx_connect(int uart)
{
	/*
	 * Don't drive TX unless the debug cable is connected (we have
	 * something to transmit) and servo is disconnected (we won't be
	 * drive-fighting with servo).
	 */
	if (servo_is_connected() || !ccd_ext_is_enabled())
		return;

	if (uart == UART_AP) {
		if (!ccd_is_cap_enabled(CCD_CAP_GSC_TX_AP_RX))
			return;

		if (!ap_uart_is_on())
			return;

		uart_select_tx(UART_AP, GC_PINMUX_UART1_TX_SEL);
	} else {
		if (!ccd_is_cap_enabled(CCD_CAP_GSC_TX_EC_RX))
			return;

		if (!ec_is_on())
			return;

		uart_select_tx(UART_EC, GC_PINMUX_UART2_TX_SEL);
	}
}

void uartn_tx_disconnect(int uart)
{
	/* Disconnect the TX pin from UART peripheral */
	uart_select_tx(uart, 0);
}

/*
 * Flags for the current CCD device state.  This is used for determining what
 * hardware devices we've enabled now, and which we want enabled.
 */
enum ccd_state_flag {
	/* Flags for individual devices/ports */

	/* AP UART is enabled.  RX-only, unless TX is also enabled. */
	CCD_ENABLE_UART_AP		= BIT(0),

	/* AP UART transmit is enabled.  Requires AP UART enabled. */
	CCD_ENABLE_UART_AP_TX		= BIT(1),

	/* EC UART is enabled.  RX-only, unless TX is also enabled. */
	CCD_ENABLE_UART_EC		= BIT(2),

	/* EC UART transmit is enabled.  Requires EC UART enabled. */
	CCD_ENABLE_UART_EC_TX		= BIT(3),

	/*
	 * EC UART bit-banging is enabled.  Requires EC UART enabled, and
	 * blocks EC UART transmit.
	 */
	CCD_ENABLE_UART_EC_BITBANG	= BIT(4),

	/* I2C port is enabled */
	CCD_ENABLE_I2C			= BIT(5),

	/* SPI port is enabled for AP and/or EC flash */
	CCD_ENABLE_SPI			= BIT(6),
};

int console_is_restricted(void)
{
	return !ccd_is_cap_enabled(CCD_CAP_GSC_RESTRICTED_CONSOLE);
}

/**
 * Return the currently enabled state flags (see enum ccd_state_flag).
 */
static uint32_t get_state_flags(void)
{
	uint32_t flags_now = 0;

	if (uartn_is_enabled(UART_AP))
		flags_now |= CCD_ENABLE_UART_AP;
	if (uart_tx_is_connected(UART_AP))
		flags_now |= CCD_ENABLE_UART_AP_TX;
	if (uartn_is_enabled(UART_EC))
		flags_now |= CCD_ENABLE_UART_EC;
	if (uart_tx_is_connected(UART_EC))
		flags_now |= CCD_ENABLE_UART_EC_TX;

#ifdef CONFIG_UART_BITBANG
	if (uart_bitbang_is_enabled())
		flags_now |= CCD_ENABLE_UART_EC_BITBANG;
#endif

	if (usb_i2c_board_is_enabled())
		flags_now |= CCD_ENABLE_I2C;

	if (ccd_usb_spi.state->enabled_device)
		flags_now |= CCD_ENABLE_SPI;

	return flags_now;
}

/**
 * Print the state flags to the specified output channel
 *
 * @param channel	Console channel
 * @param flags		Flags to print
 */
static void print_state_flags(enum console_channel channel, uint32_t flags)
{
	if (flags & CCD_ENABLE_UART_AP)
		cprintf(channel, " UARTAP");
	if (flags & CCD_ENABLE_UART_AP_TX)
		cprintf(channel, "+TX");
	if (flags & CCD_ENABLE_UART_EC)
		cprintf(channel, " UARTEC");
	if (flags & CCD_ENABLE_UART_EC_TX)
		cprintf(channel, "+TX");
	if (flags & CCD_ENABLE_UART_EC_BITBANG)
		cprintf(channel, "+BB");
	if (flags & CCD_ENABLE_I2C)
		cprintf(channel, " I2C");
	if (flags & CCD_ENABLE_SPI)
		cprintf(channel, " SPI");
}

static void ccd_state_change_hook(void)
{
	uint32_t flags_now;
	uint32_t flags_want = 0;
	uint32_t delta;

	/* Check what's enabled now */
	flags_now = get_state_flags();

	/* Start out by figuring what flags we might want enabled */

	/* Enable EC/AP UART RX if that device is on */
	if (ap_uart_is_on())
		flags_want |= CCD_ENABLE_UART_AP;
	if (ec_is_rx_allowed())
		flags_want |= CCD_ENABLE_UART_EC;

#ifdef CONFIG_UART_BITBANG
	if (uart_bitbang_is_wanted())
		flags_want |= CCD_ENABLE_UART_EC_BITBANG;
#endif

	/*
	 * External CCD will try to enable all the ports. If it's disabled,
	 * disable all ports.
	 */
	if (ccd_ext_is_enabled())
		flags_want |= (CCD_ENABLE_UART_AP_TX | CCD_ENABLE_UART_EC_TX |
			       CCD_ENABLE_I2C | CCD_ENABLE_SPI);
	else
		flags_want = 0;

	/* Then disable flags we can't have */

	/* Servo takes over UART TX, I2C, and SPI. */
	if (servo_is_connected() || (ccd_block & CCD_BLOCK_SERVO_SHARED))
		flags_want &= ~(CCD_ENABLE_UART_AP_TX | CCD_ENABLE_UART_EC_TX |
				CCD_ENABLE_UART_EC_BITBANG | CCD_ENABLE_I2C |
				CCD_ENABLE_SPI);

	/* Disable based on capabilities */
	if (!ccd_is_cap_enabled(CCD_CAP_GSC_RX_AP_TX))
		flags_want &= ~CCD_ENABLE_UART_AP;
	if (!ccd_is_cap_enabled(CCD_CAP_GSC_TX_AP_RX))
		flags_want &= ~CCD_ENABLE_UART_AP_TX;
	if (!ccd_is_cap_enabled(CCD_CAP_GSC_RX_EC_TX))
		flags_want &= ~CCD_ENABLE_UART_EC;
	if (!ccd_is_cap_enabled(CCD_CAP_GSC_TX_EC_RX))
		flags_want &= ~(CCD_ENABLE_UART_EC_TX |
				CCD_ENABLE_UART_EC_BITBANG);
	if (!ccd_is_cap_enabled(CCD_CAP_I2C))
		flags_want &= ~CCD_ENABLE_I2C;

	/*
	 * EC and AP flash block on a per-packet basis, but if we don't have
	 * access to either one, turn off SPI.
	 */
	if (!ccd_is_cap_enabled(CCD_CAP_AP_FLASH) &&
	    !ccd_is_cap_enabled(CCD_CAP_EC_FLASH))
		flags_want &= ~CCD_ENABLE_SPI;

	/* EC UART TX blocked by bit-banging */
	if (flags_want & CCD_ENABLE_UART_EC_BITBANG)
		flags_want &= ~CCD_ENABLE_UART_EC_TX;

	/* UARTs can be specifically blocked by console command */
	if (ccd_block & CCD_BLOCK_AP_UART)
		flags_want &= ~CCD_ENABLE_UART_AP;
	if (ccd_block & CCD_BLOCK_EC_UART)
		flags_want &= ~CCD_ENABLE_UART_EC;

	/* UARTs are either RX-only or RX+TX, so no RX implies no TX */
	if (!(flags_want & CCD_ENABLE_UART_AP))
		flags_want &= ~CCD_ENABLE_UART_AP_TX;
	if (!(flags_want & CCD_ENABLE_UART_EC))
		flags_want &= ~CCD_ENABLE_UART_EC_TX;

	/* If no change, we're done */
	if (flags_now == flags_want)
		return;

	CPRINTF("[%T CCD state:");
	print_state_flags(CC_USB, flags_want);
	CPRINTF("]\n");

	/* Handle turning things off */
	delta = flags_now & ~flags_want;
	if (delta & CCD_ENABLE_UART_AP)
		uartn_disable(UART_AP);
	if (delta & CCD_ENABLE_UART_AP_TX)
		uartn_tx_disconnect(UART_AP);
	if (delta & CCD_ENABLE_UART_EC)
		uartn_disable(UART_EC);
	if (delta & CCD_ENABLE_UART_EC_TX)
		uartn_tx_disconnect(UART_EC);
#ifdef CONFIG_UART_BITBANG
	if (delta & CCD_ENABLE_UART_EC_BITBANG)
		uart_bitbang_disable();
#endif
	if (delta & CCD_ENABLE_I2C)
		usb_i2c_board_disable();
	if (delta & CCD_ENABLE_SPI)
		usb_spi_enable(&ccd_usb_spi, 0);

	/* Handle turning things on */
	delta = flags_want & ~flags_now;
	if (delta & CCD_ENABLE_UART_AP)
		uartn_enable(UART_AP);
	if (delta & CCD_ENABLE_UART_AP_TX)
		uartn_tx_connect(UART_AP);
	if (delta & CCD_ENABLE_UART_EC)
		uartn_enable(UART_EC);
	if (delta & CCD_ENABLE_UART_EC_TX)
		uartn_tx_connect(UART_EC);
#ifdef CONFIG_UART_BITBANG
	if (delta & CCD_ENABLE_UART_EC_BITBANG) {
		/*
		 * Servo detect interrupt will be re-enabled by the
		 * servo_detect() poll once bit bang mode is disabled.
		 */
		gpio_disable_interrupt(GPIO_DETECT_SERVO);
		uart_bitbang_enable();
	}
#endif
	if (delta & CCD_ENABLE_I2C)
		usb_i2c_board_enable();
	if (delta & CCD_ENABLE_SPI)
		usb_spi_enable(&ccd_usb_spi, 1);
}
DECLARE_DEFERRED(ccd_state_change_hook);

void ccd_update_state(void)
{
	/*
	 * Use a deferred call to serialize changes from CCD config, RDD
	 * attach/detach, EC/AP startup or shutdown, etc.
	 */
	hook_call_deferred(&ccd_state_change_hook_data, 0);
}

/*****************************************************************************/

static void ccd_ext_detect(void)
{
	/* The CCD mode pin is active low. */
	int enable = !gpio_get_level(GPIO_CCD_MODE_L);

	if (enable == ccd_ext_is_enabled())
		return;

	if (enable) {
		/*
		 * If we're not disconnected, release USB to ensure it's in a
		 * good state before we usb_init().  This matches what
		 * common/case_closed_debug.c does.
		 *
		 * Not sure exactly why this is necessary.  It could be because
		 * that also has CCD_MODE_PARTIAL, and the only way to go
		 * cleanly between ENABLED and PARTIAL is to disable things and
		 * then re-enable only what's needed?
		 *
		 * TODO(rspangler): Figure out whether we can delete this.
		 */
		if (state != DEVICE_STATE_DISCONNECTED)
			usb_release();

		CPRINTS("CCD EXT enable");
		state = DEVICE_STATE_CONNECTED;

		usb_init();
		usb_console_enable(1, 0);
	} else {
		CPRINTS("CCD EXT disable");
		state = DEVICE_STATE_DISCONNECTED;

		usb_release();
		usb_console_enable(0, 0);
	}

	ccd_update_state();
}
DECLARE_HOOK(HOOK_SECOND, ccd_ext_detect, HOOK_PRIO_DEFAULT);

static void print_ccd_ports_blocked(void)
{
	/* Regardless, print current state */
	ccputs("CCD ports blocked:");
	if (ccd_block & CCD_BLOCK_AP_UART)
		ccputs(" AP");
	if (ccd_block & CCD_BLOCK_EC_UART)
		ccputs(" EC");
	if (ccd_block & CCD_BLOCK_SERVO_SHARED)
		ccputs(" SERVO");
	if (ccd_block & CCD_BLOCK_IGNORE_SERVO) {
		ccputs(" IGNORE_SERVO");
		ccputs("\nWARNING: enabling UART while servo is connected may "
		       "damage hardware");
	}
	if (!ccd_block)
		ccputs(" (none)");
	ccputs("\n");
}

static int command_ccd_state(int argc, char **argv)
{
	print_ap_state();
	print_ap_uart_state();
	print_ec_state();
	print_rdd_state();
	print_servo_state();

	ccprintf("CCD EXT: %s\n",
		 ccd_ext_is_enabled() ? "enabled" : "disabled");

	ccprintf("State flags:");
	print_state_flags(CC_COMMAND, get_state_flags());
	ccprintf("\n");

	print_ccd_ports_blocked();

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(ccdstate, command_ccd_state,
			     "",
			     "Print the case closed debug device state");

static int command_ccd_block(int argc, char **argv)
{
	uint8_t block_flag = 0;
	int new_state;

	if (argc == 3) {
		if (!strcasecmp(argv[1], "AP"))
			block_flag = CCD_BLOCK_AP_UART;
		else if (!strcasecmp(argv[1], "EC"))
			block_flag = CCD_BLOCK_EC_UART;
		else if (!strcasecmp(argv[1], "SERVO"))
			block_flag = CCD_BLOCK_SERVO_SHARED;
		else if (!strcasecmp(argv[1], "IGNORE_SERVO"))
			block_flag = CCD_BLOCK_IGNORE_SERVO;
		else
			return EC_ERROR_PARAM1;

		if (!parse_bool(argv[2], &new_state))
			return EC_ERROR_PARAM2;

		if (new_state)
			ccd_block |= block_flag;
		else
			ccd_block &= ~block_flag;

		if (block_flag == CCD_BLOCK_IGNORE_SERVO)
			servo_ignore(new_state);

		/* Update blocked state in deferred function */
		ccd_update_state();
	}

	print_ccd_ports_blocked();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ccdblock, command_ccd_block,
			"[<AP | EC | SERVO | IGNORE_SERVO> [BOOLEAN]]",
			"Force CCD ports disabled");
