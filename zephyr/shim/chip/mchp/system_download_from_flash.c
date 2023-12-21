/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"
#include "flash.h"
#include "soc.h"
#include "system_chip.h"

#include <zephyr/dt-bindings/clock/npcx_clock.h>
#include <zephyr/toolchain.h>

/* Modules Map */
#define WDT_NODE DT_INST(0, microchip_xec_watchdog)
#define STRUCT_WDT_REG_BASE_ADDR ((struct wdt_regs *)(DT_REG_ADDR(WDT_NODE)))

#define PCR_NODE DT_INST(0, microchip_xec_pcr)
#define STRUCT_PCR_REG_BASE_ADDR \
	((struct pcr_regs *)DT_REG_ADDR_BY_IDX(PCR_NODE, 0))

#define QSPI_NODE DT_INST(0, microchip_xec_qmspi_ldma)
#define STRUCT_QSPI_REG_BASE_ADDR \
	((struct qmspi_regs *)(DT_REG_ADDR(QSPI_NODE)))

#define SPI_READ_111 0x03
#define SPI_READ_111_FAST 0x0b
#define SPI_READ_112_FAST 0x3b

#define QSPI_STATUS_DONE (MCHP_QMSPI_STS_DONE | MCHP_QMSPI_STS_DMA_DONE)

#define QSPI_STATUS_ERR                                    \
	(MCHP_QMSPI_STS_TXB_ERR | MCHP_QMSPI_STS_RXB_ERR | \
	 MCHP_QMSPI_STS_PROG_ERR | MCHP_QMSPI_STS_LDMA_RX_ERR)

FUNC_NORETURN void __keep __attribute__((section(".code_in_sram2")))
__start_qspi(uint32_t resetVectAddr)
{
	struct pcr_regs *pcr = STRUCT_PCR_REG_BASE_ADDR;
	struct qmspi_regs *qspi = STRUCT_QSPI_REG_BASE_ADDR;
	struct wdt_regs *wdt = STRUCT_WDT_REG_BASE_ADDR;
	uint32_t qsts = 0;
	uint32_t exeAddr = 0;

	qspi->EXE = MCHP_QMSPI_EXE_START;

	qsts = qspi->STS;
	while (!(qsts & QSPI_STATUS_DONE)) {
		qsts = qspi->STS;
		if (qsts & QSPI_STATUS_ERR)
			break;
	}

	/* Stop the watchdog */
	wdt->CTRL &= ~MCHP_WDT_CTRL_EN;

	qspi->MODE &= ~(MCHP_QMSPI_M_ACTIVATE);
	if (qsts & QSPI_STATUS_ERR) {
		pcr->SYS_RST |= MCHP_PCR_SYS_RESET_NOW;
		while (1)
			;
	}

	/*
	 * Jump to the exeAddr address if needed. Setting bit 0 of address to
	 * indicate it's a thumb branch for cortex-m series CPU.
	 */
	exeAddr = *(uint32_t *)(resetVectAddr & ~0x03u);

	((void (*)(void))(exeAddr | 0x01))();

	/* Should never get here */
	while (1)
		;
}

uintptr_t __lfw_sram_start = CONFIG_CROS_EC_RAM_BASE + CONFIG_CROS_EC_RAM_SIZE;

typedef void (*START_QSPI_IN_SRAM_FP)(uint32_t);

