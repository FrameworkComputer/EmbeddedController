/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Register map for NPCX processor
 *
 * This is meant to be temporary until the NPCX monitor support is moved
 * to Zephyr upstream
 */

#ifndef __CROS_EC_REGISTERS_H
#define __CROS_EC_REGISTERS_H

/*
 * The monitor code doesn't build cleanly under the Zephyr environment if
 * include/common.h is included. Replicate the register access macros until
 * this code is moved upstream.
 */

/* Macros to access registers */
#define REG64_ADDR(addr) ((volatile uint64_t *)(addr))
#define REG32_ADDR(addr) ((volatile uint32_t *)(addr))
#define REG16_ADDR(addr) ((volatile uint16_t *)(addr))
#define REG8_ADDR(addr) ((volatile uint8_t *)(addr))

#define REG64(addr) (*REG64_ADDR(addr))
#define REG32(addr) (*REG32_ADDR(addr))
#define REG16(addr) (*REG16_ADDR(addr))
#define REG8(addr) (*REG8_ADDR(addr))

/* Standard macros / definitions */
#define GENERIC_MAX(x, y) ((x) > (y) ? (x) : (y))
#define GENERIC_MIN(x, y) ((x) < (y) ? (x) : (y))
#ifndef MAX
#define MAX(a, b)                            \
	({                                   \
		__typeof__(a) temp_a = (a);  \
		__typeof__(b) temp_b = (b);  \
                                             \
		GENERIC_MAX(temp_a, temp_b); \
	})
#endif
#ifndef MIN
#define MIN(a, b)                            \
	({                                   \
		__typeof__(a) temp_a = (a);  \
		__typeof__(b) temp_b = (b);  \
                                             \
		GENERIC_MIN(temp_a, temp_b); \
	})
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif

/******************************************************************************/
/*
 * Macro Functions
 */
/* Bit functions */
#define SET_BIT(reg, bit) ((reg) |= (0x1 << (bit)))
#define CLEAR_BIT(reg, bit) ((reg) &= (~(0x1 << (bit))))
#define IS_BIT_SET(reg, bit) (((reg) >> (bit)) & (0x1))
#define UPDATE_BIT(reg, bit, cond)           \
	{                                    \
		if (cond)                    \
			SET_BIT(reg, bit);   \
		else                         \
			CLEAR_BIT(reg, bit); \
	}
/* Field functions */
#define GET_POS_FIELD(pos, size) pos
#define GET_SIZE_FIELD(pos, size) size
#define FIELD_POS(field) GET_POS_##field
#define FIELD_SIZE(field) GET_SIZE_##field
/* Read field functions */
#define GET_FIELD(reg, field) \
	_GET_FIELD_(reg, FIELD_POS(field), FIELD_SIZE(field))
#define _GET_FIELD_(reg, f_pos, f_size) \
	(((reg) >> (f_pos)) & ((1 << (f_size)) - 1))
/* Write field functions */
#define SET_FIELD(reg, field, value) \
	_SET_FIELD_(reg, FIELD_POS(field), FIELD_SIZE(field), value)
#define _SET_FIELD_(reg, f_pos, f_size, value)                     \
	((reg) = ((reg) & (~(((1 << (f_size)) - 1) << (f_pos)))) | \
		 ((value) << (f_pos)))

/* NPCX7 & NPCX9 */
#define NPCX_DEVALT(n) REG8(NPCX_SCFG_BASE_ADDR + 0x010 + (n))

/******************************************************************************/
/*
 * NPCX (Nuvoton M4 EC) Register Definitions
 */

