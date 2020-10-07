/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* FUSB307BGEVB configuration */

#include "common.h"
#include "ec_version.h"
#include "fusb307.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "lcd.h"
#include "printf.h"
#include "queue_policies.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "usart-stm32f0.h"
#include "usart_tx_dma.h"
#include "usart_rx_dma.h"
#include "usb_gpio.h"
#include "usb-stream.h"
#include "usb_common.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static void tcpc_alert_event(enum gpio_signal signal)
{
	schedule_deferred_pd_interrupt(0);
}

/******************************************************************************
 * Handle button presses. Press BUTTON REFRESH to refresh pdos shown on lcd.
 * Press BUTTON DOWN to select pdo. Prss BUTTON ENTER to confirm selection.
 */
static int count;

static void button_enter_event_deferred(void)
{
	uint32_t ma, mv;

	CPRINTS("Button enter event");

	if (count >= 0 && count < pd_get_src_cap_cnt(0)) {
		pd_extract_pdo_power(pd_get_src_caps(0)[count], &ma, &mv);
		pd_request_source_voltage(0, mv);
	} else {
		CPRINTS("ERROR: button counter weird value.");
		return;
	}
}
DECLARE_DEFERRED(button_enter_event_deferred);

void button_enter_event(enum gpio_signal signal)
{
	hook_call_deferred(&button_enter_event_deferred_data, 100 * MSEC);
}

static void button_refresh_event_deferred(void)
{
	int i;
	uint32_t ma, mv;
	char c[20];

	CPRINTS("Button refresh event");
	count = 0;

	/* Display supply voltage on first page. */
	lcd_clear();
	for (i = 0; i < MIN(pd_get_src_cap_cnt(0), 4); i++) {
		pd_extract_pdo_power(pd_get_src_caps(0)[i], &ma, &mv);
		snprintf(c, ARRAY_SIZE(c), "[%d] %dmV %dmA", i, mv, ma);
		lcd_set_cursor(0, i);
		lcd_print_string(c);
	}

	/* Display selector */
	lcd_set_cursor(19, 0);
	lcd_print_string("V");
}
DECLARE_DEFERRED(button_refresh_event_deferred);

void button_refresh_event(enum gpio_signal signal)
{
	hook_call_deferred(&button_refresh_event_deferred_data, 100 * MSEC);
}

static void button_down_event_deferred(void)
{
	int i;
	uint32_t ma, mv;
	char c[20];

	CPRINTS("Button down event");
	if (pd_get_src_cap_cnt(0) > 0)
		count = (count + 1) % pd_get_src_cap_cnt(0);
	else {
		/* Hasn't plug in adaptor yet, do nothing. */
		return;
	}

	/* Display all supply voltage, count will never be greater than 7 */
	if (count == 0) {
		lcd_clear();
		for (i = 0; i < MIN(pd_get_src_cap_cnt(0), 4); i++) {
			pd_extract_pdo_power(pd_get_src_caps(0)[i], &ma, &mv);
			snprintf(c, ARRAY_SIZE(c), "[%d] %dmV %dmA", i, mv, ma);
			lcd_set_cursor(0, i);
			lcd_print_string(c);
		}
	}
	if (count == 4) {
		lcd_clear();
		for (i = 4; i < pd_get_src_cap_cnt(0); i++) {
			pd_extract_pdo_power(pd_get_src_caps(0)[i], &ma, &mv);
			snprintf(c, ARRAY_SIZE(c), "[%d] %dmV %dmA", i, mv, ma);
			lcd_set_cursor(0, i - 4);
			lcd_print_string(c);
		}
	}

	/* Clear last col on LCD */
	for (i = 0; i < 4; i++) {
		lcd_set_cursor(19, i);
		lcd_print_string(" ");
	}
	/* Display selector */
	lcd_set_cursor(19, count % 4);
	lcd_print_string("V");
}
DECLARE_DEFERRED(button_down_event_deferred);

void button_down_event(enum gpio_signal signal)
{
	hook_call_deferred(&button_down_event_deferred_data, 100 * MSEC);
}

/******************************************************************************
 * Build GPIO tables and expose a subset of the GPIOs over USB.
 */
#include "gpio_list.h"

static enum gpio_signal const usb_gpio_list[] = {
	GPIO_USER_BUTTON_ENTER,
	GPIO_USER_BUTTON_REFRESH,
	GPIO_USER_BUTTON_DOWN,
};

/*
 * This instantiates struct usb_gpio_config const usb_gpio, plus several other
 * variables, all named something beginning with usb_gpio_
 */
USB_GPIO_CONFIG(usb_gpio,
		usb_gpio_list,
		USB_IFACE_GPIO,
		USB_EP_GPIO);

