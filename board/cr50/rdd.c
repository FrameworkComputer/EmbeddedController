/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "case_closed_debug.h"
#include "console.h"
#include "device_state.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "rbox.h"
#include "rdd.h"
#include "registers.h"
#include "system.h"
#include "uartn.h"
#include "usb_api.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

static int ec_uart_enabled, enable_usb_wakeup;
static int usb_is_initialized;

struct uart_config {
	const char *name;
	enum device_type device;
	int tx_signal;
};

static struct uart_config uarts[] = {
	[UART_AP] = {"AP", DEVICE_AP, GC_PINMUX_UART1_TX_SEL},
	[UART_EC] = {"EC", DEVICE_EC, GC_PINMUX_UART2_TX_SEL},
};

static int ccd_is_enabled(void)
{
	return ccd_get_mode() == CCD_MODE_ENABLED;
}

int is_utmi_wakeup_allowed(void)
{
	return enable_usb_wakeup;
}


/* If the UART TX is enabled the pinmux select will have a non-zero value */
int uartn_enabled(int uart)
{
	if (uart == UART_AP)
		return GREAD(PINMUX, DIOA7_SEL);
	return GREAD(PINMUX, DIOB5_SEL);
}

/* Connect the UART pin to the given signal */
static void uart_select_tx(int uart, int signal)
{
	if (uart == UART_AP)
		GWRITE(PINMUX, DIOA7_SEL, signal);
	else
		GWRITE(PINMUX, DIOB5_SEL, signal);
}

static int servo_is_connected(void)
{
	return device_get_state(DEVICE_SERVO) == DEVICE_STATE_ON;
}

void uartn_tx_connect(int uart)
{
	if (uart == UART_EC && !ec_uart_enabled)
		return;

	if (!ccd_is_enabled())
		return;

	if (servo_is_connected()) {
		CPRINTS("Servo is attached cannot enable %s UART",
			uarts[uart].name);
		return;
	}

	if (device_get_state(uarts[uart].device) == DEVICE_STATE_ON)
		uart_select_tx(uart, uarts[uart].tx_signal);
	else if (!uartn_enabled(uart))
		CPRINTS("%s is powered off", uarts[uart].name);
}

void uartn_tx_disconnect(int uart)
{
	/* If servo is connected disable UART */
	if (servo_is_connected())
		ec_uart_enabled = 0;

	/* Disconnect the TX pin from UART peripheral */
	uart_select_tx(uart, 0);
}

void ina_connect(void)
{
	/* Apply power to INA chips */
	gpio_set_level(GPIO_EN_PP3300_INA_L, 0);
	/* Allow enough time for power rail to come up */
	usleep(25);

	/*
	 * Connect B0/B1 pads to I2C0 input SDA/SCL. Note, that the inputs
	 * for these pads are already enabled for the gpio signals I2C_SCL_INA
	 * and I2C_SDA_INA in gpio.inc.
	 */
	GWRITE(PINMUX, I2C0_SDA_SEL, GC_PINMUX_DIOB1_SEL);
	GWRITE(PINMUX, I2C0_SCL_SEL, GC_PINMUX_DIOB0_SEL);

	/* Connect I2CS SDA/SCL output to B1/B0 pads */
	GWRITE(PINMUX, DIOB1_SEL, GC_PINMUX_I2C0_SDA_SEL);
	GWRITE(PINMUX, DIOB0_SEL, GC_PINMUX_I2C0_SCL_SEL);

	/*
	 * Initialize the i2cm module after the INAs are powered and the signal
	 * lines are connected.
	 */
	i2cm_init();
}

void ina_disconnect(void)
{
	/* Disonnect I2C0 SDA/SCL output to B1/B0 pads */
	GWRITE(PINMUX, DIOB1_SEL, 0);
	GWRITE(PINMUX, DIOB0_SEL, 0);
	/* Disconnect B1/B0 pads to I2C0 input SDA/SCL */
	GWRITE(PINMUX, I2C0_SDA_SEL, 0);
	GWRITE(PINMUX, I2C0_SCL_SEL, 0);

	/* Disable power to INA chips */
	gpio_set_level(GPIO_EN_PP3300_INA_L, 1);
}

void rdd_attached(void)
{
	/* Indicate case-closed debug mode (active low) */
	gpio_set_level(GPIO_CCD_MODE_L, 0);

	/* Enable CCD */
	ccd_set_mode(CCD_MODE_ENABLED);

	enable_usb_wakeup = 1;

	uartn_tx_connect(UART_AP);
}

void rdd_detached(void)
{
	/* Disconnect from AP and EC UART TX peripheral from gpios */
	uartn_tx_disconnect(UART_EC);
	uartn_tx_disconnect(UART_AP);

	/* Disable the AP and EC UART peripheral */
	uartn_disable(UART_AP);
	uartn_disable(UART_EC);

	/* Done with case-closed debug mode */
	gpio_set_level(GPIO_CCD_MODE_L, 1);

	enable_usb_wakeup = 0;
	ec_uart_enabled = 0;

	/* Disable CCD */
	ccd_set_mode(CCD_MODE_DISABLED);
}

