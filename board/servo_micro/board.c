/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Servo micro board configuration */

#include "common.h"
#include "console.h"
#include "ec_version.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "i2c_ite_flash_support.h"
#include "queue_policies.h"
#include "registers.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "update_fw.h"
#include "usart-stm32f0.h"
#include "usart_rx_dma.h"
#include "usart_tx_dma.h"
#include "usb-stream.h"
#include "usb_hw.h"
#include "usb_i2c.h"
#include "usb_spi.h"
#include "util.h"

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= STM32_RCC_SYSCFGEN;

	/*
	 * the DMA mapping is :
	 *  Chan 3 : USART3_RX
	 *  Chan 5 : USART2_RX
	 *  Chan 6 : USART4_RX (Disable)
	 *  Chan 6 : SPI2_RX
	 *  Chan 7 : SPI2_TX
	 *
	 *  i2c : no dma
	 *  tim16/17: no dma
	 */
	STM32_SYSCFG_CFGR1 |= BIT(26); /* Remap USART3 RX/TX DMA */

	/* Remap SPI2 to DMA channels 6 and 7 */
	/* STM32F072 SPI2 defaults to using DMA channels 4 and 5 */
	/* but cros_ec hardcodes a 6/7 assumption in registers.h */
	STM32_SYSCFG_CFGR1 |= BIT(24);
}

/******************************************************************************
 * Forward UARTs as a USB serial interface.
 */

#define USB_STREAM_RX_SIZE 32
#define USB_STREAM_TX_SIZE 64

/******************************************************************************
 * Forward USART2 (EC) as a simple USB serial interface.
 */

static struct usart_config const usart2;
struct usb_stream_config const usart2_usb;

static struct queue const usart2_to_usb =
	QUEUE_DIRECT(1024, uint8_t, usart2.producer, usart2_usb.consumer);
static struct queue const usb_to_usart2 =
	QUEUE_DIRECT(64, uint8_t, usart2_usb.producer, usart2.consumer);

static struct usart_rx_dma const usart2_rx_dma =
	USART_RX_DMA(STM32_DMAC_CH5, 32);

static struct usart_config const usart2 =
	USART_CONFIG(usart2_hw, usart2_rx_dma.usart_rx, usart_tx_interrupt,
		     115200, 0, usart2_to_usb, usb_to_usart2);

USB_STREAM_CONFIG_USART_IFACE(usart2_usb, USB_IFACE_USART2_STREAM,
			      USB_STR_USART2_STREAM_NAME, USB_EP_USART2_STREAM,
			      USB_STREAM_RX_SIZE, USB_STREAM_TX_SIZE,
			      usb_to_usart2, usart2_to_usb, usart2)

/******************************************************************************
 * Forward USART3 (CPU) as a simple USB serial interface.
 */

static struct usart_config const usart3;
struct usb_stream_config const usart3_usb;

static struct queue const usart3_to_usb =
	QUEUE_DIRECT(1024, uint8_t, usart3.producer, usart3_usb.consumer);
static struct queue const usb_to_usart3 =
	QUEUE_DIRECT(64, uint8_t, usart3_usb.producer, usart3.consumer);

static struct usart_rx_dma const usart3_rx_dma =
	USART_RX_DMA(STM32_DMAC_CH3, 32);

static struct usart_config const usart3 =
	USART_CONFIG(usart3_hw, usart3_rx_dma.usart_rx, usart_tx_interrupt,
		     115200, 0, usart3_to_usb, usb_to_usart3);

USB_STREAM_CONFIG_USART_IFACE(usart3_usb, USB_IFACE_USART3_STREAM,
			      USB_STR_USART3_STREAM_NAME, USB_EP_USART3_STREAM,
			      USB_STREAM_RX_SIZE, USB_STREAM_TX_SIZE,
			      usb_to_usart3, usart3_to_usb, usart3)

/******************************************************************************
 * Forward USART4 (cr50) as a simple USB serial interface.
 *  We cannot enable DMA due to lack of DMA channels.
 */

static struct usart_config const usart4;
struct usb_stream_config const usart4_usb;

static struct queue const usart4_to_usb =
	QUEUE_DIRECT(1024, uint8_t, usart4.producer, usart4_usb.consumer);
static struct queue const usb_to_usart4 =
	QUEUE_DIRECT(64, uint8_t, usart4_usb.producer, usart4.consumer);

static struct usart_config const usart4 =
	USART_CONFIG(usart4_hw, usart_rx_interrupt, usart_tx_interrupt, 115200,
		     0, usart4_to_usb, usb_to_usart4);

