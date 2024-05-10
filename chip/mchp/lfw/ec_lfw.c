/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MCHP MEC SoC little FW
 *
 */

#include "clock.h"
#include "config.h"
#include "cpu.h"
#include "cros_version.h"
#include "dma.h"
#include "gpio.h"
#include "hwtimer.h"
#include "registers.h"
#include "spi.h"
#include "spi_flash.h"
#include "system.h"
#include "tfdp_chip.h"
#include "timer.h"
#include "util.h"

#include <stdint.h>

#ifdef CONFIG_MCHP_LFW_DEBUG
#include "dma_chip.h"
#endif

#include "ec_lfw.h"

/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/*
 * Check if LFW build is pulling in GPSPI which is not
 * used for EC firmware SPI flash access.
 */
#ifdef CONFIG_MCHP_GPSPI
#error "FORCED BUILD ERROR: CONFIG_MCHP_GPSPI is defined"
#endif

#define LFW_SPI_BYTE_TRANSFER_TIMEOUT_US (1 * MSEC)
#define LFW_SPI_BYTE_TRANSFER_POLL_INTERVAL_US 100

__attribute__((section(".intvector")))
const struct int_vector_t hdr_int_vect = {
	/* init sp, unused. set by MEC ROM loader */
	(void *)lfw_stack_top, /* preserve ROM log. was (void *)0x11FA00, */
	&lfw_main,
	/* was &lfw_main, */ /* reset vector */
	&fault_handler, /* NMI handler */
	&fault_handler, /* HardFault handler */
	&fault_handler, /* MPU fault handler */
	&fault_handler /* Bus fault handler */
};

