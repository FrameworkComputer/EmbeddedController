/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* HyperDebug board configuration */

#include "common.h"
#include "console.h"
#include "ec_version.h"
#include "gpio.h"
#include "hwtimer.h"
#include "i2c.h"
#include "queue_policies.h"
#include "registers.h"
#include "spi.h"
#include "task.h"
#include "timer.h"
#include "usart-stm32l5.h"
#include "usb-stream.h"
#include "usb_hw.h"
#include "usb_spi.h"

#include <stdio.h>

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= STM32_RCC_SYSCFGEN;
}

/******************************************************************************
 * Forward UARTs as a USB serial interface.
 */

#define USB_STREAM_RX_SIZE 16
#define USB_STREAM_TX_SIZE 16

/******************************************************************************
 * Forward USART2 as a simple USB serial interface.
 */

static struct usart_config const usart2;
struct usb_stream_config const usart2_usb;

static struct queue const usart2_to_usb =
	QUEUE_DIRECT(64, uint8_t, usart2.producer, usart2_usb.consumer);
static struct queue const usb_to_usart2 =
	QUEUE_DIRECT(64, uint8_t, usart2_usb.producer, usart2.consumer);

static struct usart_config const usart2 =
	USART_CONFIG(usart2_hw, usart_rx_interrupt, usart_tx_interrupt, 115200,
		     0, usart2_to_usb, usb_to_usart2);

USB_STREAM_CONFIG(usart2_usb, USB_IFACE_USART2_STREAM,
		  USB_STR_USART2_STREAM_NAME, USB_EP_USART2_STREAM,
		  USB_STREAM_RX_SIZE, USB_STREAM_TX_SIZE, usb_to_usart2,
		  usart2_to_usb)

/******************************************************************************
 * Forward USART3 as a simple USB serial interface.
 */

static struct usart_config const usart3;
struct usb_stream_config const usart3_usb;

static struct queue const usart3_to_usb =
	QUEUE_DIRECT(64, uint8_t, usart3.producer, usart3_usb.consumer);
static struct queue const usb_to_usart3 =
	QUEUE_DIRECT(64, uint8_t, usart3_usb.producer, usart3.consumer);

static struct usart_config const usart3 =
	USART_CONFIG(usart3_hw, usart_rx_interrupt, usart_tx_interrupt, 115200,
		     0, usart3_to_usb, usb_to_usart3);

USB_STREAM_CONFIG(usart3_usb, USB_IFACE_USART3_STREAM,
		  USB_STR_USART3_STREAM_NAME, USB_EP_USART3_STREAM,
		  USB_STREAM_RX_SIZE, USB_STREAM_TX_SIZE, usb_to_usart3,
		  usart3_to_usb)

/******************************************************************************
 * Forward USART4 as a simple USB serial interface.
 */

static struct usart_config const usart4;
struct usb_stream_config const usart4_usb;

static struct queue const usart4_to_usb =
	QUEUE_DIRECT(64, uint8_t, usart4.producer, usart4_usb.consumer);
static struct queue const usb_to_usart4 =
	QUEUE_DIRECT(64, uint8_t, usart4_usb.producer, usart4.consumer);

static struct usart_config const usart4 =
	USART_CONFIG(usart4_hw, usart_rx_interrupt, usart_tx_interrupt, 115200,
		     0, usart4_to_usb, usb_to_usart4);

USB_STREAM_CONFIG(usart4_usb, USB_IFACE_USART4_STREAM,
		  USB_STR_USART4_STREAM_NAME, USB_EP_USART4_STREAM,
		  USB_STREAM_RX_SIZE, USB_STREAM_TX_SIZE, usb_to_usart4,
		  usart4_to_usb)

/******************************************************************************
 * Forward USART5 as a simple USB serial interface.
 */

static struct usart_config const usart5;
struct usb_stream_config const usart5_usb;

static struct queue const usart5_to_usb =
	QUEUE_DIRECT(64, uint8_t, usart5.producer, usart5_usb.consumer);
static struct queue const usb_to_usart5 =
	QUEUE_DIRECT(64, uint8_t, usart5_usb.producer, usart5.consumer);