USB_STREAM_CONFIG_USART_IFACE(usart4_usb, USB_IFACE_USART4_STREAM,
			      USB_STR_USART4_STREAM_NAME, USB_EP_USART4_STREAM,
			      USB_STREAM_RX_SIZE, USB_STREAM_TX_SIZE,
			      usb_to_usart4, usart4_to_usb, usart4)

/******************************************************************************
 * Check parity setting on usarts.
 */
static int command_uart_parity(int argc, const char **argv)
{
	int parity = 0, newparity;
	struct usart_config const *usart;
	char *e;

	if ((argc < 2) || (argc > 3))
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "usart2"))
		usart = &usart2;
	else if (!strcasecmp(argv[1], "usart3"))
		usart = &usart3;
	else if (!strcasecmp(argv[1], "usart4"))
		usart = &usart4;
	else
		return EC_ERROR_PARAM1;

	if (argc == 3) {
		parity = strtoi(argv[2], &e, 0);
		if (*e || (parity < 0) || (parity > 2))
			return EC_ERROR_PARAM2;

		usart_set_parity(usart, parity);
	}

	newparity = usart_get_parity(usart);
	ccprintf("Parity on %s is %d.\n", argv[1], newparity);

	if ((argc == 3) && (newparity != parity))
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(parity, command_uart_parity, "usart[2|3|4] [0|1|2]",
			"Set parity on uart");

/******************************************************************************
 * Set baud rate setting on usarts.
 */
static int command_uart_baud(int argc, const char **argv)
{
	int baud = 0;
	struct usart_config const *usart;
	char *e;

	if ((argc < 2) || (argc > 3))
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "usart2"))
		usart = &usart2;
	else if (!strcasecmp(argv[1], "usart3"))
		usart = &usart3;
	else if (!strcasecmp(argv[1], "usart4"))
		usart = &usart4;
	else
		return EC_ERROR_PARAM1;

	baud = strtoi(argv[2], &e, 0);
	if (*e || baud < 0)
		return EC_ERROR_PARAM2;

	usart_set_baud(usart, baud);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(baud, command_uart_baud, "usart[2|3|4] rate",
			"Set baud rate on uart");

/******************************************************************************
 * Hold the usart pins low while disabling it, or return it to normal.
 */
static int command_hold_usart_low(int argc, const char **argv)
{
	/* Each bit represents if that port rx is being held low */
	static int usart_status;

	int usart_mask;
	enum gpio_signal rx;

	if (argc > 3 || argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "usart2")) {
		usart_mask = 1 << 2;
		rx = GPIO_USART2_SERVO_RX_DUT_TX;
	} else if (!strcasecmp(argv[1], "usart3")) {
		usart_mask = 1 << 3;
		rx = GPIO_USART3_SERVO_RX_DUT_TX;
	} else if (!strcasecmp(argv[1], "usart4")) {
		usart_mask = 1 << 4;
		rx = GPIO_USART4_SERVO_RX_DUT_TX;
	} else {
		return EC_ERROR_PARAM1;
	}

	/* Updating the status of this port */
	if (argc == 3) {
		char *e;
		const int hold_low = strtoi(argv[2], &e, 0);

		if (*e || (hold_low < 0) || (hold_low > 1))
			return EC_ERROR_PARAM2;

		if (!!(usart_status & usart_mask) == hold_low) {
			/* Do nothing since there is no change */
		} else if (hold_low) {
			/*
			 * No need to shutdown UART, just de-mux the RX pin from
			 * UART and change it to a GPIO temporarily.
			 */
			gpio_config_pin(MODULE_USART, rx, 0);
			gpio_set_flags(rx, GPIO_OUT_LOW);

			/* Update global uart state */
			usart_status |= usart_mask;
		} else {
			/*
			 * Mux the RX pin back to GPIO mode
			 */
			gpio_config_pin(MODULE_USART, rx, 1);

			/* Update global uart state */
			usart_status &= ~usart_mask;
		}
	}

	/* Print status for get and set case. */
	ccprintf("USART status: %s\n",
		 usart_status & usart_mask ? "held low" : "normal");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hold_usart_low, command_hold_usart_low,
			"usart[2|3|4] [0|1]?",
			"Get/set the hold-low state for usart port");

/******************************************************************************
 * Define the strings used in our USB descriptors.
 */
