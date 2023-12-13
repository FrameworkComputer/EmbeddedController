/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "flash.h"
#include "flash_chip.h"
#include "host_command.h"
#include "intc.h"
#include "registers.h"
#include "shared_mem.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "util.h"
#include "watchdog.h"

#define FLASH_DMA_START ((uint32_t) & __flash_dma_start)
#define FLASH_DMA_CODE __attribute__((section(".flash_direct_map")))
#define FLASH_ILM0_ADDR ((uint32_t) & __ilm0_ram_code)

/* erase size of sector is 1KB or 4KB */
#define FLASH_SECTOR_ERASE_SIZE CONFIG_FLASH_ERASE_SIZE

#ifdef IT83XX_CHIP_FLASH_IS_KGD
/* page program command  */
#define FLASH_CMD_PAGE_WRITE 0x2
/* ector erase command (erase size is 4KB) */
#define FLASH_CMD_SECTOR_ERASE 0x20
/* command for flash write */
#define FLASH_CMD_WRITE FLASH_CMD_PAGE_WRITE
#else
/* Auto address increment programming */
#define FLASH_CMD_AAI_WORD 0xAD
/* Flash sector erase (1K bytes) command */
#define FLASH_CMD_SECTOR_ERASE 0xD7
/* command for flash write */
#define FLASH_CMD_WRITE FLASH_CMD_AAI_WORD
#endif
/* Write status register */
#define FLASH_CMD_WRSR 0x01
/* Write disable */
#define FLASH_CMD_WRDI 0x04
/* Write enable */
#define FLASH_CMD_WREN 0x06
/* Read status register */
#define FLASH_CMD_RS 0x05

#if (CONFIG_FLASH_SIZE_BYTES == 0x80000) && defined(CHIP_CORE_NDS32)
#define FLASH_TEXT_START ((uint32_t) & __flash_text_start)
/* Apply workaround of the issue (b:111808417) */
#define IMMU_CACHE_TAG_INVALID
/* The default tag index of immu. */
#define IMMU_TAG_INDEX_BY_DEFAULT 0x7E000
/* immu cache size is 8K bytes. */
#define IMMU_SIZE 0x2000
#endif

static int stuck_locked;
static int inconsistent_locked;
static int all_protected;
static int flash_dma_code_enabled;

#define FWP_REG(bank) (bank / 8)
#define FWP_MASK(bank) (1 << (bank % 8))

enum flash_wp_interface {
	FLASH_WP_HOST = 0x01,
	FLASH_WP_DBGR = 0x02,
	FLASH_WP_EC = 0x04,
};

enum flash_wp_status {
	FLASH_WP_STATUS_PROTECT_RO = EC_FLASH_PROTECT_RO_NOW,
	FLASH_WP_STATUS_PROTECT_ALL = EC_FLASH_PROTECT_ALL_NOW,
};

enum flash_status_mask {
	FLASH_SR_NO_BUSY = 0,
	/* Internal write operation is in progress */
	FLASH_SR_BUSY = 0x01,
	/* Device is memory Write enabled */
	FLASH_SR_WEL = 0x02,

	FLASH_SR_ALL = (FLASH_SR_BUSY | FLASH_SR_WEL),
};