static struct usart_config const usart5 =
	USART_CONFIG(usart5_hw, usart_rx_interrupt, usart_tx_interrupt, 115200,
		     0, usart5_to_usb, usb_to_usart5);

USB_STREAM_CONFIG(usart5_usb, USB_IFACE_USART5_STREAM,
		  USB_STR_USART5_STREAM_NAME, USB_EP_USART5_STREAM,
		  USB_STREAM_RX_SIZE, USB_STREAM_TX_SIZE, usb_to_usart5,
		  usart5_to_usb)

/******************************************************************************
 * Support SPI bridging over USB, this requires usb_spi_board_enable and
 * usb_spi_board_disable to be defined to enable and disable the SPI bridge.
 */

#define OCTOSPI_CLOCK (16000000UL)
#define SPI_CLOCK (16000000UL)

/* SPI devices, default to 250 kb/s for all. */
struct spi_device_t spi_devices[] = {
	{ .name = "SPI2",
	  .port = 1,
	  .div = 5,
	  .gpio_cs = GPIO_CN9_25,
	  .usb_flags = USB_SPI_ENABLED },
	{ .name = "QSPI",
	  .port = -1 /* OCTOSPI */,
	  .div = 63,
	  .gpio_cs = GPIO_CN10_6,
	  .usb_flags = USB_SPI_ENABLED | USB_SPI_CUSTOM_SPI_DEVICE },
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

void usb_spi_board_enable(struct usb_spi_config const *config)
{
	/* All initialization already done in board_init(). */
}

void usb_spi_board_disable(struct usb_spi_config const *config)
{
}

USB_SPI_CONFIG(usb_spi, USB_IFACE_SPI, USB_EP_SPI, 0);

/******************************************************************************
 * Support I2C bridging over USB.
 */

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "controller",
	  .port = I2C_PORT_CONTROLLER,
	  .kbps = 100,
	  .scl = GPIO_CN7_2,
	  .sda = GPIO_CN7_4 },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

int usb_i2c_board_is_enabled(void)
{
	return 1;
}

/******************************************************************************
 * Define the strings used in our USB descriptors.
 */

const void *const usb_strings[] = {
	[USB_STR_DESC] = usb_string_desc,
	[USB_STR_VENDOR] = USB_STRING_DESC("Google LLC"),
	[USB_STR_PRODUCT] = USB_STRING_DESC("HyperDebug"),
	[USB_STR_SERIALNO] = 0,
	[USB_STR_VERSION] = USB_STRING_DESC(CROS_EC_VERSION32),
	[USB_STR_CONSOLE_NAME] = USB_STRING_DESC("HyperDebug Shell"),
	[USB_STR_SPI_NAME] = USB_STRING_DESC("SPI"),
	[USB_STR_I2C_NAME] = USB_STRING_DESC("I2C"),
	[USB_STR_USART2_STREAM_NAME] = USB_STRING_DESC("UART2"),
	[USB_STR_USART3_STREAM_NAME] = USB_STRING_DESC("UART3"),
	[USB_STR_USART4_STREAM_NAME] = USB_STRING_DESC("UART4"),
	[USB_STR_USART5_STREAM_NAME] = USB_STRING_DESC("UART5"),
	[USB_STR_DFU_NAME] = USB_STRING_DESC("DFU"),
};

BUILD_ASSERT(ARRAY_SIZE(usb_strings) == USB_STR_COUNT);

/******************************************************************************
 * OCTOSPI driver.
 */

#define OCTOSPI_INIT_TIMEOUT_US (100 * MSEC)

/*
 * Timeout for a complete SPI transaction.  Users can potentially set the clock
 * down to 62.5 kHz and transfer up to 2048 bytes, which would take 262ms
 * assuming no FIFO stalls.
 */
#define OCTOSPI_TRANSACTION_TIMEOUT_US (500 * MSEC)

/*
 * Wait for a certain set of status bits to all be asserted.
 */