/* Modules Map */
#define NPCX_ESPI_BASE_ADDR 0x4000A000
#define NPCX_MDC_BASE_ADDR 0x4000C000
#define NPCX_PMC_BASE_ADDR 0x4000D000
#define NPCX_SIB_BASE_ADDR 0x4000E000
#define NPCX_SHI_BASE_ADDR 0x4000F000
#define NPCX_SHM_BASE_ADDR 0x40010000
#define NPCX_GDMA_BASE_ADDR 0x40011000
#define NPCX_FIU_BASE_ADDR 0x40020000
#define NPCX_KBSCAN_REGS_BASE 0x400A3000
#define NPCX_WOV_BASE_ADDR 0x400A4000
#define NPCX_APM_BASE_ADDR 0x400A4800
#define NPCX_GLUE_REGS_BASE 0x400A5000
#define NPCX_BBRAM_BASE_ADDR 0x400AF000
#define NPCX_PS2_BASE_ADDR 0x400B1000
#define NPCX_HFCG_BASE_ADDR 0x400B5000
#define NPCX_LFCG_BASE_ADDR 0x400B5100
#define NPCX_FMUL2_BASE_ADDR 0x400B5200
#define NPCX_MTC_BASE_ADDR 0x400B7000
#define NPCX_MSWC_BASE_ADDR 0x400C1000
#define NPCX_SCFG_BASE_ADDR 0x400C3000
#define NPCX_KBC_BASE_ADDR 0x400C7000
#define NPCX_ADC_BASE_ADDR 0x400D1000
#define NPCX_SPI_BASE_ADDR 0x400D2000
#define NPCX_PECI_BASE_ADDR 0x400D4000
#define NPCX_TWD_BASE_ADDR 0x400D8000

/* Multi-Modules Map */
#define NPCX_PWM_BASE_ADDR(mdl) (0x40080000 + ((mdl)*0x2000L))
#define NPCX_GPIO_BASE_ADDR(mdl) (0x40081000 + ((mdl)*0x2000L))
#define NPCX_ITIM_BASE_ADDR(mdl) (0x400B0000 + ((mdl)*0x2000L))
#define NPCX_MIWU_BASE_ADDR(mdl) (0x400BB000 + ((mdl)*0x2000L))
#define NPCX_MFT_BASE_ADDR(mdl) (0x400E1000 + ((mdl)*0x2000L))
#define NPCX_PM_CH_BASE_ADDR(mdl) (0x400C9000 + ((mdl)*0x2000L))

/******************************************************************************/
/* System Configuration (SCFG) Registers */
#define NPCX_DEVCNT REG8(NPCX_SCFG_BASE_ADDR + 0x000)
#define NPCX_STRPST REG8(NPCX_SCFG_BASE_ADDR + 0x001)
#define NPCX_RSTCTL REG8(NPCX_SCFG_BASE_ADDR + 0x002)
#define NPCX_DEV_CTL4 REG8(NPCX_SCFG_BASE_ADDR + 0x006)
#define NPCX_LFCGCALCNT REG8(NPCX_SCFG_BASE_ADDR + 0x021)
#define NPCX_PUPD_EN0 REG8(NPCX_SCFG_BASE_ADDR + 0x028)
#define NPCX_PUPD_EN1 REG8(NPCX_SCFG_BASE_ADDR + 0x029)
#define NPCX_SCFG_VER REG8(NPCX_SCFG_BASE_ADDR + 0x02F)

#define TEST_BKSL REG8(NPCX_SCFG_BASE_ADDR + 0x037)
#define TEST0 REG8(NPCX_SCFG_BASE_ADDR + 0x038)
#define BLKSEL 0

/* SCFG register fields */
#define NPCX_DEVCNT_F_SPI_TRIS 6
#define NPCX_DEVCNT_HIF_TYP_SEL_FIELD FIELD(2, 2)
#define NPCX_DEVCNT_JEN1_HEN 5
#define NPCX_DEVCNT_JEN0_HEN 4
#define NPCX_STRPST_TRIST 1
#define NPCX_STRPST_TEST 2
#define NPCX_STRPST_JEN1 4
#define NPCX_STRPST_JEN0 5
#define NPCX_STRPST_SPI_COMP 7
#define NPCX_RSTCTL_VCC1_RST_STS 0
#define NPCX_RSTCTL_DBGRST_STS 1
#define NPCX_RSTCTL_VCC1_RST_SCRATCH 3
#define NPCX_RSTCTL_LRESET_PLTRST_MODE 5
#define NPCX_RSTCTL_HIPRST_MODE 6
#define NPCX_DEV_CTL4_F_SPI_SLLK 2
#define NPCX_DEV_CTL4_SPI_SP_SEL 4
#define NPCX_DEV_CTL4_WP_IF 5
#define NPCX_DEV_CTL4_VCC1_RST_LK 6
#define NPCX_DEVPU0_I2C0_0_PUE 0
#define NPCX_DEVPU0_I2C0_1_PUE 1
#define NPCX_DEVPU0_I2C1_0_PUE 2
#define NPCX_DEVPU0_I2C2_0_PUE 4
#define NPCX_DEVPU0_I2C3_0_PUE 6
#define NPCX_DEVPU1_F_SPI_PUD_EN 7