enum dlm_address_view {
	SCAR0_ILM0_DLM13 = 0x8D000, /* DLM ~ 0x8DFFF H2RAM map LPC I/O */
	SCAR1_ILM1_DLM11 = 0x8B000, /* DLM ~ 0x8BFFF ram 44K ~ 48K */
	SCAR2_ILM2_DLM14 = 0x8E000, /* DLM ~ 0x8EFFF RO/RW flash code DMA */
	SCAR3_ILM3_DLM6 = 0x86000, /* DLM ~ 0x86FFF ram 24K ~ 28K */
	SCAR4_ILM4_DLM7 = 0x87000, /* DLM ~ 0x87FFF ram 28K ~ 32K */
	SCAR5_ILM5_DLM8 = 0x88000, /* DLM ~ 0x88FFF ram 32K ~ 36K */
	SCAR6_ILM6_DLM9 = 0x89000, /* DLM ~ 0x89FFF ram 36K ~ 40K */
	SCAR7_ILM7_DLM10 = 0x8A000, /* DLM ~ 0x8AFFF ram 40K ~ 44K */
	SCAR8_ILM8_DLM4 = 0x84000, /* DLM ~ 0x84FFF ram 16K ~ 20K */
	SCAR9_ILM9_DLM5 = 0x85000, /* DLM ~ 0x85FFF ram 20K ~ 24K */
	SCAR10_ILM10_DLM2 = 0x82000, /* DLM ~ 0x82FFF ram 8K ~ 12K */
	SCAR11_ILM11_DLM3 = 0x83000, /* DLM ~ 0x83FFF ram 12K ~ 16K */
	SCAR12_ILM12_DLM12 = 0x8C000, /* DLM ~ 0x8CFFF immu cache */
};

void FLASH_DMA_CODE dma_reset_immu(int fill_immu)
{
	/* Immu tag sram reset */
	IT83XX_GCTRL_MCCR |= 0x10;
	/* Make sure the immu(dynamic cache) is reset */
	data_serialization_barrier();

	IT83XX_GCTRL_MCCR &= ~0x10;
	data_serialization_barrier();

#ifdef IMMU_CACHE_TAG_INVALID
	/*
	 * Workaround for (b:111808417):
	 * After immu reset, we will fill the immu cache with 8KB data
	 * that are outside address 0x7e000 ~ 0x7ffff.
	 * When CPU tries to fetch contents from address 0x7e000 ~ 0x7ffff,
	 * immu will re-fetch the missing contents inside 0x7e000 ~ 0x7ffff.
	 */
	if (fill_immu) {
		volatile int immu __unused;
		const uint32_t *ptr = (uint32_t *)FLASH_TEXT_START;
		int i = 0;

		while (i < IMMU_SIZE) {
			immu = *ptr++;
			i += sizeof(*ptr);
		}
	}
#endif
}

void FLASH_DMA_CODE dma_flash_follow_mode(void)
{
	/*
	 * ECINDAR3-0 are EC-indirect memory address registers.
	 *
	 * Enter follow mode by writing 0xf to low nibble of ECINDAR3 register,
	 * and set high nibble as 0x4 to select internal flash.
	 */
	IT83XX_SMFI_ECINDAR3 = (EC_INDIRECT_READ_INTERNAL_FLASH | 0xf);
	/* Set FSCE# as high level by writing 0 to address xfff_fe00h */
	IT83XX_SMFI_ECINDAR2 = 0xFF;
	IT83XX_SMFI_ECINDAR1 = 0xFE;
	IT83XX_SMFI_ECINDAR0 = 0x00;
	/* EC-indirect memory data register */
	IT83XX_SMFI_ECINDDR = 0x00;
}

void FLASH_DMA_CODE dma_flash_follow_mode_exit(void)
{
	/* Exit follow mode, and keep the setting of selecting internal flash */
	IT83XX_SMFI_ECINDAR3 = EC_INDIRECT_READ_INTERNAL_FLASH;
	IT83XX_SMFI_ECINDAR2 = 0x00;
}

void FLASH_DMA_CODE dma_flash_fsce_high(void)
{
	/* FSCE# high level */
	IT83XX_SMFI_ECINDAR1 = 0xFE;
	IT83XX_SMFI_ECINDDR = 0x00;
}

uint8_t FLASH_DMA_CODE dma_flash_read_dat(void)
{
	/* Read data from FMISO */
	return IT83XX_SMFI_ECINDDR;
}

void FLASH_DMA_CODE dma_flash_write_dat(uint8_t wdata)
{
	/* Write data to FMOSI */
	IT83XX_SMFI_ECINDDR = wdata;
}