void system_download_from_flash(uint32_t srcAddr, uint32_t dstAddr,
				uint32_t size, uint32_t resetVectAddr)
{
	struct pcr_regs *pcr = STRUCT_PCR_REG_BASE_ADDR;
	struct qmspi_regs *qspi = STRUCT_QSPI_REG_BASE_ADDR;
	uint32_t fdiv;
	uint32_t cmdaddr;
	int i;
	START_QSPI_IN_SRAM_FP __start_qspi_in_data_sram =
		(START_QSPI_IN_SRAM_FP)(__lfw_sram_start | 0x01);

	/* Check valid address for jumpiing */
	__ASSERT_NO_MSG(exeAddr != 0x0);

#ifdef CONFIG_FLASH_EX_OP_ENABLED
	/* flash registers reset before starting DMA */
	crec_flash_reset();
#endif

	/* Configure QMSPI controller */
	qspi->MODE = MCHP_QMSPI_M_SRST;
	fdiv = CONFIG_PLATFORM_EC_SPI_CLOCK_DIVIDE;
	if (pcr->TURBO_CLK & MCHP_PCR_TURBO_CLK_96M)
		fdiv *= 2;

	qspi->MODE = (fdiv << MCHP_QMSPI_M_FDIV_POS) & MCHP_QMSPI_M_FDIV_MASK;
	qspi->MODE |= (MCHP_QMSPI_M_ACTIVATE | MCHP_QMSPI_M_LDMA_RX_EN);
	qspi->CTRL = BIT(MCHP_QMSPI_C_DESCR_EN_POS);

	/* Transmit 4 bytes(opcode + 24-bit address) on IO0 */
	qspi->DESCR[0] =
		(MCHP_QMSPI_C_IFM_1X | MCHP_QMSPI_C_TX_DATA |
		 MCHP_QMSPI_C_XFR_UNITS_1 | MCHP_QMSPI_C_XFR_NUNITS(4) |
		 MCHP_QMSPI_C_NEXT_DESCR(1));

	if (DT_PROP(QSPI_NODE, lines) == 1) {
		/* Transmit 8 clocks with IO0 tri-stated */
		qspi->DESCR[1] =
			(MCHP_QMSPI_C_IFM_1X | MCHP_QMSPI_C_TX_DIS |
			 MCHP_QMSPI_C_XFR_UNITS_1 | MCHP_QMSPI_C_XFR_NUNITS(1) |
			 MCHP_QMSPI_C_NEXT_DESCR(2));

		/* Read using LDMA RX Chan 0, IFM=1x, Last Descriptor, close */
		qspi->DESCR[2] = (MCHP_QMSPI_C_IFM_1X | MCHP_QMSPI_C_TX_DIS |
				  MCHP_QMSPI_C_RX_EN | MCHP_QMSPI_C_RX_DMA_1B |
				  MCHP_QMSPI_C_CLOSE | MCHP_QMSPI_C_DESCR_LAST |
				  MCHP_QMSPI_C_NEXT_DESCR(0));
	} else {
		/* Transmit 8 clocks with IO0 and IO1 tri-stated */
		qspi->DESCR[1] =
			(MCHP_QMSPI_C_IFM_2X | MCHP_QMSPI_C_TX_DIS |
			 MCHP_QMSPI_C_XFR_UNITS_1 | MCHP_QMSPI_C_XFR_NUNITS(2) |
			 MCHP_QMSPI_C_NEXT_DESCR(2));

		/* Read using LDMA RX Chan 0, IFM=2x, Last Descriptor, close */
		qspi->DESCR[2] = (MCHP_QMSPI_C_IFM_2X | MCHP_QMSPI_C_TX_DIS |
				  MCHP_QMSPI_C_RX_EN | MCHP_QMSPI_C_RX_DMA_1B |
				  MCHP_QMSPI_C_CLOSE | MCHP_QMSPI_C_DESCR_LAST |
				  MCHP_QMSPI_C_NEXT_DESCR(0));
	}

	/* QSPI Local DMA RX channel 0 */
	qspi->LDMA_RX_DESCR_BM = BIT(2); /* descriptor 2 uses RX LDMA */
	qspi->LDRX[0].CTRL = MCHP_QMSPI_LDC_EN | MCHP_QMSPI_LDC_UCHL_EN |
			     MCHP_QMSPI_LDC_INCR_EN;
	qspi->LDRX[0].MSTART = dstAddr;
	qspi->LDRX[0].LEN = size;

	switch ((dstAddr | size) & 0x03u) {
	case 0:
		qspi->LDRX[0].CTRL |= MCHP_QMSPI_LDC_ASZ_4;
		break;
	case 2:
		qspi->LDRX[0].CTRL |= MCHP_QMSPI_LDC_ASZ_2;
		break;
	default:
		qspi->LDRX[0].CTRL |= MCHP_QMSPI_LDC_ASZ_1;
		break;
	}

	cmdaddr = __builtin_bswap32(srcAddr & 0x00ffffff);
	if (DT_PROP(QSPI_NODE, lines) == 1) {
		cmdaddr |= SPI_READ_111_FAST;
	} else {
		cmdaddr |= SPI_READ_112_FAST;
	}

	qspi->TX_FIFO = cmdaddr;

	/* MCHP destination location */
	/* Copy the __start_gdma_in_lpram instructions to LPRAM */
	for (i = 0; i < &__flash_lplfw_end - &__flash_lplfw_start; i++) {
		*((uint32_t *)__lfw_sram_start + i) =
			*(&__flash_lplfw_start + i);
	}

	/* Call into SRAM routine to start QSPI */
	__start_qspi_in_data_sram(resetVectAddr);
}
