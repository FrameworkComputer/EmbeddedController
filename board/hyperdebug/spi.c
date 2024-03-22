/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* HyperDebug SPI logic and console commands */

#include "board_util.h"
#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "console.h"
#include "dma.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "spi.h"
#include "stm32-dma.h"
#include "timer.h"
#include "usb_spi.h"
#include "util.h"

/*
 * List of SPI devices that can be controlled via USB.
 *
 * SPI1 and SPI2 use PCLK (27.5 MHz) as base frequency.
 * QSPI uses either SYSCLK (110 MHz) or MSI (variable) as base frequency.
 *
 * Divisors below result in default SPI clock of approx. 430 kHz for all
 */
struct spi_device_t spi_devices[] = {
	{ .name = "SPI2",
	  .port = 1,
	  .div = 5,
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
	  .div = 5,
	  .gpio_cs = GPIO_CN7_4,
	  .usb_flags = USB_SPI_ENABLED },
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

static int spi_device_default_gpio_cs[ARRAY_SIZE(spi_devices)];
static int spi_device_default_div[ARRAY_SIZE(spi_devices)];

static const size_t NUM_MSI_FREQUENCIES = 12;

/*
 * List of possible base frequencies for the OCTOSPI controller, the first
 * NUM_MSI_FREQUENCIES entries correspond to the options of the MSI oscillator.
 * The last one is SYSCLK, and will be dynamically populated.
 */
static uint32_t base_frequencies[13] = {
	100000,	 200000,   400000,   800000,   1000000,	 2000000,    4000000,
	8000000, 16000000, 24000000, 32000000, 48000000, 0xFFFFFFFF,
};

uint32_t octospi_clock(void)
{
	switch (STM32_RCC_CCIPR2 & STM32_RCC_CCIPR2_OSPISEL_MSK) {
	case STM32_RCC_CCIPR2_OSPISEL_SYSCLK:
		return clock_get_freq();
	case STM32_RCC_CCIPR2_OSPISEL_MSI: {
		size_t msi_freq = (STM32_RCC_CR & STM32_RCC_CR_MSIRANGE_MSK) >>
				  STM32_RCC_CR_MSIRANGE_POS;
		if (msi_freq < NUM_MSI_FREQUENCIES)
			return base_frequencies[msi_freq];
		return 0;
	}
	default:
		return 0;
	}
}

uint32_t spi_clock(void)
{
	return clock_get_apb_freq();
}

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
		bits_per_second =
			octospi_clock() / (spi_devices[index].div + 1);
	} else {
		// Other SPIs have prescaler by power of two 2, 4, 8, ..., 256.
		bits_per_second = spi_clock() / (2 << spi_devices[index].div);
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
		/* Turn off MSI oscillator (in order to allow modification). */
		STM32_RCC_CR &= ~STM32_RCC_CR_MSION;

		/*
		 * Find prescaler value by division, rounding up in order to get
		 * slightly slower speed than requested, if it cannot be matched
		 * exactly.
		 *
		 * The OCTOSPI peripheral can derive clock from either SYSCLK
		 * (110 MHz) or the variable MSI, attempt calculation with all
		 * possible frequencies, and see which one gets closest to the
		 * requested frequency, without exceeding it.
		 */
		uint8_t best_divisor;
		size_t best_base_frequency_index;

		/* Populate current SYSCLK. */
		base_frequencies[NUM_MSI_FREQUENCIES] = clock_get_freq();

		find_best_divisor(desired_speed, base_frequencies,
				  NUM_MSI_FREQUENCIES + 1, &best_divisor,
				  &best_base_frequency_index);

		if (best_base_frequency_index < NUM_MSI_FREQUENCIES) {
			/*
			 * Either the requested SPI clock frequency is too slow
			 * for SYSCLK source, or the MSI source would be able to
			 * get closer to the requested frequency.  Select MSI as
			 * OCTOSPI clock source
			 */

			/* Select MSI frequency */
			STM32_RCC_CR =
				(STM32_RCC_CR & ~STM32_RCC_CR_MSIRANGE_MSK) |
				(best_base_frequency_index
				 << STM32_RCC_CR_MSIRANGE_POS) |
				STM32_RCC_CR_MSIRGSEL;

			/* Enable MSI and wait for MSI to be ready */
			wait_for_ready(&STM32_RCC_CR, STM32_RCC_CR_MSION,
				       STM32_RCC_CR_MSIRDY);

			/* Choose MSI as clock source for OCTOSPI */
			STM32_RCC_CCIPR2 = (STM32_RCC_CCIPR2 &
					    ~STM32_RCC_CCIPR2_OSPISEL_MSK) |
					   STM32_RCC_CCIPR2_OSPISEL_MSI;
		} else {
			/*
			 * The SYSCLK source is able to get closer to the
			 * requested SPI clock frequency, select SYSCLK as
			 * OCTOSPI clock source
			 */
			STM32_RCC_CCIPR2 = (STM32_RCC_CCIPR2 &
					    ~STM32_RCC_CCIPR2_OSPISEL_MSK) |
					   STM32_RCC_CCIPR2_OSPISEL_SYSCLK;
		}
		STM32_OCTOSPI_DCR2 = spi_devices[index].div = best_divisor;
	} else {
		int divisor = 7;
		/*
		 * Find the smallest divisor that result in a speed not faster
		 * than what was requested.
		 */
		while (divisor > 0) {
			if (spi_clock() / (2 << (divisor - 1)) >
			    desired_speed) {
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

	/*
	 * Bring OCTOSPI block out of reset.
	 */
	deadline.val = get_time().val + OCTOSPI_INIT_TIMEOUT_US;
	STM32_RCC_AHB3RSTR |= STM32_RCC_AHB3RSTR_QSPIRST;
	STM32_RCC_AHB3RSTR &= ~STM32_RCC_AHB3RSTR_QSPIRST;
	while (STM32_OCTOSPI_SR & STM32_OCTOSPI_SR_BUSY) {
		timestamp_t now = get_time();
		if (timestamp_expired(deadline, &now))
			return EC_ERROR_TIMEOUT;
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
		} else if (!txlen) {
			/*
			 * Receive-only transaction. Not supported by STM32L552,
			 * as described in ST document ES0448:
			 * "STM32L552xx/562xx device errata" in the section
			 * 2.4.12: "Data not sampled correctly on reads without
			 * DQS and with less than two cycles before the data
			 * phase".
			 */
			cprints(CC_SPI,
				"Receive-only transaction not supported by OctoSPI hardware");
			return EC_ERROR_UNIMPLEMENTED;
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

	/*
	 * Put OCTOSPI block into reset, to ensure that no state carries over to
	 * next transaction.
	 */
	STM32_RCC_AHB3RSTR |= STM32_RCC_AHB3RSTR_QSPIRST;
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

/* Reconfigure SPI ports to power-on default values. */
static void spi_reinit(void)
{
	for (unsigned int i = 0; i < spi_devices_used; i++) {
		if (spi_devices[i].usb_flags & USB_SPI_CUSTOM_SPI_DEVICE) {
			/* Quad SPI controller */
			spi_devices[i].gpio_cs = spi_device_default_gpio_cs[i];
			spi_devices[i].div = spi_device_default_div[i];
			/* Select SYSCLK clock source */
			STM32_RCC_CCIPR2 = (STM32_RCC_CCIPR2 &
					    ~STM32_RCC_CCIPR2_OSPISEL_MSK) |
					   STM32_RCC_CCIPR2_OSPISEL_SYSCLK;

		} else {
			/* "Ordinary" SPI controller */
			spi_enable(&spi_devices[i], 0);
			spi_devices[i].gpio_cs = spi_device_default_gpio_cs[i];
			spi_devices[i].div = spi_device_default_div[i];
			spi_enable(&spi_devices[i], 1);
		}
	}
}
DECLARE_HOOK(HOOK_REINIT, spi_reinit, HOOK_PRIO_DEFAULT);

/* Initialize board for SPI. */
static void spi_init(void)
{
	/* Record initial values for use by `spi_reinit()` above. */
	for (unsigned int i = 0; i < spi_devices_used; i++) {
		spi_device_default_gpio_cs[i] = spi_devices[i].gpio_cs;
		spi_device_default_div[i] = spi_devices[i].div;
	}

	/* Structured endpoints */
	usb_spi_enable(1);

	/* Configure SPI GPIOs */
	gpio_config_module(MODULE_SPI, 1);

	/*
	 * Unlike most SPI, I2C and UARTs, which are configured in their
	 * alternate mode by default, SPI1 pins are in GPIO input mode on
	 * HyperDebug power-on, for compatibility with previous firmwares.  In
	 * the future we may decide to leave even more functions off by default,
	 * in order for HyperDebug to actively drive as little at possible on
	 * boot.  It is relatively straightforward to declare pins as "Alternate
	 * mode" in opentitantool json configuration file, to have them enabled
	 * by "transport init".
	 *
	 * The code below sets up the alternate function "number" for the
	 * relevant pins, such that when alternate mode is enabled on the pins,
	 * the result is the particular alternate function that HyperDebug
	 * firmware has chosen for the pin.
	 */
	STM32_GPIO_AFRL(STM32_GPIOA_BASE) |= 0x55000000; /* SPI1: PA6/PA7
							    HIDO/HODI */
	STM32_GPIO_AFRL(STM32_GPIOB_BASE) |= 0x00005000; /* SPI1: PB3 SCK */

	/*
	 * Enable SPI1.
	 */

	/* Enable clocks to SPI1 module */
	STM32_RCC_APB2ENR |= STM32_RCC_APB2ENR_SPI1EN;

	/* Reset SPI1 */
	STM32_RCC_APB2RSTR |= STM32_RCC_APB2RSTR_SPI1RST;
	STM32_RCC_APB2RSTR &= ~STM32_RCC_APB2RSTR_SPI1RST;

	spi_enable(&spi_devices[2], 1);

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
	 * Enable OCTOSPI clock, but keep the block under reset.  Will be
	 * brought out of reset only when needed.
	 */
	STM32_RCC_AHB3RSTR |= STM32_RCC_AHB3RSTR_QSPIRST;
	STM32_RCC_AHB3ENR |= STM32_RCC_AHB3ENR_QSPIEN;

	/* Turn off MSI, not used initially. */
	STM32_RCC_CR &= ~STM32_RCC_CR_MSION;

	/* Select DMA channel */
	dma_select_channel(STM32_DMAC_CH13, DMAMUX_REQ_OCTOSPI1);
}
DECLARE_HOOK(HOOK_INIT, spi_init, HOOK_PRIO_DEFAULT + 1);
