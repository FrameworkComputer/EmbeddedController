/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Servo micro board configuration */

#include "common.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "queue_policies.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "usart-stm32f0.h"
#include "usart_tx_dma.h"
#include "usart_rx_dma.h"
#include "usb_gpio.h"
#include "usb_i2c.h"
#include "usb_spi.h"
#include "usb-stream.h"
#include "util.h"


/******************************************************************************
 * Build GPIO tables and expose a subset of the GPIOs over USB.
 */

#include "gpio_list.h"

static enum gpio_signal const usb_gpio_list[] = {
GPIO_UART1_EN_L,
GPIO_SERVO_JTAG_TRST_L,
GPIO_SPI1_BUF_EN_L,
GPIO_SPI2_BUF_EN_L,
GPIO_UART2_EN_L,
GPIO_SPI1_VREF_33,
GPIO_SPI1_VREF_18,
GPIO_SPI2_VREF_33,
GPIO_SPI2_VREF_18,
GPIO_SERVO_JTAG_TRST_DIR,
GPIO_SERVO_JTAG_TDI_DIR,
GPIO_TCA6416_RESET_L
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
 * Forward UARTs as a USB serial interface.
 */

#define USB_STREAM_RX_SIZE	16
#define USB_STREAM_TX_SIZE	16

/******************************************************************************
 * Forward USART2 as a simple USB serial interface.
 */

static struct usart_config const usart2;
struct usb_stream_config const usart2_usb;

static struct queue const usart2_to_usb = QUEUE_DIRECT(64, uint8_t,
	usart2.producer, usart2_usb.consumer);
static struct queue const usb_to_usart2 = QUEUE_DIRECT(64, uint8_t,
	usart2_usb.producer, usart2.consumer);

static struct usart_config const usart2 =
	USART_CONFIG(usart2_hw,
		usart_rx_interrupt,
		usart_tx_interrupt,
		115200,
		usart2_to_usb,
		usb_to_usart2);

USB_STREAM_CONFIG(usart2_usb,
	USB_IFACE_USART2_STREAM,
	USB_STR_USART2_STREAM_NAME,
	USB_EP_USART2_STREAM,
	USB_STREAM_RX_SIZE,
	USB_STREAM_TX_SIZE,
	usb_to_usart2,
	usart2_to_usb)


/******************************************************************************
 * Forward USART3 as a simple USB serial interface.
 */

static struct usart_config const usart3;
struct usb_stream_config const usart3_usb;

static struct queue const usart3_to_usb = QUEUE_DIRECT(64, uint8_t,
	usart3.producer, usart3_usb.consumer);
static struct queue const usb_to_usart3 = QUEUE_DIRECT(64, uint8_t,
	usart3_usb.producer, usart3.consumer);

static struct usart_config const usart3 =
	USART_CONFIG(usart3_hw,
		usart_rx_interrupt,
		usart_tx_interrupt,
		115200,
		usart3_to_usb,
		usb_to_usart3);

USB_STREAM_CONFIG(usart3_usb,
	USB_IFACE_USART3_STREAM,
	USB_STR_USART3_STREAM_NAME,
	USB_EP_USART3_STREAM,
	USB_STREAM_RX_SIZE,
	USB_STREAM_TX_SIZE,
	usb_to_usart3,
	usart3_to_usb)


/******************************************************************************
 * Forward USART4 as a simple USB serial interface.
 */

static struct usart_config const usart4;
struct usb_stream_config const usart4_usb;

static struct queue const usart4_to_usb = QUEUE_DIRECT(64, uint8_t,
	usart4.producer, usart4_usb.consumer);
static struct queue const usb_to_usart4 = QUEUE_DIRECT(64, uint8_t,
	usart4_usb.producer, usart4.consumer);

static struct usart_config const usart4 =
	USART_CONFIG(usart4_hw,
		usart_rx_interrupt,
		usart_tx_interrupt,
		115200,
		usart4_to_usb,
		usb_to_usart4);

USB_STREAM_CONFIG(usart4_usb,
	USB_IFACE_USART4_STREAM,
	USB_STR_USART4_STREAM_NAME,
	USB_EP_USART4_STREAM,
	USB_STREAM_RX_SIZE,
	USB_STREAM_TX_SIZE,
	usb_to_usart4,
	usart4_to_usb)


/******************************************************************************
 * Define the strings used in our USB descriptors.
 */

const void *const usb_strings[] = {
	[USB_STR_DESC]         = usb_string_desc,
	[USB_STR_VENDOR]       = USB_STRING_DESC("Google Inc."),
	[USB_STR_PRODUCT]      = USB_STRING_DESC("Servo Micro"),
	[USB_STR_SERIALNO]     = USB_STRING_DESC("1234-a"),
	[USB_STR_VERSION]      = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_USART4_STREAM_NAME]  = USB_STRING_DESC("Servo UART3"),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("Servo EC Shell"),
	[USB_STR_USART3_STREAM_NAME]  = USB_STRING_DESC("Servo UART2"),
	[USB_STR_USART2_STREAM_NAME]  = USB_STRING_DESC("Servo UART1"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);


/******************************************************************************
 * Support SPI bridging over USB, this requires usb_spi_board_enable and
 * usb_spi_board_disable to be defined to enable and disable the SPI bridge.
 */

/* SPI devices */
/* Set clock divider to 7 since our SPI routing is non-optimal. */
const struct spi_device_t spi_devices[] = {
	{ CONFIG_SPI_FLASH_PORT, 7, GPIO_SPI_CS},
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

void usb_spi_board_enable(struct usb_spi_config const *config)
{
	/* Remap SPI2 to DMA channels 6 and 7 */
	/* STM32F072 SPI2 defaults to using DMA channels 4 and 5 */
	/* but cros_ec hardcodes a 6/7 assumption in registers.h */
	STM32_SYSCFG_CFGR1 |= (1 << 24);

	/* Configure SPI GPIOs */
	gpio_config_module(MODULE_SPI_FLASH, 1);

	/* Set all four SPI pins to high speed */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0xff000000;

	/* Enable clocks to SPI2 module */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;

	/* Reset SPI2 */
	STM32_RCC_APB1RSTR |= STM32_RCC_PB1_SPI2;
	STM32_RCC_APB1RSTR &= ~STM32_RCC_PB1_SPI2;

	spi_enable(CONFIG_SPI_FLASH_PORT, 1);
}

void usb_spi_board_disable(struct usb_spi_config const *config)
{
	spi_enable(CONFIG_SPI_FLASH_PORT, 0);

	/* Disable clocks to SPI2 module */
	STM32_RCC_APB1ENR &= ~STM32_RCC_PB1_SPI2;

	/* Release SPI GPIOs */
	gpio_config_module(MODULE_SPI_FLASH, 0);
}

USB_SPI_CONFIG(usb_spi, USB_IFACE_SPI, USB_EP_SPI);


/******************************************************************************
 * Support I2C bridging over USB, this requires usb_i2c_board_enable and
 * usb_i2c_board_disable to be defined to enable and disable the SPI bridge.
 */

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", I2C_PORT_MASTER, 100,
		GPIO_MASTER_I2C_SCL, GPIO_MASTER_I2C_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

USB_I2C_CONFIG(usb_i2c, USB_IFACE_I2C, USB_EP_I2C);


/******************************************************************************
 * Initialize board.
 */
static void board_init(void)
{
	/* USB to serial queues */
	queue_init(&usart2_to_usb);
	queue_init(&usb_to_usart2);
	queue_init(&usart3_to_usb);
	queue_init(&usb_to_usart3);
	queue_init(&usart4_to_usb);
	queue_init(&usb_to_usart4);

	/* UART init */
	usart_init(&usart2);
	usart_init(&usart3);
	usart_init(&usart4);

	/* Enable GPIO expander. */
	gpio_set_level(GPIO_TCA6416_RESET_L, 1);

	/* Structured enpoints */
	usb_spi_enable(&usb_spi, 1);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