/* DEVALT */
/* pin-mux for SPI/FIU */
#define NPCX_DEVALT0_SPIP_SL 0
#define NPCX_DEVALT0_GPIO_NO_SPIP 3
#define NPCX_DEVALT0_F_SPI_CS1_2 4
#define NPCX_DEVALT0_F_SPI_CS1_1 5
#define NPCX_DEVALT0_F_SPI_QUAD 6
#define NPCX_DEVALT0_NO_F_SPI 7

/******************************************************************************/
/* Flash Interface Unit (FIU) Registers */
#define NPCX_FIU_CFG REG8(NPCX_FIU_BASE_ADDR + 0x000)
#define NPCX_BURST_CFG REG8(NPCX_FIU_BASE_ADDR + 0x001)
#define NPCX_RESP_CFG REG8(NPCX_FIU_BASE_ADDR + 0x002)
#define NPCX_SPI_FL_CFG REG8(NPCX_FIU_BASE_ADDR + 0x014)
#define NPCX_UMA_CODE REG8(NPCX_FIU_BASE_ADDR + 0x016)
#define NPCX_UMA_AB0 REG8(NPCX_FIU_BASE_ADDR + 0x017)
#define NPCX_UMA_AB1 REG8(NPCX_FIU_BASE_ADDR + 0x018)
#define NPCX_UMA_AB2 REG8(NPCX_FIU_BASE_ADDR + 0x019)
#define NPCX_UMA_DB0 REG8(NPCX_FIU_BASE_ADDR + 0x01A)
#define NPCX_UMA_DB1 REG8(NPCX_FIU_BASE_ADDR + 0x01B)
#define NPCX_UMA_DB2 REG8(NPCX_FIU_BASE_ADDR + 0x01C)
#define NPCX_UMA_DB3 REG8(NPCX_FIU_BASE_ADDR + 0x01D)
#define NPCX_UMA_CTS REG8(NPCX_FIU_BASE_ADDR + 0x01E)
#define NPCX_UMA_ECTS REG8(NPCX_FIU_BASE_ADDR + 0x01F)
#define NPCX_UMA_DB0_3 REG32(NPCX_FIU_BASE_ADDR + 0x020)
#define NPCX_FIU_RD_CMD REG8(NPCX_FIU_BASE_ADDR + 0x030)
#define NPCX_FIU_DMM_CYC REG8(NPCX_FIU_BASE_ADDR + 0x032)
#define NPCX_FIU_EXT_CFG REG8(NPCX_FIU_BASE_ADDR + 0x033)
#define NPCX_FIU_UMA_AB0_3 REG32(NPCX_FIU_BASE_ADDR + 0x034)

/* FIU register fields */
#define NPCX_RESP_CFG_IAD_EN 0
#define NPCX_RESP_CFG_DEV_SIZE_EX 2
#define NPCX_UMA_CTS_A_SIZE 3
#define NPCX_UMA_CTS_C_SIZE 4
#define NPCX_UMA_CTS_RD_WR 5
#define NPCX_UMA_CTS_DEV_NUM 6
#define NPCX_UMA_CTS_EXEC_DONE 7
#define NPCX_UMA_ECTS_SW_CS0 0
#define NPCX_UMA_ECTS_SW_CS1 1
#define NPCX_UMA_ECTS_SEC_CS 2
#define NPCX_UMA_ECTS_UMA_LOCK 3

/******************************************************************************/
/* KBC Registers */
#define NPCX_HICTRL REG8(NPCX_KBC_BASE_ADDR + 0x000)
#define NPCX_HIIRQC REG8(NPCX_KBC_BASE_ADDR + 0x002)
#define NPCX_HIKMST REG8(NPCX_KBC_BASE_ADDR + 0x004)
#define NPCX_HIKDO REG8(NPCX_KBC_BASE_ADDR + 0x006)
#define NPCX_HIMDO REG8(NPCX_KBC_BASE_ADDR + 0x008)
#define NPCX_KBCVER REG8(NPCX_KBC_BASE_ADDR + 0x009)
#define NPCX_HIKMDI REG8(NPCX_KBC_BASE_ADDR + 0x00A)
#define NPCX_SHIKMDI REG8(NPCX_KBC_BASE_ADDR + 0x00B)