static int octospi_wait_for(uint32_t flags, timestamp_t deadline)
{
	while ((STM32_OCTOSPI_SR & flags) != flags) {
		timestamp_t now = get_time();
		if (timestamp_expired(deadline, &now))
			return EC_ERROR_TIMEOUT;
	}
	return EC_SUCCESS;
}

/*
 * Write transaction: Write a number of bytes on the OCTOSPI bus.
 */
static int octospi_indirect_write(const uint8_t *txdata, int txlen)
{
	timestamp_t deadline;
	/* Deadline on the entire SPI transaction. */
	deadline.val = get_time().val + OCTOSPI_TRANSACTION_TIMEOUT_US;

	/* Enable OCTOSPI, indirect write mode. */
	STM32_OCTOSPI_CR = STM32_OCTOSPI_CR_FMODE_IND_WRITE |
			   STM32_OCTOSPI_CR_EN;
	/* Clear completion flag from last transaction. */
	STM32_OCTOSPI_FCR = STM32_OCTOSPI_FCR_CTCF;

	/* Data length. */
	STM32_OCTOSPI_DLR = txlen - 1;
	/* No instruction or address, only data. */
	STM32_OCTOSPI_CCR =
		STM32_OCTOSPI_CCR_IMODE_NONE | STM32_OCTOSPI_CCR_ADMODE_NONE |
		STM32_OCTOSPI_CCR_ABMODE_NONE | STM32_OCTOSPI_CCR_DMODE_1WIRE;

	/* Transmit data, four bytes at a time. */
	for (int i = 0; i < txlen; i += 4) {
		uint32_t value = 0;
		int rv;
		for (int j = 0; j < 4; j++) {
			if (i + j < txlen)
				value |= txdata[i + j] << (j * 8);
		}
		/* Wait for room in the FIFO. */
		if ((rv = octospi_wait_for(STM32_OCTOSPI_SR_FTF, deadline)))
			return rv;
		STM32_OCTOSPI_DR = value;
	}
	/* Wait for transaction completion flag. */
	return octospi_wait_for(STM32_OCTOSPI_SR_TCF, deadline);
}

/*
 * Read transaction: Optionally write a few bytes, before reading a number of
 * bytes on the OCTOSPI bus.
 */