/******************************************************************************
 * Setup USART1 as a loopback device, it just echo's back anything sent to it.
 */
static struct usart_config const loopback_usart;

static struct queue const loopback_queue =
	QUEUE_DIRECT(64, uint8_t,
		     loopback_usart.producer,
		     loopback_usart.consumer);

static struct usart_rx_dma const loopback_rx_dma =
	USART_RX_DMA(STM32_DMAC_CH3, 8);

static struct usart_tx_dma const loopback_tx_dma =
	USART_TX_DMA(STM32_DMAC_CH2, 16);

static struct usart_config const loopback_usart =
	USART_CONFIG(usart1_hw,
		     loopback_rx_dma.usart_rx,
		     loopback_tx_dma.usart_tx,
		     115200,
		     0,
		     loopback_queue,
		     loopback_queue);

/******************************************************************************
 * Forward USART4 as a simple USB serial interface.
 */
static struct usart_config const forward_usart;
struct usb_stream_config const forward_usb;

static struct queue const usart_to_usb = QUEUE_DIRECT(64, uint8_t,
						      forward_usart.producer,
						      forward_usb.consumer);
static struct queue const usb_to_usart = QUEUE_DIRECT(64, uint8_t,
						      forward_usb.producer,
						      forward_usart.consumer);

static struct usart_tx_dma const forward_tx_dma =
	USART_TX_DMA(STM32_DMAC_CH7, 16);

static struct usart_config const forward_usart =
	USART_CONFIG(usart4_hw,
		     usart_rx_interrupt,
		     forward_tx_dma.usart_tx,
		     115200,
		     0,
		     usart_to_usb,
		     usb_to_usart);

#define USB_STREAM_RX_SIZE	16
#define USB_STREAM_TX_SIZE	16

USB_STREAM_CONFIG(forward_usb,
		  USB_IFACE_STREAM,
		  USB_STR_STREAM_NAME,
		  USB_EP_STREAM,
		  USB_STREAM_RX_SIZE,
		  USB_STREAM_TX_SIZE,
		  usb_to_usart,
		  usart_to_usb)

/******************************************************************************
 * Define the strings used in our USB descriptors.
 */
const void *const usb_strings[] = {
	[USB_STR_DESC]         = usb_string_desc,
	[USB_STR_VENDOR]       = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT]      = USB_STRING_DESC("fusb307bgevb"),
	[USB_STR_VERSION]      = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_STREAM_NAME]  = USB_STRING_DESC("Forward"),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("Shell"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/******************************************************************************
 * I2C interface.
 */
const struct i2c_port_t i2c_ports[] = {
	{"tcpc", I2C_PORT_TCPC, 400 /* kHz */, GPIO_I2C2_SCL, GPIO_I2C2_SDA}
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/******************************************************************************
 * PD
 */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_TCPC,
			.addr_flags = FUSB307_I2C_SLAVE_ADDR_FLAGS,
		},
		.drv = &fusb307_tcpm_drv,
	},
};


uint16_t tcpc_get_alert_status(void)
{
	uint16_t status = 0;

	if (!gpio_get_level(GPIO_USB_C0_PD_INT_ODL))
		status |= PD_STATUS_TCPC_ALERT_0;

	return status;
}

void board_reset_pd_mcu(void)
{
}

int pd_snk_is_vbus_provided(int port)
{
	return EC_ERROR_UNIMPLEMENTED;
}

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
	/* No battery, nothing to do */
}

void pd_power_supply_reset(int port)
{
	/* Disable VBUS */
	fusb307_power_supply_reset(port);
}

int pd_set_power_supply_ready(int port)
{
	return EC_SUCCESS;
}

int pd_board_checks(void)
{
	return EC_SUCCESS;
}

/******************************************************************************
 * Initialize board.
 */
static void board_init(void)
{
	/* Enable button interrupts */
	gpio_enable_interrupt(GPIO_USER_BUTTON_ENTER);
	gpio_enable_interrupt(GPIO_USER_BUTTON_REFRESH);
	gpio_enable_interrupt(GPIO_USER_BUTTON_DOWN);
	/* Enable TCPC alert interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_PD_INT_ODL);

	lcd_init(20, 4, 0);
	lcd_set_cursor(0, 0);
	lcd_print_string("USB-C");
	lcd_set_cursor(0, 1);
	lcd_print_string("Sink Advertiser");
	queue_init(&loopback_queue);
	queue_init(&usart_to_usb);
	queue_init(&usb_to_usart);
	usart_init(&loopback_usart);
	usart_init(&forward_usart);

}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