void FLASH_DMA_CODE dma_flash_transaction(int wlen, uint8_t *wbuf, int rlen,
					  uint8_t *rbuf, int cmd_end)
{
	int i;

	/*  FSCE# with low level */
	IT83XX_SMFI_ECINDAR1 = 0xFD;
	/* Write data to FMOSI */
	for (i = 0; i < wlen; i++)
		IT83XX_SMFI_ECINDDR = wbuf[i];
	/* Read data from FMISO */
	for (i = 0; i < rlen; i++)
		rbuf[i] = IT83XX_SMFI_ECINDDR;

	/* FSCE# high level if transaction done */
	if (cmd_end)
		dma_flash_fsce_high();
}

void FLASH_DMA_CODE dma_flash_cmd_read_status(enum flash_status_mask mask,
					      enum flash_status_mask target)
{
	uint8_t status[1];
	uint8_t cmd_rs[] = { FLASH_CMD_RS };

	/*
	 * We prefer no timeout here. We can always get the status
	 * we want, or wait for watchdog triggered to check
	 * e-flash's status instead of breaking loop.
	 * This will avoid fetching unknown instruction from e-flash
	 * and causing exception.
	 */
	while (1) {
		/* read status */
		dma_flash_transaction(sizeof(cmd_rs), cmd_rs, 1, status, 1);
		/* only bit[1:0] valid */
		if ((status[0] & mask) == target)
			break;
	}
}

void FLASH_DMA_CODE dma_flash_cmd_write_enable(void)
{
	uint8_t cmd_we[] = { FLASH_CMD_WREN };

	/* enter EC-indirect follow mode */
	dma_flash_follow_mode();
	/* send write enable command */
	dma_flash_transaction(sizeof(cmd_we), cmd_we, 0, NULL, 1);
	/* read status and make sure busy bit cleared and write enabled. */
	dma_flash_cmd_read_status(FLASH_SR_ALL, FLASH_SR_WEL);
	/* exit EC-indirect follow mode */
	dma_flash_follow_mode_exit();
}

void FLASH_DMA_CODE dma_flash_cmd_write_disable(void)
{
	uint8_t cmd_wd[] = { FLASH_CMD_WRDI };

	/* enter EC-indirect follow mode */
	dma_flash_follow_mode();
	/* send write disable command */
	dma_flash_transaction(sizeof(cmd_wd), cmd_wd, 0, NULL, 1);
	/* make sure busy bit cleared. */
	dma_flash_cmd_read_status(FLASH_SR_ALL, FLASH_SR_NO_BUSY);
	/* exit EC-indirect follow mode */
	dma_flash_follow_mode_exit();
}

void FLASH_DMA_CODE dma_flash_cmd_erase(int addr, int cmd)
{
	uint8_t cmd_erase[] = { cmd, ((addr >> 16) & 0xFF),
				((addr >> 8) & 0xFF), (addr & 0xFF) };

	/* enter EC-indirect follow mode */
	dma_flash_follow_mode();
	/* send erase command */
	dma_flash_transaction(sizeof(cmd_erase), cmd_erase, 0, NULL, 1);
	/* make sure busy bit cleared. */
	dma_flash_cmd_read_status(FLASH_SR_BUSY, FLASH_SR_NO_BUSY);
	/* exit EC-indirect follow mode */
	dma_flash_follow_mode_exit();
}