const void *const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google LLC"),
	[USB_STR_PRODUCT] = USB_STRING_DESC("Servo Micro"),
	[USB_STR_SERIALNO] = 0,
	[USB_STR_VERSION] = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_SPI_NAME] = USB_STRING_DESC("SPI"),
	[USB_STR_I2C_NAME] = USB_STRING_DESC("I2C"),
	[USB_STR_USART4_STREAM_NAME] = USB_STRING_DESC("UART3"),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("Servo Shell"),
	[USB_STR_USART3_STREAM_NAME] = USB_STRING_DESC("CPU"),
	[USB_STR_USART2_STREAM_NAME] = USB_STRING_DESC("EC"),
	[USB_STR_UPDATE_NAME] = USB_STRING_DESC("Firmware update"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/******************************************************************************
 * Support SPI bridging over USB, this requires usb_spi_board_enable and
 * usb_spi_board_disable to be defined to enable and disable the SPI bridge.
 */

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	{ CONFIG_SPI_FLASH_PORT, 1, GPIO_SPI_CS, USB_SPI_ENABLED },
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

void usb_spi_board_enable(void)
{
	/* Configure SPI GPIOs */
	gpio_config_module(MODULE_SPI_FLASH, 1);

	/* Set all four SPI pins to high speed */
	STM32_GPIO_OSPEEDR(GPIO_B) |= 0xff000000;

	/* Enable clocks to SPI2 module */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_SPI2;

	/* Reset SPI2 */
	STM32_RCC_APB1RSTR |= STM32_RCC_PB1_SPI2;
	STM32_RCC_APB1RSTR &= ~STM32_RCC_PB1_SPI2;

	spi_enable(&spi_devices[0], 1);
}

void usb_spi_board_disable(void)
{
	spi_enable(&spi_devices[0], 0);

	/* Disable clocks to SPI2 module */
	STM32_RCC_APB1ENR &= ~STM32_RCC_PB1_SPI2;

	/* Release SPI GPIOs */
	gpio_config_module(MODULE_SPI_FLASH, 0);
}

/******************************************************************************
 * Support I2C bridging over USB.
 */

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{
		.name = "master",
		.port = I2C_PORT_MASTER,
		.kbps = 100,
		.scl = GPIO_MASTER_I2C_SCL,
		.sda = GPIO_MASTER_I2C_SDA,
		.flags = I2C_PORT_FLAG_DYNAMIC_SPEED,
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

int usb_i2c_board_is_enabled(void)
{
	return 1;
}

/* Configure ITE flash support module */
const struct ite_dfu_config_t ite_dfu_config = {
	.i2c_port = I2C_PORT_MASTER,
	.scl = GPIO_MASTER_I2C_SCL,
	.sda = GPIO_MASTER_I2C_SDA,
};

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

	/* Structured endpoints */
	usb_spi_enable(1);

	/* Enable UARTs by default. */
	gpio_set_level(GPIO_UART1_EN_L, 0);
	gpio_set_level(GPIO_UART2_EN_L, 0);
	/* Disable power output. */
	gpio_set_level(GPIO_SPI1_VREF_18, 0);
	gpio_set_level(GPIO_SPI1_VREF_33, 0);
	gpio_set_level(GPIO_SPI2_VREF_18, 0);
	gpio_set_level(GPIO_SPI2_VREF_33, 0);
	/* Enable UART3 routing. */
	gpio_set_level(GPIO_SPI1_MUX_SEL, 1);
	gpio_set_level(GPIO_SPI1_BUF_EN_L, 1);
	gpio_set_level(GPIO_JTAG_BUFIN_EN_L, 0);
	gpio_set_level(GPIO_SERVO_JTAG_TDO_BUFFER_EN, 1);
	gpio_set_level(GPIO_SERVO_JTAG_TDO_SEL, 1);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/******************************************************************************
 * Turn down USART before jumping to RW.
 */
static void board_jump(void)
{
	/*
	 * If we don't shutdown the USARTs before jumping to RW, then when early
	 * RW tries to set the GPIOs to input (or anything other than alternate)
	 * the jump fail on some servo micros.
	 *
	 * It also make sense to shut them down since RW will reinitialize them
	 * in board_init above.
	 */
	usart_shutdown(&usart2);
	usart_shutdown(&usart3);
	usart_shutdown(&usart4);

	/* Shutdown other hardware modules and let RW reinitialize them */
	usb_spi_enable(0);
}
DECLARE_HOOK(HOOK_SYSJUMP, board_jump, HOOK_PRIO_DEFAULT);