/* SPI devices - from board.c */
const struct spi_device_t spi_devices[] = {
	{ CONFIG_SPI_FLASH_PORT, 4, GPIO_QMSPI_CS0 },
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/*
 * At POR or EC reset MCHP Boot-ROM should only load LFW and jumps
 * into LFW entry point located at offset 0x04 of LFW.
 * Entry point is programmed into SPI Header by Python SPI image
 * builder in chip/mchp/util.
 *
 * EC_RO/RW calling LFW should enter through this routine if you
 * want the vector table updated. The stack should be set to
 * LFW linker file parameter lfw_stack_top because we do not
 * know if the callers stack is OK.
 *
 * Make sure lfw_stack_top will not overwrite panic data!
 * from include/panic.h
 * Panic data goes at the end of RAM. This is safe because we don't
 * context switch away from the panic handler before rebooting,
 * and stacks and data start at the beginning of RAM.
 *
 * chip level config_chip.h
 * #define CONFIG_RAM_SIZE 0x00008000
 * #define CONFIG_RAM_BASE 0x120000 - 0x8000 = 0x118000
 *
 *  #define PANIC_DATA_PTR ((struct panic_data *)\
 *	(CONFIG_RAM_BASE + CONFIG_RAM_SIZE - sizeof(struct panic_data)))
 *
 * LFW stack located by ec_lfw.ld linker file 256 bytes below top of
 * data SRAM.
 * PROVIDE( lfw_stack_top = 0x11F000 );
 *
 * !!!WARNING!!!
 * Current MEC BootROM's zeros all memory therefore any chip reset
 * will destroy panic data.
 */

/*
 * Configure 32-bit basic timer 0 for 1MHz, auto-reload and
 * no interrupt.
 */
void timer_init(void)
{
	uint32_t val = 0;

	/* Ensure timer is not running */
	MCHP_TMR32_CTL(0) &= ~BIT(5);

	/* Enable timer */
	MCHP_TMR32_CTL(0) |= BIT(0);

	val = MCHP_TMR32_CTL(0);

	/* Prescale = 48 -> 1MHz -> Period = 1 us */
	val = (val & 0xffff) | (47 << 16);

	MCHP_TMR32_CTL(0) = val;

	/* Set preload to use the full 32 bits of the timer */
	MCHP_TMR32_PRE(0) = 0xffffffff;

	/* Override the count */
	MCHP_TMR32_CNT(0) = 0xffffffff;

	/* Auto restart */
	MCHP_TMR32_CTL(0) |= BIT(3);

	/* Start counting in timer 0 */
	MCHP_TMR32_CTL(0) |= BIT(5);
}

/*
 * Use copy of SPI flash read compiled for LFW (no semaphores).
 * LFW timeout code does not use interrupts so reset timer
 * before starting SPI read to minimize probability of
 * timer wrap.
 */
static int spi_flash_readloc(uint8_t *buf_usr, unsigned int offset,
			     unsigned int bytes)
{
	uint8_t cmd[4] = { SPI_FLASH_READ, (offset >> 16) & 0xFF,
			   (offset >> 8) & 0xFF, offset & 0xFF };

	if (offset + bytes > CONFIG_FLASH_SIZE_BYTES)
		return EC_ERROR_INVAL;

	__hw_clock_source_set(0); /* restart free run timer */
	return spi_transaction(SPI_FLASH_DEVICE, cmd, 4, buf_usr, bytes);
}

/*
 * Load EC_RO/RW image from local SPI flash.
 * If CONFIG_MEC_TEST_EC_RORW_CRC was define the last 4 bytes
 * of the binary is IEEE 802.3 CRC32 of the previous bytes.
 * Use DMA channel 0 CRC32 HW to check data integrity.
 */
int spi_image_load(uint32_t offset)
{
	uint8_t *buf =
		(uint8_t *)(CONFIG_RW_MEM_OFF + CONFIG_PROGRAM_MEMORY_BASE);
	uint32_t i;
#ifdef CONFIG_MCHP_LFW_DEBUG
	uint32_t crc_calc, crc_exp;
	int rc;
#endif

	BUILD_ASSERT(CONFIG_RO_SIZE == CONFIG_RW_SIZE);

	/* Why fill all but last 4-bytes? */
	memset((void *)buf, 0xFF, (CONFIG_RO_SIZE - 4));

	for (i = 0; i < CONFIG_RO_SIZE; i += SPI_CHUNK_SIZE)
#ifdef CONFIG_MCHP_LFW_DEBUG
		rc = spi_flash_readloc(&buf[i], offset + i, SPI_CHUNK_SIZE);
	if (rc != EC_SUCCESS) {
		trace2(0, LFW, 0, "spi_flash_readloc block %d ret = %d", i, rc);
		while (MCHP_PCR_PROC_CLK_CTL)
			MCHP_PCR_CHIP_OSC_ID &= 0x1FE;
	}
#else
		spi_flash_readloc(&buf[i], offset + i, SPI_CHUNK_SIZE);
#endif

#ifdef CONFIG_MCHP_LFW_DEBUG
	dma_crc32_start(buf, (CONFIG_RO_SIZE - 4), 0);
	do {
		MCHP_USEC_DELAY(31); /* delay(stall) CPU by 32 us */
		i = dma_is_done_chan(0);
	} while (i == 0);
	crc_calc = MCHP_DMA_CH0_CRC32_DATA;
	crc_exp = *((uint32_t *)&buf[CONFIG_RO_SIZE - 4]);
	trace12(0, LFW, 0, "EC image CRC32 = 0x%08x  expected = 0x%08x",
		crc_calc, crc_exp);
#endif

	return 0;
}

void udelay(unsigned int us)
{
	uint32_t t0 = __hw_clock_source_read();

	while (__hw_clock_source_read() - t0 < us)
		;
}

int crec_usleep(unsigned int us)
{
	udelay(us);
	return 0;
}

int timestamp_expired(timestamp_t deadline, const timestamp_t *now)
{
	timestamp_t now_val;

	if (!now) {
		now_val = get_time();
		now = &now_val;
	}

	return ((int64_t)(now->val - deadline.val) >= 0);
}

/*
 * LFW does not use interrupts so no ISR will fire to
 * increment high 32-bits of timestap_t. Force high
 * word to zero. NOTE: There is a risk of false timeout
 * errors due to timer wrap. We will reset timer before
 * each SPI transaction.
 */
timestamp_t get_time(void)
{
	timestamp_t ts;

	ts.le.hi = 0; /* clksrc_high; */
	ts.le.lo = __hw_clock_source_read();
	return ts;
}

#ifdef CONFIG_UART_CONSOLE

BUILD_ASSERT(CONFIG_UART_CONSOLE < MCHP_UART_INSTANCES);

void uart_write_c(char c)
{
	/* Put in carriage return prior to newline to mimic uart_vprintf() */
	if (c == '\n')
		uart_write_c('\r');

	/* Wait for space in transmit FIFO. */
	while (!(MCHP_UART_LSR(CONFIG_UART_CONSOLE) & BIT(5)))
		;
	MCHP_UART_TB(CONFIG_UART_CONSOLE) = c;
}

void uart_puts(const char *str)
{
	if (!str || !*str)
		return;

	do {
		uart_write_c(*str++);
	} while (*str);
}

void uart_init(void)
{
	/* Set UART to reset on VCC1_RESET instead of nSIO_RESET */
	MCHP_UART_CFG(CONFIG_UART_CONSOLE) &= ~BIT(1);

	/* Baud rate = 115200. 1.8432MHz clock. Divisor = 1 */

	/* Set CLK_SRC = 0 */
	MCHP_UART_CFG(CONFIG_UART_CONSOLE) &= ~BIT(0);

	/* Set DLAB = 1 */
	MCHP_UART_LCR(CONFIG_UART_CONSOLE) |= BIT(7);

	/* PBRG0/PBRG1 */
	MCHP_UART_PBRG0(CONFIG_UART_CONSOLE) = 1;
	MCHP_UART_PBRG1(CONFIG_UART_CONSOLE) = 0;

	/* Set DLAB = 0 */
	MCHP_UART_LCR(CONFIG_UART_CONSOLE) &= ~BIT(7);

	/* Set word length to 8-bit */
	MCHP_UART_LCR(CONFIG_UART_CONSOLE) |= BIT(0) | BIT(1);

	/* Enable FIFO */
	MCHP_UART_FCR(CONFIG_UART_CONSOLE) = BIT(0);

	/* Activate UART */
	MCHP_UART_ACT(CONFIG_UART_CONSOLE) |= BIT(0);

	gpio_config_module(MODULE_UART, 1);
}
#else
void uart_write_c(char c __attribute__((unused)))
{
}

void uart_puts(const char *str __attribute__((unused)))
{
}

void uart_init(void)
{
}
#endif /* #ifdef CONFIG_UART_CONSOLE */

__noreturn void watchdog_reset(void)
{
	uart_puts("EXCEPTION!\nTriggering watchdog reset\n");
	/* trigger reset in 1 ms */
	crec_usleep(1000);
	MCHP_PCR_SYS_RST = MCHP_PCR_SYS_SOFT_RESET;
	while (1)
		;
}

void fault_handler(void)
{
	asm("b watchdog_reset");
}

void jump_to_image(uintptr_t init_addr)
{
	void (*resetvec)(void) = (void (*)(void))init_addr;

	resetvec();
}

/*
 * If any of VTR POR, VBAT POR, chip resets, or WDT reset are active
 * force VBAT image type to none causing load of EC_RO.
 */
void system_init(void)
{
	uint32_t wdt_sts = MCHP_VBAT_STS & MCHP_VBAT_STS_ANY_RST;
	uint32_t rst_sts = MCHP_PCR_PWR_RST_STS & MCHP_PWR_RST_STS_SYS;

	trace12(0, LFW, 0, "VBAT_STS = 0x%08x  PCR_PWR_RST_STS = 0x%08x",
		wdt_sts, rst_sts);

	if (rst_sts || wdt_sts)
		MCHP_VBAT_RAM(MCHP_IMAGETYPE_IDX) = EC_IMAGE_UNKNOWN;
}

enum ec_image system_get_image_copy(void)
{
	return MCHP_VBAT_RAM(MCHP_IMAGETYPE_IDX);
}

/*
 * lfw_main is entered by MEC BootROM or EC_RO/RW calling it directly.
 * NOTE: Based on LFW from MEC1322
 * Upon chip reset, BootROM loads image = LFW+EC_RO and enters LFW.
 * LFW checks reset type:
 *   VTR POR, chip reset, WDT reset then set VBAT Load type to Unknown.
 * LFW reads VBAT Load type:
 *   EC_IMAGE_RO then read EC_RO from SPI flash and jump into it.
 *   EC_IMAGE_RO then read EC_RW from SPI flash and jump into it.
 *   Other then jump into EC image loaded by Boot-ROM.
 */
void lfw_main(void)
{
	uintptr_t init_addr;

	/* install vector table */
	*((uintptr_t *)0xe000ed08) = (uintptr_t)&hdr_int_vect;

	/* Use 48 MHz processor clock to power through boot */
	MCHP_PCR_PROC_CLK_CTL = 1;

#ifdef CONFIG_WATCHDOG
	/* Reload watchdog which may be running in case of sysjump */
	MCHP_WDG_KICK = 1;
#ifdef CONFIG_WATCHDOG_HELP
	/* Stop aux timer */
	MCHP_TMR16_CTL(0) &= ~1;
#endif
#endif
	/*
	 * TFDP functions will compile to nothing if CONFIG_MEC1701_TFDP
	 * is not defined.
	 */
	tfdp_power(1);
	tfdp_enable(1, 1);
	trace0(0, LFW, 0, "LFW first trace");

	timer_init();
	clock_init();
	cpu_init();
	dma_init();
	uart_init();
	system_init();

	spi_enable(SPI_FLASH_DEVICE, 1);

	uart_puts("littlefw ");
	uart_puts(current_image_data.version);
	uart_puts("\n");

	switch (system_get_image_copy()) {
	case EC_IMAGE_RW:
		trace0(0, LFW, 0, "LFW EC_RW Load");
		uart_puts("lfw-RW load\n");

		init_addr = CONFIG_RW_MEM_OFF + CONFIG_PROGRAM_MEMORY_BASE;
		spi_image_load(CONFIG_EC_WRITABLE_STORAGE_OFF +
			       CONFIG_RW_STORAGE_OFF);
		break;
	case EC_IMAGE_RO:
		trace0(0, LFW, 0, "LFW EC_RO Load");
		uart_puts("lfw-RO load\n");

		init_addr = CONFIG_RO_MEM_OFF + CONFIG_PROGRAM_MEMORY_BASE;
		spi_image_load(CONFIG_EC_PROTECTED_STORAGE_OFF +
			       CONFIG_RO_STORAGE_OFF);
		break;
	default:
		trace0(0, LFW, 0, "LFW default: use EC_RO loaded by BootROM");
		uart_puts("lfw-default case\n");

		MCHP_VBAT_RAM(MCHP_IMAGETYPE_IDX) = EC_IMAGE_RO;

		init_addr = CONFIG_RO_MEM_OFF + CONFIG_PROGRAM_MEMORY_BASE;
	}

	trace11(0, LFW, 0, "Get EC reset handler from 0x%08x", (init_addr + 4));
	trace11(0, LFW, 0, "Jump to EC @ 0x%08x",
		*((uint32_t *)(init_addr + 4)));
	jump_to_image(*(uintptr_t *)(init_addr + 4));

	/* should never get here */
	while (1)
		;
}