void FLASH_DMA_CODE dma_flash_cmd_write(int addr, int wlen, uint8_t *wbuf)
{
	int i;
	uint8_t flash_write[] = { FLASH_CMD_WRITE, ((addr >> 16) & 0xFF),
				  ((addr >> 8) & 0xFF), (addr & 0xFF) };

	/* enter EC-indirect follow mode */
	dma_flash_follow_mode();
	/* send flash write command (aai word or page program) */
	dma_flash_transaction(sizeof(flash_write), flash_write, 0, NULL, 0);
#ifdef IT83XX_CHIP_FLASH_IS_KGD
	for (i = 0; i < wlen; i++) {
		/* send data byte */
		dma_flash_write_dat(wbuf[i]);

		/*
		 * we want to restart the write sequence every IDEAL_SIZE
		 * chunk worth of data.
		 */
		if (!(++addr % CONFIG_FLASH_WRITE_IDEAL_SIZE)) {
			uint8_t w_en[] = { FLASH_CMD_WREN };

			dma_flash_fsce_high();
			/* make sure busy bit cleared. */
			dma_flash_cmd_read_status(FLASH_SR_BUSY,
						  FLASH_SR_NO_BUSY);
			/* send write enable command */
			dma_flash_transaction(sizeof(w_en), w_en, 0, NULL, 1);
			/* make sure busy bit cleared and write enabled. */
			dma_flash_cmd_read_status(FLASH_SR_ALL, FLASH_SR_WEL);
			/* re-send write command */
			flash_write[1] = (addr >> 16) & 0xff;
			flash_write[2] = (addr >> 8) & 0xff;
			flash_write[3] = addr & 0xff;
			dma_flash_transaction(sizeof(flash_write), flash_write,
					      0, NULL, 0);
		}
	}
	dma_flash_fsce_high();
	/* make sure busy bit cleared. */
	dma_flash_cmd_read_status(FLASH_SR_BUSY, FLASH_SR_NO_BUSY);
#else
	for (i = 0; i < wlen; i += 2) {
		dma_flash_write_dat(wbuf[i]);
		dma_flash_write_dat(wbuf[i + 1]);
		dma_flash_fsce_high();
		/* make sure busy bit cleared. */
		dma_flash_cmd_read_status(FLASH_SR_BUSY, FLASH_SR_NO_BUSY);
		/* resend aai word command without address field */
		if ((i + 2) < wlen)
			dma_flash_transaction(1, flash_write, 0, NULL, 0);
	}
#endif
	/* exit EC-indirect follow mode */
	dma_flash_follow_mode_exit();
}

uint8_t FLASH_DMA_CODE dma_flash_indirect_fast_read(int addr)
{
	IT83XX_SMFI_ECINDAR3 = EC_INDIRECT_READ_INTERNAL_FLASH;
	IT83XX_SMFI_ECINDAR2 = (addr >> 16) & 0xFF;
	IT83XX_SMFI_ECINDAR1 = (addr >> 8) & 0xFF;
	IT83XX_SMFI_ECINDAR0 = (addr & 0xFF);

	return IT83XX_SMFI_ECINDDR;
}

int FLASH_DMA_CODE dma_flash_verify(int addr, int size, const char *data)
{
	int i;
	uint8_t *wbuf = (uint8_t *)data;
	uint8_t *flash = (uint8_t *)addr;

	/* verify for erase */
	if (data == NULL) {
		for (i = 0; i < size; i++) {
			if (flash[i] != 0xFF)
				return EC_ERROR_UNKNOWN;
		}
		/* verify for write */
	} else {
		for (i = 0; i < size; i++) {
			if (flash[i] != wbuf[i])
				return EC_ERROR_UNKNOWN;
		}
	}

	return EC_SUCCESS;
}

void FLASH_DMA_CODE dma_flash_write(int addr, int wlen, const char *wbuf)
{
	dma_flash_cmd_write_enable();
	dma_flash_cmd_write(addr, wlen, (uint8_t *)wbuf);
	dma_flash_cmd_write_disable();
}

void FLASH_DMA_CODE dma_flash_erase(int addr, int cmd)
{
	dma_flash_cmd_write_enable();
	dma_flash_cmd_erase(addr, cmd);
	dma_flash_cmd_write_disable();
}

static enum flash_wp_status flash_check_wp(void)
{
	enum flash_wp_status wp_status;
	int all_bank_count, bank;

	all_bank_count = CONFIG_FLASH_SIZE_BYTES / CONFIG_FLASH_BANK_SIZE;

	for (bank = 0; bank < all_bank_count; bank++) {
		if (!(IT83XX_GCTRL_EWPR0PFEC(FWP_REG(bank)) & FWP_MASK(bank)))
			break;
	}