void ccd_phy_init(int enable_ccd)
{
	uint32_t properties = system_get_board_properties();
	/*
	 * For boards that have one phy connected to the AP and one to the
	 * external port PHY0 is for the AP and PHY1 is for CCD.
	 */
	uint32_t which_phy = enable_ccd ? USB_SEL_PHY1 : USB_SEL_PHY0;

	/*
	 * TODO: if both PHYs are connected to the external port select the
	 * PHY based on the detected polarity
	 */
	usb_select_phy(which_phy);

	/*
	 * If the usb is going to be initialized on the AP PHY, but the AP is
	 * off, wait until HOOK_CHIPSET_RESUME to initialize usb.
	 */
	if (!enable_ccd && device_get_state(DEVICE_AP) != DEVICE_STATE_ON) {
		usb_is_initialized = 0;
		return;
	}

	/*
	 * If the board has the non-ccd phy connected to the AP initialize the
	 * phy no matter what. Otherwise only initialized the phy if ccd is
	 * enabled.
	 */
	if ((properties & BOARD_USB_AP) || enable_ccd) {
		usb_init();
		usb_is_initialized = 1;
	}
}

void disable_ap_usb(void)
{
	if ((system_get_board_properties() & BOARD_USB_AP) &&
	    !ccd_is_enabled() && usb_is_initialized) {
		usb_release();
		usb_is_initialized = 0;
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, disable_ap_usb, HOOK_PRIO_DEFAULT);

void enable_ap_usb(void)
{
	if ((system_get_board_properties() & BOARD_USB_AP) &&
	    !ccd_is_enabled() && !usb_is_initialized) {
		usb_is_initialized = 1;
		usb_init();
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, enable_ap_usb, HOOK_PRIO_DEFAULT);

static int command_ccd(int argc, char **argv)
{
	int val;

	if (argc > 1) {
		if (!strcasecmp("uart", argv[1]) && argc > 2) {
			if (!parse_bool(argv[2], &val))
				return EC_ERROR_PARAM2;

			if (val) {
				ec_uart_enabled = 1;
				uartn_tx_connect(UART_EC);
			} else {
				ec_uart_enabled = 0;
				uartn_tx_disconnect(UART_EC);
			}
		} else if (!strcasecmp("ina", argv[1]) && argc > 2) {
			if (!parse_bool(argv[2], &val))
				return EC_ERROR_PARAM2;

			if (val) {
				ina_connect();
				ccprintf("CCD: INAs enabled\n");
			} else {
				ina_disconnect();
				ccprintf("CCD: INAs disabled\n");
			}
		} else if (argc == 2) {
			if (!parse_bool(argv[1], &val))
				return EC_ERROR_PARAM1;

			if (val)
				rdd_attached();
			else
				rdd_detached();
		} else
			return EC_ERROR_PARAM1;
	}

	ccprintf("CCD:     %s\nAP UART: %s\nEC UART: %s\n",
		ccd_is_enabled() ? " enabled" : "disabled",
		uartn_enabled(UART_AP) ? " enabled" : "disabled",
		uartn_enabled(UART_EC) ? " enabled" : "disabled");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ccd, command_ccd,
			"[uart|ina] [<BOOLEAN>]",
			"Get/set the case closed debug state");

static int command_sys_rst(int argc, char **argv)
{
	int val;

	if (argc > 1) {
		if (!strcasecmp("pulse", argv[1])) {
			ccprintf("Pulsing AP reset\n");
			assert_sys_rst();
			usleep(200);
			deassert_sys_rst();
		} else if (parse_bool(argv[1], &val)) {
			if (val)
				assert_sys_rst();
			else
				deassert_sys_rst();
		} else
			return EC_ERROR_PARAM1;
	}

	ccprintf("SYS_RST_L is %s\n", is_sys_rst_asserted() ?
		 "asserted" : "deasserted");

	return EC_SUCCESS;

}
DECLARE_SAFE_CONSOLE_COMMAND(sysrst, command_sys_rst,
	"[pulse | <BOOLEAN>]",
	"Assert/deassert SYS_RST_L to reset the AP");

static int command_ec_rst(int argc, char **argv)
{
	int val;

	if (argc > 1) {
		if (!strcasecmp("pulse", argv[1])) {
			ccprintf("Pulsing EC reset\n");
			assert_ec_rst();
			usleep(200);
			deassert_ec_rst();
		} else if (parse_bool(argv[1], &val)) {
			if (val)
				assert_ec_rst();
			else
				deassert_ec_rst();
		} else
			return EC_ERROR_PARAM1;
	}

	ccprintf("EC_RST_L is %s\n", is_ec_rst_asserted() ?
		 "asserted" : "deasserted");

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(ecrst, command_ec_rst,
	"[pulse | <BOOLEAN>]",
	"Assert/deassert EC_RST_L to reset the EC (and AP)");

static int command_powerbtn(int argc, char **argv)
{
	char *e;
	int ms = 200;

	if (argc > 1) {
		if (!strcasecmp("pulse", argv[1])) {
			if (argc == 3) {
				ms = strtoi(argv[2], &e, 0);
				if (*e)
					return EC_ERROR_PARAM2;
			}

			ccprintf("Force %dms power button press\n", ms);

			rbox_powerbtn_press();
			msleep(ms);
			rbox_powerbtn_release();
		} else if (!strcasecmp("press", argv[1])) {
			rbox_powerbtn_press();
		} else if (!strcasecmp("release", argv[1])) {
			rbox_powerbtn_release();
		} else
			return EC_ERROR_PARAM1;
	}

	ccprintf("powerbtn: %s\n",
		 rbox_powerbtn_override_is_enabled() ? "forced press" :
		 rbox_powerbtn_is_pressed() ? "pressed\n" : "released\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(powerbtn, command_powerbtn,
			"[pulse [ms] | press | release]",
			"get/set the state of the power button");


