/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MEC1322 SoC little FW
 *
 */

#include <stdint.h>

#include "config.h"
#include "gpio.h"
#include "spi.h"
#include "spi_flash.h"
#include "util.h"
#include "timer.h"
#include "dma.h"
#include "registers.h"
#include "cpu.h"
#include "clock.h"
#include "system.h"
#include "version.h"
#include "hwtimer.h"
#include "gpio_list.h"

#include "ec_lfw.h"

__attribute__ ((section(".intvector")))
const struct int_vector_t hdr_int_vect = {
			(void *)0x11FA00, /* init sp, unused,
						set by MEC ROM loader*/
			&lfw_main,	  /* reset vector */
			&fault_handler,   /* NMI handler */
			&fault_handler,   /* HardFault handler */
			&fault_handler,   /* MPU fault handler */
			&fault_handler    /* Bus fault handler */
};

/* SPI devices - from glados/board.c*/
const struct spi_device_t spi_devices[] = {
	{ CONFIG_SPI_FLASH_PORT, 0, GPIO_PVT_CS0},
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);


void timer_init()
{
	uint32_t val = 0;

	/* Ensure timer is not running */
	MEC1322_TMR32_CTL(0) &= ~(1 << 5);

	/* Enable timer */
	MEC1322_TMR32_CTL(0) |= (1 << 0);

	val = MEC1322_TMR32_CTL(0);

	/* Pre-scale = 48 -> 1MHz -> Period = 1us */
	val = (val & 0xffff) | (47 << 16);

	MEC1322_TMR32_CTL(0) = val;

	/* Set preload to use the full 32 bits of the timer */
	MEC1322_TMR32_PRE(0) = 0xffffffff;

	/* Override the count */
	MEC1322_TMR32_CNT(0) = 0xffffffff;

	/* Auto restart */
	MEC1322_TMR32_CTL(0) |= (1 << 3);

	/* Start counting in timer 0 */
	MEC1322_TMR32_CTL(0) |= (1 << 5);

}

static int spi_flash_readloc(uint8_t *buf_usr,
				unsigned int offset,
				unsigned int bytes)
{
	uint8_t cmd[4] = {SPI_FLASH_READ,
				(offset >> 16) & 0xFF,
				(offset >> 8) & 0xFF,
				offset & 0xFF};

	if (offset + bytes > CONFIG_FLASH_SIZE)
		return EC_ERROR_INVAL;

	return spi_transaction(SPI_FLASH_DEVICE, cmd, 4, buf_usr, bytes);
}

int spi_image_load(uint32_t offset)
{
	uint8_t *buf = (uint8_t *) (CONFIG_RW_MEM_OFF +
				    CONFIG_PROGRAM_MEMORY_BASE);
	uint32_t i;

	BUILD_ASSERT(CONFIG_RO_SIZE == CONFIG_RW_SIZE);
	memset((void *)buf, 0xFF, (CONFIG_RO_SIZE - 4));

	for (i = 0; i < CONFIG_RO_SIZE; i += SPI_CHUNK_SIZE)
		spi_flash_readloc(&buf[i], offset + i, SPI_CHUNK_SIZE);

	return 0;

}

void udelay(unsigned us)
{
	uint32_t t0 = __hw_clock_source_read();
	while (__hw_clock_source_read() - t0 < us)
		;
}

void usleep(unsigned us)
{
	udelay(us);
}

int timestamp_expired(timestamp_t deadline, const timestamp_t *now)
{
	timestamp_t now_val;

	if (!now) {
		now_val = get_time();
		now = &now_val;
	}

	return ((uint32_t)(now->le.lo - deadline.le.lo) >= 0);
}


timestamp_t get_time(void)
{
	timestamp_t ts;

	ts.le.hi = 0;
	ts.le.lo = __hw_clock_source_read();
	return ts;
}

void uart_write_c(char c)
{
	/* Put in carriage return prior to newline to mimic uart_vprintf() */
	if (c == '\n')
		uart_write_c('\r');

	/* Wait for space in transmit FIFO. */
	while (!(MEC1322_UART_LSR & (1 << 5)))
		;
	MEC1322_UART_TB = c;
}