	if (bank == WP_BANK_COUNT)
		wp_status = FLASH_WP_STATUS_PROTECT_RO;
	else if (bank == (WP_BANK_COUNT + PSTATE_BANK_COUNT))
		wp_status = FLASH_WP_STATUS_PROTECT_RO;
	else if (bank == all_bank_count)
		wp_status = FLASH_WP_STATUS_PROTECT_ALL;
	else
		wp_status = 0;

	return wp_status;
}

/**
 * Protect flash banks until reboot.
 *
 * @param start_bank    Start bank to protect
 * @param bank_count    Number of banks to protect
 */
static void flash_protect_banks(int start_bank, int bank_count,
				enum flash_wp_interface wp_if)
{
	int bank;

	for (bank = start_bank; bank < start_bank + bank_count; bank++) {
		if (wp_if & FLASH_WP_EC)
			IT83XX_GCTRL_EWPR0PFEC(FWP_REG(bank)) |= FWP_MASK(bank);
		if (wp_if & FLASH_WP_HOST)
			IT83XX_GCTRL_EWPR0PFH(FWP_REG(bank)) |= FWP_MASK(bank);
		if (wp_if & FLASH_WP_DBGR)
			IT83XX_GCTRL_EWPR0PFD(FWP_REG(bank)) |= FWP_MASK(bank);
	}
}

int FLASH_DMA_CODE crec_flash_physical_read(int offset, int size, char *data)
{
	int i;

	for (i = 0; i < size; i++) {
		data[i] = dma_flash_indirect_fast_read(offset);
		offset++;
	}

	return EC_SUCCESS;
}

/**
 * Write to physical flash.
 *
 * Offset and size must be a multiple of CONFIG_FLASH_WRITE_SIZE.
 *
 * @param offset        Flash offset to write.
 * @param size          Number of bytes to write.
 * @param data          Data to write to flash.  Must be 32-bit aligned.
 */
int FLASH_DMA_CODE crec_flash_physical_write(int offset, int size,
					     const char *data)
{
	int ret = EC_ERROR_UNKNOWN;

	if (flash_dma_code_enabled == 0)
		return EC_ERROR_ACCESS_DENIED;

	if (all_protected)
		return EC_ERROR_ACCESS_DENIED;

	watchdog_reload();

	/*
	 * CPU can't fetch instruction from flash while use
	 * EC-indirect follow mode to access flash, interrupts need to be
	 * disabled.
	 */
	interrupt_disable();

	dma_flash_write(offset, size, data);
#ifdef IMMU_CACHE_TAG_INVALID
	dma_reset_immu((offset + size) >= IMMU_TAG_INDEX_BY_DEFAULT);
#else
	dma_reset_immu(0);
#endif
	/*
	 * Internal flash of N8 or RISC-V core is ILM(Instruction Local Memory)
	 * mapped, but RISC-V's ILM base address is 0x80000000.
	 *
	 * Ensure that we will get the ILM address of a flash offset.
	 */
	offset |= CONFIG_MAPPED_STORAGE_BASE;
	ret = dma_flash_verify(offset, size, data);

	interrupt_enable();

	return ret;
}

/**
 * Erase physical flash.
 *
 * Offset and size must be a multiple of CONFIG_FLASH_ERASE_SIZE.
 *
 * @param offset        Flash offset to erase.
 * @param size          Number of bytes to erase.
 */