static int octospi_indirect_read(const uint8_t *control_data, int control_len,
				 uint8_t *rxdata, int rxlen)
{
	uint32_t instruction = 0, address = 0;
	timestamp_t deadline;

	/* Deadline on the entire SPI transaction. */
	deadline.val = get_time().val + OCTOSPI_TRANSACTION_TIMEOUT_US;

	/* Enable OCTOSPI, indirect read mode. */
	STM32_OCTOSPI_CR = STM32_OCTOSPI_CR_FMODE_IND_READ |
			   STM32_OCTOSPI_CR_EN;
	/* Clear completion flag from last transaction. */
	STM32_OCTOSPI_FCR = STM32_OCTOSPI_FCR_CTCF;

	/* Data length (receive). */
	STM32_OCTOSPI_DLR = rxlen - 1;
	if (control_len == 0) {
		/*
		 * Set up OCTOSPI for: No instruction, no address, then read
		 * data.
		 */
		STM32_OCTOSPI_CCR = STM32_OCTOSPI_CCR_IMODE_NONE |
				    STM32_OCTOSPI_CCR_ADMODE_NONE |
				    STM32_OCTOSPI_CCR_ABMODE_NONE |
				    STM32_OCTOSPI_CCR_DMODE_1WIRE;
	} else if (control_len <= 4) {
		/*
		 * Set up OCTOSPI for: One to four bytes of instruction, no
		 * address, then read data.
		 */
		STM32_OCTOSPI_CCR = STM32_OCTOSPI_CCR_IMODE_1WIRE |
				    (control_len - 1)
					    << STM32_OCTOSPI_CCR_ISIZE_POS |
				    STM32_OCTOSPI_CCR_ADMODE_NONE |
				    STM32_OCTOSPI_CCR_ABMODE_NONE |
				    STM32_OCTOSPI_CCR_DMODE_1WIRE;
		for (int i = 0; i < control_len; i++) {
			instruction <<= 8;
			instruction |= control_data[i];
		}
	} else if (control_len <= 8) {
		/*
		 * Set up OCTOSPI for: One to four bytes of instruction, four
		 * bytes of address, then read data.
		 */
		STM32_OCTOSPI_CCR = STM32_OCTOSPI_CCR_IMODE_1WIRE |
				    (control_len - 1)
					    << STM32_OCTOSPI_CCR_ISIZE_POS |
				    STM32_OCTOSPI_CCR_ADMODE_1WIRE |
				    STM32_OCTOSPI_CCR_ADSIZE_4BYTES |
				    STM32_OCTOSPI_CCR_ABMODE_NONE |
				    STM32_OCTOSPI_CCR_DMODE_1WIRE;
		for (int i = 0; i < control_len - 4; i++) {
			instruction <<= 8;
			instruction |= control_data[i];
		}
		for (int i = 0; i < 4; i++) {
			address <<= 8;
			address |= control_data[control_len - 4 + i];
		}
	} else if (control_len <= 12) {
		uint32_t alternate = 0;
		/*
		 * Set up OCTOSPI for: One to four bytes of instruction, four
		 * bytes of address, four "alternate" bytes, then read data.
		 */
		STM32_OCTOSPI_CCR = STM32_OCTOSPI_CCR_IMODE_1WIRE |
				    (control_len - 1)
					    << STM32_OCTOSPI_CCR_ISIZE_POS |
				    STM32_OCTOSPI_CCR_ADMODE_1WIRE |
				    STM32_OCTOSPI_CCR_ADSIZE_4BYTES |
				    STM32_OCTOSPI_CCR_ABMODE_1WIRE |
				    STM32_OCTOSPI_CCR_ABSIZE_4BYTES |
				    STM32_OCTOSPI_CCR_DMODE_1WIRE;
		for (int i = 0; i < control_len - 8; i++) {
			instruction <<= 8;
			instruction |= control_data[i];
		}
		for (int i = 0; i < 4; i++) {
			address <<= 8;
			address |= control_data[control_len - 8 + i];
		}
		for (int i = 0; i < 4; i++) {
			alternate <<= 8;
			alternate |= control_data[control_len - 4 + i];
		}
		STM32_OCTOSPI_ABR = alternate;
	} else {
		return EC_ERROR_UNIMPLEMENTED;
	}
	/* Set instruction and address registers, triggering the start of the
	 * write+read transaction. */
	STM32_OCTOSPI_IR = instruction;
	STM32_OCTOSPI_AR = address;

	/* Receive data, four bytes at a time. */
	for (int i = 0; i < rxlen; i += 4) {
		int rv;
		uint32_t value;
		/* Wait for data available in the FIFO. */
		if ((rv = octospi_wait_for(STM32_OCTOSPI_SR_FTF, deadline)))
			return rv;
		value = STM32_OCTOSPI_DR;
		for (int j = 0; j < 4; j++) {
			if (i + j < rxlen)
				rxdata[i + j] = value >> (j * 8);
		}
	}
	/* Wait for transaction completion flag. */
	return octospi_wait_for(STM32_OCTOSPI_SR_TCF, deadline);
}

/*
 * Board-specific SPI driver entry point, called by usb_spi.c.  On this board,
 * the only spi device declared as requiring board specific driver is OCTOSPI.
 */