/* KBC register field */
#define NPCX_HICTRL_OBFKIE 0 /* Automatic Serial IRQ1 for KBC */
#define NPCX_HICTRL_OBFMIE 1 /* Automatic Serial IRQ12 for Mouse*/
#define NPCX_HICTRL_OBECIE 2 /* KBC OBE interrupt enable */
#define NPCX_HICTRL_IBFCIE 3 /* KBC IBF interrupt enable */
#define NPCX_HICTRL_PMIHIE 4 /* Automatic Serial IRQ11 for PMC1 */
#define NPCX_HICTRL_PMIOCIE 5 /* PMC1 OBE interrupt enable */
#define NPCX_HICTRL_PMICIE 6 /* PMC1 IBF interrupt enable */
#define NPCX_HICTRL_FW_OBF 7 /* Firmware control over OBF */

#define NPCX_HIKMST_OBF 0 /* KB output buffer is full */

/******************************************************************************/
/* Timer Watch Dog (TWD) Registers */
#define NPCX_TWCFG REG8(NPCX_TWD_BASE_ADDR + 0x000)
#define NPCX_TWCP REG8(NPCX_TWD_BASE_ADDR + 0x002)
#define NPCX_TWDT0 REG16(NPCX_TWD_BASE_ADDR + 0x004)
#define NPCX_T0CSR REG8(NPCX_TWD_BASE_ADDR + 0x006)
#define NPCX_WDCNT REG8(NPCX_TWD_BASE_ADDR + 0x008)
#define NPCX_WDSDM REG8(NPCX_TWD_BASE_ADDR + 0x00A)
#define NPCX_TWMT0 REG16(NPCX_TWD_BASE_ADDR + 0x00C)
#define NPCX_TWMWD REG8(NPCX_TWD_BASE_ADDR + 0x00E)
#define NPCX_WDCP REG8(NPCX_TWD_BASE_ADDR + 0x010)

/* TWD register fields */
#define NPCX_TWCFG_LTWCFG 0
#define NPCX_TWCFG_LTWCP 1
#define NPCX_TWCFG_LTWDT0 2
#define NPCX_TWCFG_LWDCNT 3
#define NPCX_TWCFG_WDCT0I 4
#define NPCX_TWCFG_WDSDME 5
#define NPCX_TWCFG_WDRST_MODE 6
#define NPCX_TWCFG_WDC2POR 7
#define NPCX_T0CSR_RST 0
#define NPCX_T0CSR_TC 1
#define NPCX_T0CSR_WDLTD 3
#define NPCX_T0CSR_WDRST_STS 4
#define NPCX_T0CSR_WD_RUN 5
#define NPCX_T0CSR_TESDIS 7

/******************************************************************************/
/* SPI Register */
#define NPCX_SPI_DATA REG16(NPCX_SPI_BASE_ADDR + 0x00)
#define NPCX_SPI_CTL1 REG16(NPCX_SPI_BASE_ADDR + 0x02)
#define NPCX_SPI_STAT REG8(NPCX_SPI_BASE_ADDR + 0x04)

/* SPI register fields */
#define NPCX_SPI_CTL1_SPIEN 0
#define NPCX_SPI_CTL1_SNM 1
#define NPCX_SPI_CTL1_MOD 2
#define NPCX_SPI_CTL1_EIR 5
#define NPCX_SPI_CTL1_EIW 6
#define NPCX_SPI_CTL1_SCM 7
#define NPCX_SPI_CTL1_SCIDL 8
#define NPCX_SPI_CTL1_SCDV 9
#define NPCX_SPI_STAT_BSY 0
#define NPCX_SPI_STAT_RBF 1

/******************************************************************************/
/* Flash Utiltiy definition */
/*
 *  Flash commands for the W25Q16CV SPI flash
 */
#define CMD_READ_ID 0x9F
#define CMD_READ_MAN_DEV_ID 0x90
#define CMD_WRITE_EN 0x06
#define CMD_WRITE_DIS 0x04
#define CMD_WRITE_STATUS 0x50
#define CMD_READ_STATUS_REG 0x05
#define CMD_READ_STATUS_REG2 0x35
#define CMD_WRITE_STATUS_REG 0x01
#define CMD_FLASH_PROGRAM 0x02
#define CMD_SECTOR_ERASE 0x20
#define CMD_BLOCK_32K_ERASE 0x52
#define CMD_BLOCK_64K_ERASE 0xd8
#define CMD_PROGRAM_UINT_SIZE 0x08
#define CMD_PAGE_SIZE 0x00
#define CMD_READ_ID_TYPE 0x47
#define CMD_FAST_READ 0x0B