int FLASH_DMA_CODE crec_flash_physical_erase(int offset, int size)
{
	int v_size = size, v_addr = offset, ret = EC_ERROR_UNKNOWN;

	if (flash_dma_code_enabled == 0)
		return EC_ERROR_ACCESS_DENIED;

	if (all_protected)
		return EC_ERROR_ACCESS_DENIED;

	/*
	 * CPU can't fetch instruction from flash while use
	 * EC-indirect follow mode to access flash, interrupts need to be
	 * disabled.
	 */
	interrupt_disable();

	/* Always use sector erase command (1K or 4K bytes) */
	for (; size > 0; size -= FLASH_SECTOR_ERASE_SIZE) {
		dma_flash_erase(offset, FLASH_CMD_SECTOR_ERASE);
		offset += FLASH_SECTOR_ERASE_SIZE;
		/*
		 * If requested erase size is too large at one time on KGD
		 * flash, we need to reload watchdog to prevent the reset.
		 */
		if (IS_ENABLED(IT83XX_CHIP_FLASH_IS_KGD) && (size > 0x10000))
			watchdog_reload();
			/*
			 * EC still need to handle AP's EC_CMD_GET_COMMS_STATUS
			 * command during erasing.
			 */
#ifdef IT83XX_IRQ_SPI_PERIPHERAL
		if (IS_ENABLED(CONFIG_SPI) && IS_ENABLED(HAS_TASK_HOSTCMD) &&
		    IS_ENABLED(CONFIG_HOST_COMMAND_STATUS)) {
			if (IT83XX_SPI_RX_VLISR & IT83XX_SPI_RVLI)
				task_trigger_irq(IT83XX_IRQ_SPI_PERIPHERAL);
		}
#endif
	}
#ifdef IMMU_CACHE_TAG_INVALID
	dma_reset_immu((v_addr + v_size) >= IMMU_TAG_INDEX_BY_DEFAULT);
#else
	dma_reset_immu(0);
#endif
	/* get the ILM address of a flash offset. */
	v_addr |= CONFIG_MAPPED_STORAGE_BASE;
	ret = dma_flash_verify(v_addr, v_size, NULL);

	interrupt_enable();

	return ret;
}

/**
 * Read physical write protect setting for a flash bank.
 *
 * @param bank    Bank index to check.
 * @return        non-zero if bank is protected until reboot.
 */
int crec_flash_physical_get_protect(int bank)
{
	return IT83XX_GCTRL_EWPR0PFEC(FWP_REG(bank)) & FWP_MASK(bank);
}

/**
 * Protect flash now.
 *
 * @param all      Protect all (=1) or just read-only and pstate (=0).
 * @return         non-zero if error.
 */
int crec_flash_physical_protect_now(int all)
{
	if (all) {
		/* Protect the entire flash */
		flash_protect_banks(
			0, CONFIG_FLASH_SIZE_BYTES / CONFIG_FLASH_BANK_SIZE,
			FLASH_WP_EC);
		all_protected = 1;
	} else {
		/* Protect the read-only section and persistent state */
		flash_protect_banks(WP_BANK_OFFSET, WP_BANK_COUNT, FLASH_WP_EC);
#ifdef PSTATE_BANK
		flash_protect_banks(PSTATE_BANK, PSTATE_BANK_COUNT,
				    FLASH_WP_EC);
#endif
	}

	/*
	 * bit[0], eflash protect lock register which can only be write 1 and
	 * only be cleared by power-on reset.
	 */
	IT83XX_GCTRL_EPLR |= 0x01;

	return EC_SUCCESS;
}

/**
 * Return flash protect state flags from the physical layer.
 *
 * This should only be called by flash_get_protect().
 *
 * Uses the EC_FLASH_PROTECT_* flags from ec_commands.h
 */
uint32_t crec_flash_physical_get_protect_flags(void)
{
	uint32_t flags = 0;

	flags |= flash_check_wp();

	if (all_protected)
		flags |= EC_FLASH_PROTECT_ALL_NOW;

	/* Check if blocks were stuck locked at pre-init */
	if (stuck_locked)
		flags |= EC_FLASH_PROTECT_ERROR_STUCK;

	/* Check if flash protection is in inconsistent state at pre-init */
	if (inconsistent_locked)
		flags |= EC_FLASH_PROTECT_ERROR_INCONSISTENT;

	return flags;
}

/**
 * Return the valid flash protect flags.
 *
 * @return   A combination of EC_FLASH_PROTECT_* flags from ec_commands.h
 */
uint32_t crec_flash_physical_get_valid_flags(void)
{
	return EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW |
	       EC_FLASH_PROTECT_ALL_NOW;
}

/**
 * Return the writable flash protect flags.
 *
 * @param    cur_flags The current flash protect flags.
 * @return   A combination of EC_FLASH_PROTECT_* flags from ec_commands.h
 */
