/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* HyperDebug SPI logic and console commands */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "registers.h"
#include "spi.h"
#include "timer.h"
#include "usb_spi.h"
#include "util.h"

#define OCTOSPI_CLOCK (CPU_CLOCK)
#define SPI_CLOCK (CPU_CLOCK)

/* SPI devices, default to 406 kb/s for all. */
struct spi_device_t spi_devices[] = {
	{ .name = "SPI2",
	  .port = 1,
	  .div = 7,
	  .gpio_cs = GPIO_CN9_25,
	  .usb_flags = USB_SPI_ENABLED },
	{ .name = "QSPI",
	  .port = -1 /* OCTOSPI */,
	  .div = 255,
	  .gpio_cs = GPIO_CN10_6,
	  .usb_flags = USB_SPI_ENABLED | USB_SPI_CUSTOM_SPI_DEVICE |
		       USB_SPI_FLASH_DUAL_SUPPORT |
		       USB_SPI_FLASH_QUAD_SUPPORT },
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

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
		int divisor =
			(OCTOSPI_CLOCK + desired_speed - 1) / desired_speed - 1;
		if (divisor >= 256)
			divisor = 255;
		STM32_OCTOSPI_DCR2 = spi_devices[index].div = divisor;
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

/******************************************************************************
 * OCTOSPI driver.
 */

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
 * Board-specific SPI driver entry point, called by usb_spi.c.
 */
void usb_spi_board_enable(struct usb_spi_config const *config)
{
	/* All initialization already done in board_init(). */
}

void usb_spi_board_disable(struct usb_spi_config const *config)
{
}

/*
 * Board-specific SPI driver entry point, called by usb_spi.c.  On this board,
 * the only spi device declared as requiring board specific driver is OCTOSPI.
 */
int usb_spi_board_transaction(const struct spi_device_t *spi_device,
			      uint32_t flash_flags, const uint8_t *txdata,
			      int txlen, uint8_t *rxdata, int rxlen)
{
	int rv = EC_SUCCESS;
	bool previous_cs;

	if (flash_flags & FLASH_FLAGS_REQUIRING_SUPPORT)
		return EC_ERROR_UNIMPLEMENTED;

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
