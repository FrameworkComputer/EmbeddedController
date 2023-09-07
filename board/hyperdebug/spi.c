/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* HyperDebug SPI logic and console commands */

#include "common.h"
#include "console.h"
#include "dma.h"
#include "gpio.h"
#include "registers.h"
#include "spi.h"
#include "stm32-dma.h"
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
		       USB_SPI_FLASH_DUAL_SUPPORT | USB_SPI_FLASH_QUAD_SUPPORT |
		       USB_SPI_FLASH_DTR_SUPPORT },
	{ .name = "SPI1",
	  .port = 0,
	  .div = 7,
	  .gpio_cs = GPIO_CN7_4,
	  .usb_flags = USB_SPI_ENABLED },
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

static int spi_device_default_gpio_cs[ARRAY_SIZE(spi_devices)] = {
	GPIO_CN9_25,
	GPIO_CN10_6,
};

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

static int command_spi_set_cs(int argc, const char **argv)
{
	int index;
	int desired_gpio_cs;
	if (argc < 5)
		return EC_ERROR_PARAM_COUNT;

	index = find_spi_by_name(argv[3]);
	if (index < 0)
		return EC_ERROR_PARAM3;

	if (!strcasecmp(argv[4], "default")) {
		desired_gpio_cs = spi_device_default_gpio_cs[index];
	} else {
		desired_gpio_cs = gpio_find_by_name(argv[4]);
		if (desired_gpio_cs == GPIO_COUNT)
			return EC_ERROR_PARAM4;
	}

	spi_devices[index].gpio_cs = desired_gpio_cs;

	return EC_SUCCESS;
}