uint32_t crec_flash_physical_get_writable_flags(uint32_t cur_flags)
{
	uint32_t ret = 0;

	/* If RO protection isn't enabled, its at-boot state can be changed. */
	if (!(cur_flags & EC_FLASH_PROTECT_RO_NOW))
		ret |= EC_FLASH_PROTECT_RO_AT_BOOT;

	/*
	 * If entire flash isn't protected at this boot, it can be enabled if
	 * the WP GPIO is asserted.
	 */
	if (!(cur_flags & EC_FLASH_PROTECT_ALL_NOW) &&
	    (cur_flags & EC_FLASH_PROTECT_GPIO_ASSERTED))
		ret |= EC_FLASH_PROTECT_ALL_NOW;

	return ret;
}

static void flash_enable_second_ilm(void)
{
#ifdef CHIP_CORE_RISCV
	/* Make sure no interrupt while enable static cache */
	interrupt_disable();

	/* Invalid ILM0 */
	IT83XX_GCTRL_RVILMCR0 &= ~ILMCR_ILM0_ENABLE;
	IT83XX_SMFI_SCAR0H = BIT(3);
	/* copy code to ram */
	memcpy((void *)CHIP_RAMCODE_ILM0, (const void *)FLASH_ILM0_ADDR,
	       IT83XX_ILM_BLOCK_SIZE);
	/*
	 * Set the logic memory address(flash code of RO/RW) in flash
	 * by programming the register SCAR0x bit19-bit0.
	 */
	IT83XX_SMFI_SCAR0L = FLASH_ILM0_ADDR & GENMASK(7, 0);
	IT83XX_SMFI_SCAR0M = (FLASH_ILM0_ADDR >> 8) & GENMASK(7, 0);
	IT83XX_SMFI_SCAR0H = (FLASH_ILM0_ADDR >> 16) & GENMASK(2, 0);
	if (FLASH_ILM0_ADDR & BIT(19))
		IT83XX_SMFI_SCAR0H |= BIT(7);
	else
		IT83XX_SMFI_SCAR0H &= ~BIT(7);
	/* Enable ILM 0 */
	IT83XX_GCTRL_RVILMCR0 |= ILMCR_ILM0_ENABLE;

	interrupt_enable();
#endif
}

static void flash_code_static_dma(void)
{
	/* Make sure no interrupt while enable static DMA */
	interrupt_disable();

	/* invalid static DMA first */
	if (IS_ENABLED(CHIP_CORE_RISCV))
		IT83XX_GCTRL_RVILMCR0 &= ~ILMCR_ILM2_ENABLE;
	IT83XX_SMFI_SCAR2H = 0x08;

	/* Enable DLM 56k~60k region and than copy data into it */
	if (IS_ENABLED(CHIP_CORE_NDS32))
		IT83XX_GCTRL_MCCR2 |= IT83XX_DLM14_ENABLE;
	memcpy((void *)CHIP_RAMCODE_BASE, (const void *)FLASH_DMA_START,
	       IT83XX_ILM_BLOCK_SIZE);
	if (IS_ENABLED(CHIP_CORE_RISCV))
		IT83XX_GCTRL_RVILMCR0 |= ILMCR_ILM2_ENABLE;
	/* Disable DLM 56k~60k region and be the ram code section */
	if (IS_ENABLED(CHIP_CORE_NDS32))
		IT83XX_GCTRL_MCCR2 &= ~IT83XX_DLM14_ENABLE;

	/*
	 * Enable ILM
	 * Set the logic memory address(flash code of RO/RW) in eflash
	 * by programming the register SCARx bit19-bit0.
	 */
	IT83XX_SMFI_SCAR2L = FLASH_DMA_START & 0xFF;
	IT83XX_SMFI_SCAR2M = (FLASH_DMA_START >> 8) & 0xFF;
#ifdef IT83XX_DAM_ADDR_BIT19_AT_REG_SCARXH_BIT7
	IT83XX_SMFI_SCAR2H = (FLASH_DMA_START >> 16) & 0x7;
	if (FLASH_DMA_START & BIT(19))
		IT83XX_SMFI_SCAR2H |= BIT(7);
	else
		IT83XX_SMFI_SCAR2H &= ~BIT(7);
#else
	IT83XX_SMFI_SCAR2H = (FLASH_DMA_START >> 16) & 0x0F;
#endif
	/*
	 * Validate Direct-map SRAM function by programming
	 * register SCARx bit20=0
	 */
	IT83XX_SMFI_SCAR2H &= ~0x10;

	flash_dma_code_enabled = 0x01;

	interrupt_enable();
}