int usb_spi_board_transaction(const struct spi_device_t *spi_device,
			      const uint8_t *txdata, int txlen, uint8_t *rxdata,
			      int rxlen)
{
	int rv = EC_SUCCESS;
	bool previous_cs;

	previous_cs = gpio_get_level(spi_device->gpio_cs);

	/* Drive chip select low */
	gpio_set_level(spi_device->gpio_cs, 0);

	/*
	 * STM32L5 OctoSPI in "indirect mode" supports two types of SPI
	 * operations, "read" and "write", in addition to the main data, each
	 * type of operation can be preceded by up to 12 bytes of
	 * "instructions", (which are always written from HyperDebug to the SPI
	 * device).  We can use the above features to support some combination
	 * of write-followed-by-read in a single OctoSPI transaction.
	 */

	if (rxlen == SPI_READBACK_ALL) {
		cprints(CC_SPI,
			"Full duplex not supported by OctoSPI hardware");
		rv = EC_ERROR_UNIMPLEMENTED;
	} else if (!rxlen && !txlen) {
		/* No operation requested, done. */
	} else if (!rxlen) {
		/*
		 * Transmit-only transaction.  This is implemented by not using
		 * any of the up to 12 bytes of instructions, but as all "data".
		 */
		rv = octospi_indirect_write(txdata, txlen);
	} else if (txlen <= 12) {
		/*
		 * Sending of up to 12 bytes, followed by reading a possibly
		 * large number of bytes.  This is implemented by a "read"
		 * transaction using the instruction and address feature of
		 * OctoSPI.
		 */
		rv = octospi_indirect_read(txdata, txlen, rxdata, rxlen);
	} else {
		/*
		 * Sending many bytes, followed by reading.  This is implemented
		 * as two separate OctoSPI transactions.  (Chip select is kept
		 * asserted across both transactions, outside the control of the
		 * OctoSPI hardware.)
		 */
		rv = octospi_indirect_write(txdata, txlen);
		if (rv == EC_SUCCESS)
			rv = octospi_indirect_read(NULL, 0, rxdata, rxlen);
	}

	/* Return chip select to previous level. */
	gpio_set_level(spi_device->gpio_cs, previous_cs);
	return rv;
}

/******************************************************************************
 * Initialize board.
 */