void uart_puts(const char *str)
{
	if (!str || !*str)
		return;

	do {
		uart_write_c(*str++);
	} while (*str);
}

void fault_handler(void)
{
	uart_puts("EXCEPTION!\nTriggering watchdog reset\n");
	/* trigger reset in 1 ms */
	MEC1322_WDG_LOAD = 1;
	MEC1322_WDG_CTL |= 1;
	while (1)
		;

}

void jump_to_image(uintptr_t init_addr)
{
	void (*resetvec)(void) = (void(*)(void))init_addr;
	resetvec();
}

void uart_init(void)
{
	/* Set UART to reset on VCC1_RESET instaed of nSIO_RESET */
	MEC1322_UART_CFG &= ~(1 << 1);

	/* Baud rate = 115200. 1.8432MHz clock. Divisor = 1 */

	/* Set CLK_SRC = 0 */
	MEC1322_UART_CFG &= ~(1 << 0);

	/* Set DLAB = 1 */
	MEC1322_UART_LCR |= (1 << 7);

	/* PBRG0/PBRG1 */
	MEC1322_UART_PBRG0 = 1;
	MEC1322_UART_PBRG1 = 0;

	/* Set DLAB = 0 */
	MEC1322_UART_LCR &= ~(1 << 7);

	/* Set word length to 8-bit */
	MEC1322_UART_LCR |= (1 << 0) | (1 << 1);

	/* Enable FIFO */
	MEC1322_UART_FCR = (1 << 0);

	/* Activate UART */
	MEC1322_UART_ACT |= (1 << 0);

	gpio_config_module(MODULE_UART, 1);
}

void system_init(void)
{

	uint32_t wdt_sts = MEC1322_VBAT_STS & MEC1322_VBAT_STS_WDT;
	uint32_t rst_sts = MEC1322_PCR_CHIP_PWR_RST &
				MEC1322_PWR_RST_STS_VCC1;

	if (rst_sts || wdt_sts)
		MEC1322_VBAT_RAM(MEC1322_IMAGETYPE_IDX)
					= SYSTEM_IMAGE_RO;
}

enum system_image_copy_t system_get_image_copy(void)
{
	return MEC1322_VBAT_RAM(MEC1322_IMAGETYPE_IDX);
}

void lfw_main()
{

	uintptr_t init_addr;

	/* install vector table */
	*((uintptr_t *) 0xe000ed08) = (uintptr_t) &hdr_int_vect;

	/* Use 48 MHz processor clock to power through boot */
	MEC1322_PCR_PROC_CLK_CTL = 1;

#ifdef CONFIG_WATCHDOG
	/* Reload watchdog which may be running in case of sysjump */
	MEC1322_WDG_KICK = 1;
#ifdef CONFIG_WATCHDOG_HELP
	/* Stop aux timer */
	MEC1322_TMR16_CTL(0) &= ~1;
#endif
#endif

	timer_init();
	clock_init();
	cpu_init();
	dma_init();
	uart_init();
	system_init();
	spi_enable(CONFIG_SPI_FLASH_PORT, 1);

	uart_puts("littlefw ");
	uart_puts(version_data.version);
	uart_puts("\n");

	switch (system_get_image_copy()) {
	case SYSTEM_IMAGE_RW:
		uart_puts("lfw-RW load\n");
		init_addr = CONFIG_RW_MEM_OFF + CONFIG_PROGRAM_MEMORY_BASE;
		spi_image_load(CONFIG_EC_WRITABLE_STORAGE_OFF +
			       CONFIG_RW_STORAGE_OFF);
		break;
	case SYSTEM_IMAGE_RO:
		uart_puts("lfw-RO load\n");
		spi_image_load(CONFIG_EC_PROTECTED_STORAGE_OFF +
			       CONFIG_RO_STORAGE_OFF);
		/* fall through */
	default:
		MEC1322_VBAT_RAM(MEC1322_IMAGETYPE_IDX) =
							SYSTEM_IMAGE_RO;
		init_addr = CONFIG_RO_MEM_OFF + CONFIG_PROGRAM_MEMORY_BASE;
	}

	jump_to_image(*(uintptr_t *)(init_addr + 4));

	/* should never get here */
	while (1)
		;
}