/**
 * Initialize the module.
 *
 * Applies at-boot protection settings if necessary.
 */
int crec_flash_pre_init(void)
{
	int32_t reset_flags, prot_flags, unwanted_prot_flags;

	/* By default, select internal flash for indirect fast read. */
	IT83XX_SMFI_ECINDAR3 = EC_INDIRECT_READ_INTERNAL_FLASH;
	if (IS_ENABLED(IT83XX_CHIP_FLASH_IS_KGD))
		IT83XX_SMFI_FLHCTRL6R |= IT83XX_SMFI_MASK_ECINDPP;
	flash_code_static_dma();
	/*
	 * Enable second ilm (ILM0 of it8xxx2 series), so we can pull more code
	 * (4kB) into static cache to save latency of fetching code from flash.
	 */
	flash_enable_second_ilm();

	reset_flags = system_get_reset_flags();
	prot_flags = crec_flash_get_protect();
	unwanted_prot_flags = EC_FLASH_PROTECT_ALL_NOW |
			      EC_FLASH_PROTECT_ERROR_INCONSISTENT;

	/*
	 * If we have already jumped between images, an earlier image could
	 * have applied write protection.  Nothing additional needs to be done.
	 */
	if (reset_flags & EC_RESET_FLAG_SYSJUMP)
		return EC_SUCCESS;

	if (prot_flags & EC_FLASH_PROTECT_GPIO_ASSERTED) {
		/* Protect the entire flash of host interface */
		flash_protect_banks(
			0, CONFIG_FLASH_SIZE_BYTES / CONFIG_FLASH_BANK_SIZE,
			FLASH_WP_HOST);
		/* Protect the entire flash of DBGR interface */
		flash_protect_banks(
			0, CONFIG_FLASH_SIZE_BYTES / CONFIG_FLASH_BANK_SIZE,
			FLASH_WP_DBGR);
		/*
		 * Write protect is asserted.  If we want RO flash protected,
		 * protect it now.
		 */
		if ((prot_flags & EC_FLASH_PROTECT_RO_AT_BOOT) &&
		    !(prot_flags & EC_FLASH_PROTECT_RO_NOW)) {
			int rv =
				crec_flash_set_protect(EC_FLASH_PROTECT_RO_NOW,
						       EC_FLASH_PROTECT_RO_NOW);
			if (rv)
				return rv;

			/* Re-read flags */
			prot_flags = crec_flash_get_protect();
		}
	} else {
		/* Don't want RO flash protected */
		unwanted_prot_flags |= EC_FLASH_PROTECT_RO_NOW;
	}

	/* If there are no unwanted flags, done */
	if (!(prot_flags & unwanted_prot_flags))
		return EC_SUCCESS;

	/*
	 * If the last reboot was a power-on reset, it should have cleared
	 * write-protect.  If it didn't, then the flash write protect registers
	 * have been permanently committed and we can't fix that.
	 */
	if (reset_flags & EC_RESET_FLAG_POWER_ON) {
		stuck_locked = 1;
		return EC_ERROR_ACCESS_DENIED;
	} else {
		/*
		 * Set inconsistent flag, because there is no software
		 * reset can clear write-protect.
		 */
		inconsistent_locked = 1;
		return EC_ERROR_ACCESS_DENIED;
	}

	/* That doesn't return, so if we're still here that's an error */
	return EC_ERROR_UNKNOWN;
}