static int command_spi_set(int argc, const char **argv)
{
	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;
	if (!strcasecmp(argv[2], "speed"))
		return command_spi_set_speed(argc, argv);
	if (!strcasecmp(argv[2], "cs"))
		return command_spi_set_cs(argc, argv);
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
			      "\nset speed PORT BPS"
			      "\nset cs PORT PIN",
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
 * Board-specific SPI driver entry point, called by usb_spi.c.
 */
void usb_spi_board_enable(void)
{
	/* All initialization already done in board_init(). */
}

void usb_spi_board_disable(void)
{
}

static const struct dma_option dma_octospi_option = {
	.channel = STM32_DMAC_CH13,
	.periph = (void *)&STM32_OCTOSPI_DR,
	.flags = STM32_DMA_CCR_MSIZE_8_BIT | STM32_DMA_CCR_PSIZE_8_BIT,
};

static bool previous_cs;
static timestamp_t deadline;

/*
 * Board-specific SPI driver entry point, called by usb_spi.c.  On this board,
 * the only spi device declared as requiring board specific driver is OCTOSPI.
 *
 * Actually, there is nothing board-specific in the code below, as it merely
 * implements the serial flash USB protocol extensions on the OctoSPI hardware
 * found on STM32L5 chips (and probably others).  For now, however, HyperDebug
 * is the only board that uses a chip with such an OctoSPI controller, so until
 * we have seen this code being generally useful, it will live in
 * board/hyperdebug.
 */
int usb_spi_board_transaction_async(const struct spi_device_t *spi_device,
				    uint32_t flash_flags, const uint8_t *txdata,
				    int txlen, uint8_t *rxdata, int rxlen)
{
	uint32_t opcode = 0, address = 0;
	const uint32_t mode = flash_flags & FLASH_FLAG_MODE_MSK;
	const uint8_t width = (flash_flags & FLASH_FLAG_WIDTH_MSK) >>
			      FLASH_FLAG_WIDTH_POS;
	uint8_t opcode_len = (flash_flags & FLASH_FLAG_OPCODE_LEN_MSK) >>
			     FLASH_FLAG_OPCODE_LEN_POS;
	uint8_t addr_len = (flash_flags & FLASH_FLAG_ADDR_LEN_MSK) >>
			   FLASH_FLAG_ADDR_LEN_POS;
	const uint8_t dummy_cycles =
		(flash_flags & FLASH_FLAG_DUMMY_CYCLES_MSK) >>
		FLASH_FLAG_DUMMY_CYCLES_POS;
	uint32_t data_len;
	uint32_t control_value = 0;

	if (!flash_flags) {
		/*
		 * This is a request does not use the extended format with SPI
		 * flash flags, but is for doing plain write-then-read of single
		 * lane (COPI/CIPO) SPI data.
		 */
		if (rxlen == SPI_READBACK_ALL) {
			cprints(CC_SPI,
				"Full duplex not supported by OctoSPI hardware");
			return EC_ERROR_UNIMPLEMENTED;
		} else if (!rxlen && !txlen) {
			/* No operation requested, done. */
			return EC_SUCCESS;
		} else if (!rxlen) {
			/*
			 * Transmit-only transaction.  This is implemented by
			 * not using any of the up to 12 bytes of instructions,
			 * but as all "data".
			 */
			flash_flags |= FLASH_FLAG_READ_WRITE_WRITE;
		} else if (txlen <= 12) {
			/*
			 * Sending of up to 12 bytes, followed by reading a
			 * possibly large number of bytes.  This is implemented
			 * by a "read" transaction using the instruction and
			 * address feature of OctoSPI.
			 */
			if (txlen <= 4) {
				opcode_len = txlen;
			} else {
				opcode_len = 4;
				addr_len = txlen - 4;
			}
		} else {
			/*
			 * Sending many bytes, followed by reading.  This would
			 * have to be implemented as two separate OctoSPI
			 * transactions.
			 */
			cprints(CC_SPI,
				"General write-then-read not supported by OctoSPI hardware");
			return EC_ERROR_UNIMPLEMENTED;
		}
	}

	previous_cs = gpio_get_level(spi_device->gpio_cs);

	/* Drive chip select low */
	gpio_set_level(spi_device->gpio_cs, 0);

	/* Deadline on the entire SPI transaction. */
	deadline.val = get_time().val + OCTOSPI_TRANSACTION_TIMEOUT_US;

	if ((flash_flags & FLASH_FLAG_READ_WRITE_MSK) ==
	    FLASH_FLAG_READ_WRITE_WRITE) {
		data_len = txlen - opcode_len - addr_len;
		/* Enable OCTOSPI, indirect write mode. */
		STM32_OCTOSPI_CR = STM32_OCTOSPI_CR_FMODE_IND_WRITE |
				   STM32_OCTOSPI_CR_DMAEN | STM32_OCTOSPI_CR_EN;
	} else {
		data_len = rxlen;
		/* Enable OCTOSPI, indirect read mode. */
		STM32_OCTOSPI_CR = STM32_OCTOSPI_CR_FMODE_IND_READ |
				   STM32_OCTOSPI_CR_DMAEN | STM32_OCTOSPI_CR_EN;
	}

	/* Clear completion flag from last transaction. */
	STM32_OCTOSPI_FCR = STM32_OCTOSPI_FCR_CTCF;

	/* Data length. */
	STM32_OCTOSPI_DLR = data_len - 1;

	/*
	 * Set up the number of bytes and data width of each of the opcode,
	 * address, and "alternate" stages of the initial command bytes.
	 */
	if (opcode_len == 0) {
		control_value |= STM32_OCTOSPI_CCR_IMODE_NONE;
	} else {
		control_value |= (opcode_len - 1)
				 << STM32_OCTOSPI_CCR_ISIZE_POS;
		if (mode < FLASH_FLAG_MODE_NNN) {
			// Opcode phase is single-lane
			control_value |= 1 << STM32_OCTOSPI_CCR_IMODE_POS;
		} else {
			// Opcode phase is multi-lane
			control_value |= (width + 1)
					 << STM32_OCTOSPI_CCR_IMODE_POS;
			if (flash_flags & FLASH_FLAG_DTR)
				control_value |= STM32_OCTOSPI_CCR_IDTR;
		}
		for (int i = 0; i < opcode_len; i++) {
			opcode <<= 8;
			opcode |= *txdata++;
			txlen--;
		}
	}
	if (addr_len == 0) {
		control_value |= STM32_OCTOSPI_CCR_ADMODE_NONE;
		control_value |= STM32_OCTOSPI_CCR_ABMODE_NONE;
	} else if (addr_len <= 4) {
		control_value |= (addr_len - 1) << STM32_OCTOSPI_CCR_ADSIZE_POS;
		if (mode < FLASH_FLAG_MODE_1NN) {
			// Address phase is single-lane
			control_value |= 1 << STM32_OCTOSPI_CCR_ADMODE_POS;
		} else {
			// Address phase is multi-lane
			control_value |= (width + 1)
					 << STM32_OCTOSPI_CCR_ADMODE_POS;
			if (flash_flags & FLASH_FLAG_DTR)
				control_value |= STM32_OCTOSPI_CCR_ADDTR;
		}
		for (int i = 0; i < addr_len; i++) {
			address <<= 8;
			address |= *txdata++;
			txlen--;
		}
		control_value |= STM32_OCTOSPI_CCR_ABMODE_NONE;
	} else {
		uint32_t alternate = 0;
		control_value |= 3 << STM32_OCTOSPI_CCR_ADSIZE_POS;
		control_value |= (addr_len - 5) << STM32_OCTOSPI_CCR_ABSIZE_POS;
		if (mode < FLASH_FLAG_MODE_1NN) {
			// Address phase is single-lane
			control_value |= 1 << STM32_OCTOSPI_CCR_ADMODE_POS;
			control_value |= 1 << STM32_OCTOSPI_CCR_ABMODE_POS;
		} else {
			// Address phase is multi-lane
			control_value |= (width + 1)
					 << STM32_OCTOSPI_CCR_ADMODE_POS;
			control_value |= (width + 1)
					 << STM32_OCTOSPI_CCR_ABMODE_POS;
			if (flash_flags & FLASH_FLAG_DTR)
				control_value |= STM32_OCTOSPI_CCR_ADDTR |
						 STM32_OCTOSPI_CCR_ABDTR;
		}
		for (int i = 0; i < 4; i++) {
			address <<= 8;
			address |= *txdata++;
			txlen--;
		}
		for (int i = 0; i < addr_len - 4; i++) {
			alternate <<= 8;
			alternate |= *txdata++;
			txlen--;
		}
		STM32_OCTOSPI_ABR = alternate;
	}
	/* Set up how many bytes to read/write after the initial command. */
	if (data_len == 0) {
		control_value |= STM32_OCTOSPI_CCR_DMODE_NONE;
	} else {
		if (mode < FLASH_FLAG_MODE_11N) {
			// Data phase is single-lane
			control_value |= 1 << STM32_OCTOSPI_CCR_DMODE_POS;
		} else {
			// Data phase is multi-lane
			control_value |= (width + 1)
					 << STM32_OCTOSPI_CCR_DMODE_POS;
			if (flash_flags & FLASH_FLAG_DTR)
				control_value |= STM32_OCTOSPI_CCR_DDTR;
		}
	}

	/* Dummy cycles. */
	STM32_OCTOSPI_TCR = dummy_cycles << STM32_OCTOSPI_TCR_DCYC_POS;

	STM32_OCTOSPI_CCR = control_value;

	/* Set instruction and address registers, triggering the start of the
	 * write+read transaction. */
	STM32_OCTOSPI_IR = opcode;
	STM32_OCTOSPI_AR = address;

	if ((flash_flags & FLASH_FLAG_READ_WRITE_MSK) ==
	    FLASH_FLAG_READ_WRITE_WRITE) {
		if (txlen > 0) {
			dma_chan_t *txdma = dma_get_channel(STM32_DMAC_CH13);
			dma_prepare_tx(&dma_octospi_option, txlen, txdata);
			dma_go(txdma);
		}
	} else {
		if (rxlen > 0) {
			dma_start_rx(&dma_octospi_option, rxlen, rxdata);
		}
	}

	return EC_SUCCESS;
}

int usb_spi_board_transaction_is_complete(const struct spi_device_t *spi_device)
{
	/* Query the "transaction complete flag" of the status register. */
	return STM32_OCTOSPI_SR & STM32_OCTOSPI_SR_TCF;
}

int usb_spi_board_transaction_flush(const struct spi_device_t *spi_device)
{
	/*
	 * Wait until DMA transfer is complete (no-op if DMA not started because
	 * of zero-length transfer).
	 */
	int rv = dma_wait(STM32_DMAC_CH13);
	dma_disable(STM32_DMAC_CH13);
	if (rv != EC_SUCCESS)
		return rv;

	/*
	 * Ensure that all bits of the last byte has been shifted onto the SPI
	 * bus.
	 */
	rv = octospi_wait_for(STM32_OCTOSPI_SR_TCF, deadline);

	/* Return chip select to previous level. */
	gpio_set_level(spi_device->gpio_cs, previous_cs);

	return rv;
}

int usb_spi_board_transaction(const struct spi_device_t *spi_device,
			      uint32_t flash_flags, const uint8_t *txdata,
			      int txlen, uint8_t *rxdata, int rxlen)
{
	int rv = usb_spi_board_transaction_async(spi_device, flash_flags,
						 txdata, txlen, rxdata, rxlen);
	if (rv == EC_SUCCESS) {
		rv = usb_spi_board_transaction_flush(spi_device);
	}
	return rv;
}