static void board_init(void)
{
	timestamp_t deadline;

	STM32_GPIO_BSRR(STM32_GPIOE_BASE) |= 0x0000FF00;

	/* We know VDDIO2 is present, enable the GPIO circuit. */
	STM32_PWR_CR2 |= STM32_PWR_CR2_IOSV;

	/* USB to serial queues */
	queue_init(&usart2_to_usb);
	queue_init(&usb_to_usart2);
	queue_init(&usart3_to_usb);
	queue_init(&usb_to_usart3);
	queue_init(&usart4_to_usb);
	queue_init(&usb_to_usart4);
	queue_init(&usart5_to_usb);
	queue_init(&usb_to_usart5);

	/* UART init */
	usart_init(&usart2);
	usart_init(&usart3);
	usart_init(&usart4);
	usart_init(&usart5);

	/* Structured endpoints */
	usb_spi_enable(&usb_spi, 1);

	/* Configure SPI GPIOs */
	gpio_config_module(MODULE_SPI, 1);

	/*
	 * Enable SPI2.
	 */

	/* Enable clocks to SPI2 module */
	STM32_RCC_APB1ENR1 |= STM32_RCC_APB1ENR1_SPI2EN;

	/* Reset SPI2 */
	STM32_RCC_APB1RSTR1 |= STM32_RCC_APB1RSTR1_SPI2RST;
	STM32_RCC_APB1RSTR1 &= ~STM32_RCC_APB1RSTR1_SPI2RST;

	spi_enable(&spi_devices[0], 1);

	/*
	 * Enable OCTOSPI, no driver for this in chip/stm32.
	 */
	deadline.val = get_time().val + OCTOSPI_INIT_TIMEOUT_US;

	STM32_RCC_AHB3ENR |= STM32_RCC_AHB3ENR_QSPIEN;
	while (STM32_OCTOSPI_SR & STM32_OCTOSPI_SR_BUSY) {
		timestamp_t now = get_time();
		if (timestamp_expired(deadline, &now)) {
			/*
			 * Ideally, the USB host would have a way of
			 * discovering our failure to initialize OctoSPI.  But
			 * for now, log and move on, this would happen only on
			 * code bug or hardware failure.
			 */
			cprints(CC_SPI, "Initialization of OctoSPI failed");
			break;
		}
	}

	/*
	 * Declare that a "Standard" SPI flash device, maximum size is connected
	 * to OCTOSPI.  This allows the controller to send arbitrary 32-bit
	 * addresses, which is needed as we use the instruction and address
	 * bytes as arbitrary data to send via SPI.
	 */
	STM32_OCTOSPI_DCR1 = STM32_OCTOSPI_DCR1_MTYP_STANDARD |
			     STM32_OCTOSPI_DCR1_DEVSIZE_MSK;
	/* Clock prescaler (max value 255) */
	STM32_OCTOSPI_DCR2 = spi_devices[1].div;
	/* Zero dummy cycles */
	STM32_OCTOSPI_TCR = 0;
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

const char *board_read_serial(void)
{
	const uint32_t *stm32_unique_id =
		(const uint32_t *)STM32_UNIQUE_ID_BASE;
	static char serial[13];

	// Compute 12 hex digits from three factory programmed 32-bit "Unique
	// ID" words in a manner that has been observed to be consistent with
	// how the STM DFU ROM bootloader presents its serial number.  This
	// means that the serial number of any particular HyperDebug board will
	// remain the same as it enters and leaves DFU mode for software
	// upgrade.
	int rc = snprintf(serial, sizeof(serial), "%08X%04X",
			  stm32_unique_id[0] + stm32_unique_id[2],
			  stm32_unique_id[1] >> 16);
	if (12 != rc)
		return NULL;
	return serial;
}

/**
 * Find a GPIO signal by name.
 *
 * This is copied from gpio.c unfortunately, as it is static over there.
 *
 * @param name		Signal name to find
 *
 * @return the signal index, or GPIO_COUNT if no match.
 */
static enum gpio_signal find_signal_by_name(const char *name)
{
	int i;

	if (!name || !*name)
		return GPIO_COUNT;

	for (i = 0; i < GPIO_COUNT; i++)
		if (gpio_is_implemented(i) &&
		    !strcasecmp(name, gpio_get_name(i)))
			return i;

	return GPIO_COUNT;
}

/*
 * Set the mode of a GPIO pin: input/opendrain/pushpull/alternate.
 */
static int command_gpio_mode(int argc, const char **argv)
{
	int gpio;
	int flags;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	gpio = find_signal_by_name(argv[1]);
	if (gpio == GPIO_COUNT)
		return EC_ERROR_PARAM1;
	flags = gpio_get_flags(gpio);

	flags = flags & ~(GPIO_INPUT | GPIO_OUTPUT | GPIO_OPEN_DRAIN);
	if (strcasecmp(argv[2], "input") == 0)
		flags |= GPIO_INPUT;
	else if (strcasecmp(argv[2], "opendrain") == 0)
		flags |= GPIO_OUTPUT | GPIO_OPEN_DRAIN;
	else if (strcasecmp(argv[2], "pushpull") == 0)
		flags |= GPIO_OUTPUT;
	else if (strcasecmp(argv[2], "alternate") == 0)
		flags |= GPIO_ALTERNATE;
	else
		return EC_ERROR_PARAM2;

	/* Update GPIO flags. */
	gpio_set_flags(gpio, flags);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND_FLAGS(gpiomode, command_gpio_mode,
			      "name <input | opendrain | pushpull | alternate>",
			      "Set a GPIO mode", CMD_FLAG_RESTRICTED);

/*
 * Set the weak pulling of a GPIO pin: up/down/none.
 */
static int command_gpio_pull_mode(int argc, const char **argv)
{
	int gpio;
	int flags;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	gpio = find_signal_by_name(argv[1]);
	if (gpio == GPIO_COUNT)
		return EC_ERROR_PARAM1;
	flags = gpio_get_flags(gpio);

	flags = flags & ~(GPIO_PULL_UP | GPIO_PULL_DOWN);
	if (strcasecmp(argv[2], "none") == 0)
		;
	else if (strcasecmp(argv[2], "up") == 0)
		flags |= GPIO_PULL_UP;
	else if (strcasecmp(argv[2], "down") == 0)
		flags |= GPIO_PULL_DOWN;
	else
		return EC_ERROR_PARAM2;

	/* Update GPIO flags. */
	gpio_set_flags(gpio, flags);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND_FLAGS(gpiopullmode, command_gpio_pull_mode,
			      "name <none | up | down>",
			      "Set a GPIO weak pull mode", CMD_FLAG_RESTRICTED);

/*
 * Find spi device by name or by number.  Returns an index into spi_devices[],
 * or on error a negative value.
 */
static int find_spi_by_name(const char *name)
{
	int i;
	char *e;
	i = strtoi(name, &e, 0);

	if (!*e && i < spi_devices_used)
		return i;

	for (i = 0; i < spi_devices_used; i++) {
		if (!strcasecmp(name, spi_devices[i].name))
			return i;
	}

	/* SPI device not found */
	return -1;
}

static void print_spi_info(int index)
{
	uint32_t bits_per_second;

	if (spi_devices[index].usb_flags & USB_SPI_CUSTOM_SPI_DEVICE) {
		// OCTOSPI as 8 bit prescaler, dividing clock by 1..256.
		bits_per_second = OCTOSPI_CLOCK / (spi_devices[index].div + 1);
	} else {
		// Other SPIs have prescaler by power of two 2, 4, 8, ..., 256.
		bits_per_second = SPI_CLOCK / (2 << spi_devices[index].div);
	}

	ccprintf("  %d %s %d bps\n", index, spi_devices[index].name,
		 bits_per_second);

	/* Flush console to avoid truncating output */
	cflush();
}

/*
 * Get information about one or all SPI ports.
 */
static int command_spi_info(int argc, const char **argv)
{
	int i;

	/* If a SPI target is specified, print only that one */
	if (argc == 3) {
		int index = find_spi_by_name(argv[2]);
		if (index < 0) {
			ccprintf("SPI device not found\n");
			return EC_ERROR_PARAM2;
		}

		print_spi_info(index);
		return EC_SUCCESS;
	}

	/* Otherwise print them all */
	for (i = 0; i < spi_devices_used; i++) {
		print_spi_info(i);
	}

	return EC_SUCCESS;
}

static int command_spi_set_speed(int argc, const char **argv)
{
	int index;
	uint32_t desired_speed;
	char *e;
	if (argc < 5)
		return EC_ERROR_PARAM_COUNT;

	index = find_spi_by_name(argv[3]);
	if (index < 0)
		return EC_ERROR_PARAM3;

	desired_speed = strtoi(argv[4], &e, 0);
	if (*e)
		return EC_ERROR_PARAM4;

	if (spi_devices[index].usb_flags & USB_SPI_CUSTOM_SPI_DEVICE) {
		/*
		 * Find prescaler value by division, rounding up in order to get
		 * slightly slower speed than requested, if it cannot be matched
		 * exactly.
		 */
		spi_devices[index].div =
			(OCTOSPI_CLOCK + desired_speed - 1) / desired_speed - 1;
		STM32_OCTOSPI_DCR2 = spi_devices[index].div;
	} else {
		int divisor = 7;
		/*
		 * Find the smallest divisor that result in a speed not faster
		 * than what was requested.
		 */
		while (divisor > 0) {
			if (SPI_CLOCK / (2 << (divisor - 1)) > desired_speed) {
				/* One step further would make the clock too
				 * fast, stop here. */
				break;
			}
			divisor--;
		}

		/*
		 * Re-initialize spi controller to apply the new clock divisor.
		 */
		spi_enable(&spi_devices[index], 0);
		spi_devices[index].div = divisor;
		spi_enable(&spi_devices[index], 1);
	}

	return EC_SUCCESS;
}

static int command_spi_set(int argc, const char **argv)
{
	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;
	if (!strcasecmp(argv[2], "speed"))
		return command_spi_set_speed(argc, argv);
	return EC_ERROR_PARAM2;
}

static int command_spi(int argc, const char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;
	if (!strcasecmp(argv[1], "info"))
		return command_spi_info(argc, argv);
	if (!strcasecmp(argv[1], "set"))
		return command_spi_set(argc, argv);
	return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND_FLAGS(spi, command_spi,
			      "info [PORT]"
			      "\nset speed PORT BPS",
			      "SPI bus manipulation", CMD_FLAG_RESTRICTED);