/*
 * Status registers for the W25Q16CV SPI flash
 */
#define SPI_FLASH_SR2_SUS BIT(7)
#define SPI_FLASH_SR2_CMP BIT(6)
#define SPI_FLASH_SR2_LB3 BIT(5)
#define SPI_FLASH_SR2_LB2 BIT(4)
#define SPI_FLASH_SR2_LB1 BIT(3)
#define SPI_FLASH_SR2_QE BIT(1)
#define SPI_FLASH_SR2_SRP1 BIT(0)
#define SPI_FLASH_SR1_SRP0 BIT(7)
#define SPI_FLASH_SR1_SEC BIT(6)
#define SPI_FLASH_SR1_TB BIT(5)
#define SPI_FLASH_SR1_BP2 BIT(4)
#define SPI_FLASH_SR1_BP1 BIT(3)
#define SPI_FLASH_SR1_BP0 BIT(2)
#define SPI_FLASH_SR1_WEL BIT(1)
#define SPI_FLASH_SR1_BUSY BIT(0)

/* 0: F_CS0 1: F_CS1_1(GPIO86) 2:F_CS1_2(GPIOA6) */
#define FIU_CHIP_SELECT 0
/* Create UMA control mask */
#define MASK(bit) (0x1 << (bit))
#define A_SIZE 0x03 /* 0: No ADR field 1: 3-bytes ADR field */
#define C_SIZE 0x04 /* 0: 1-Byte CMD field 1:No CMD field */
#define RD_WR 0x05 /* 0: Read 1: Write */
#define DEV_NUM 0x06 /* 0: PVT is used 1: SHD is used */
#define EXEC_DONE 0x07
#define D_SIZE_1 0x01
#define D_SIZE_2 0x02
#define D_SIZE_3 0x03
#define D_SIZE_4 0x04
#define FLASH_SEL MASK(DEV_NUM)

#define MASK_CMD_ONLY (MASK(EXEC_DONE) | FLASH_SEL)
#define MASK_CMD_ADR (MASK(EXEC_DONE) | FLASH_SEL | MASK(A_SIZE))
#define MASK_CMD_ADR_WR \
	(MASK(EXEC_DONE) | FLASH_SEL | MASK(RD_WR) | MASK(A_SIZE) | D_SIZE_1)
#define MASK_RD_1BYTE (MASK(EXEC_DONE) | FLASH_SEL | MASK(C_SIZE) | D_SIZE_1)
#define MASK_RD_2BYTE (MASK(EXEC_DONE) | FLASH_SEL | MASK(C_SIZE) | D_SIZE_2)
#define MASK_RD_3BYTE (MASK(EXEC_DONE) | FLASH_SEL | MASK(C_SIZE) | D_SIZE_3)
#define MASK_RD_4BYTE (MASK(EXEC_DONE) | FLASH_SEL | MASK(C_SIZE) | D_SIZE_4)
#define MASK_CMD_RD_1BYTE (MASK(EXEC_DONE) | FLASH_SEL | D_SIZE_1)
#define MASK_CMD_RD_2BYTE (MASK(EXEC_DONE) | FLASH_SEL | D_SIZE_2)
#define MASK_CMD_RD_3BYTE (MASK(EXEC_DONE) | FLASH_SEL | D_SIZE_3)
#define MASK_CMD_RD_4BYTE (MASK(EXEC_DONE) | FLASH_SEL | D_SIZE_4)
#define MASK_CMD_WR_ONLY (MASK(EXEC_DONE) | FLASH_SEL | MASK(RD_WR))
#define MASK_CMD_WR_1BYTE \
	(MASK(EXEC_DONE) | FLASH_SEL | MASK(RD_WR) | MASK(C_SIZE) | D_SIZE_1)
#define MASK_CMD_WR_2BYTE \
	(MASK(EXEC_DONE) | FLASH_SEL | MASK(RD_WR) | MASK(C_SIZE) | D_SIZE_2)
#define MASK_CMD_WR_ADR \
	(MASK(EXEC_DONE) | FLASH_SEL | MASK(RD_WR) | MASK(A_SIZE))

#endif /* __CROS_EC_REGISTERS_H */
