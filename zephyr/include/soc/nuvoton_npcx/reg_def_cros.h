/*
 * Copyright (c) 2020 Nuvoton Technology Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * @file
 * @brief Nuvoton NPCX register structure definitions used by the Chrome OS EC.
 */

#ifndef _NUVOTON_NPCX_REG_DEF_CROS_H
#define _NUVOTON_NPCX_REG_DEF_CROS_H

/*
 * KBS (Keyboard Scan) device registers
 */
struct kbs_reg {
	volatile uint8_t reserved1[4];
	/* 0x004: Keyboard Scan In */
	volatile uint8_t KBSIN;
	/* 0x005: Keyboard Scan In Pull-Up Enable */
	volatile uint8_t KBSINPU;
	/* 0x006: Keyboard Scan Out 0 */
	volatile uint16_t KBSOUT0;
	/* 0x008: Keyboard Scan Out 1 */
	volatile uint16_t KBSOUT1;
	/* 0x00A: Keyboard Scan Buffer Index */
	volatile uint8_t KBS_BUF_INDX;
	/* 0x00B: Keyboard Scan Buffer Data */
	volatile uint8_t KBS_BUF_DATA;
	/* 0x00C: Keyboard Scan Event */
	volatile uint8_t KBSEVT;
	/* 0x00D: Keyboard Scan Control */
	volatile uint8_t KBSCTL;
	/* 0x00E: Keyboard Scan Configuration Index */
	volatile uint8_t KBS_CFG_INDX;
	/* 0x00F: Keyboard Scan Configuration Data */
	volatile uint8_t KBS_CFG_DATA;
};

/* KBS register fields */
#define NPCX_KBSBUFINDX                  0
#define NPCX_KBSEVT_KBSDONE              0
#define NPCX_KBSEVT_KBSERR               1
#define NPCX_KBSCTL_START                0
#define NPCX_KBSCTL_KBSMODE              1
#define NPCX_KBSCTL_KBSIEN               2
#define NPCX_KBSCTL_KBSINC               3
#define NPCX_KBSCTL_KBHDRV_FIELD         FIELD(6, 2)
#define NPCX_KBSCFGINDX                  0
/* Index of 'Automatic Scan' configuration register */
#define KBS_CFG_INDX_DLY1                0 /* Keyboard Scan Delay T1 Byte */
#define KBS_CFG_INDX_DLY2                1 /* Keyboard Scan Delay T2 Byte */
#define KBS_CFG_INDX_RTYTO               2 /* Keyboard Scan Retry Timeout */
#define KBS_CFG_INDX_CNUM                3 /* Keyboard Scan Columns Number */
#define KBS_CFG_INDX_CDIV                4 /* Keyboard Scan Clock Divisor */

/*
 * Flash Interface Unit (FIU) device registers
 */
struct fiu_reg {
	/* 0x001: Burst Configuration */
	volatile uint8_t BURST_CFG;
	/* 0x002: FIU Response Configuration */
	volatile uint8_t RESP_CFG;
	volatile uint8_t reserved1[18];
	/* 0x014: SPI Flash Configuration */
	volatile uint8_t SPI_FL_CFG;
	volatile uint8_t reserved2;
	/* 0x016: UMA Code Byte */
	volatile uint8_t UMA_CODE;
	/* 0x017: UMA Address Byte 0 */
	volatile uint8_t UMA_AB0;
	/* 0x018: UMA Address Byte 1 */
	volatile uint8_t UMA_AB1;
	/* 0x019: UMA Address Byte 2 */
	volatile uint8_t UMA_AB2;
	/* 0x01A: UMA Data Byte 0 */
	volatile uint8_t UMA_DB0;
	/* 0x01B: UMA Data Byte 1 */
	volatile uint8_t UMA_DB1;
	/* 0x01C: UMA Data Byte 2 */
	volatile uint8_t UMA_DB2;
	/* 0x01D: UMA Data Byte 3 */
	volatile uint8_t UMA_DB3;
	/* 0x01E: UMA Control and Status */
	volatile uint8_t UMA_CTS;
	/* 0x01F: UMA Extended Control and Status */
	volatile uint8_t UMA_ECTS;
	/* 0x020: UMA Data Bytes 0-3 */
	volatile uint32_t UMA_DB0_3;
	volatile uint8_t reserved3[2];
	/* 0x026: CRC Control Register */
	volatile uint8_t CRCCON;
	/* 0x027: CRC Entry Register */
	volatile uint8_t CRCENT;
	/* 0x028: CRC Initialization and Result Register */
	volatile uint32_t CRCRSLT;
	volatile uint8_t reserved4[4];
	/* 0x030: FIU Read Command */
	volatile uint8_t FIU_RD_CMD;
	volatile uint8_t reserved5;
	/* 0x032: FIU Dummy Cycles */
	volatile uint8_t FIU_DMM_CYC;
	/* 0x033: FIU Extended Configuration */
	volatile uint8_t FIU_EXT_CFG;
};

/* FIU register fields */
#define NPCX_RESP_CFG_IAD_EN             0
#define NPCX_RESP_CFG_DEV_SIZE_EX        2
#define NPCX_UMA_CTS_A_SIZE              3
#define NPCX_UMA_CTS_C_SIZE              4
#define NPCX_UMA_CTS_RD_WR               5
#define NPCX_UMA_CTS_DEV_NUM             6
#define NPCX_UMA_CTS_EXEC_DONE           7
#define NPCX_UMA_ECTS_SW_CS0             0
#define NPCX_UMA_ECTS_SW_CS1             1
#define NPCX_UMA_ECTS_SEC_CS             2
#define NPCX_UMA_ECTS_UMA_LOCK           3

/* UMA fields selections */
#define UMA_FLD_ADDR     BIT(NPCX_UMA_CTS_A_SIZE)  /* 3-bytes ADR field */
#define UMA_FLD_NO_CMD   BIT(NPCX_UMA_CTS_C_SIZE)  /* No 1-Byte CMD field */
#define UMA_FLD_WRITE    BIT(NPCX_UMA_CTS_RD_WR)   /* Write transaction */
#define UMA_FLD_SHD_SL   BIT(NPCX_UMA_CTS_DEV_NUM) /* Shared flash selected */
#define UMA_FLD_EXEC     BIT(NPCX_UMA_CTS_EXEC_DONE)

#define UMA_FIELD_DATA_1 0x01
#define UMA_FIELD_DATA_2 0x02
#define UMA_FIELD_DATA_3 0x03
#define UMA_FIELD_DATA_4 0x04

/* UMA code for transaction */
#define UMA_CODE_CMD_ONLY       (UMA_FLD_EXEC | UMA_FLD_SHD_SL)
#define UMA_CODE_CMD_ADR        (UMA_FLD_EXEC | UMA_FLD_ADDR | \
					UMA_FLD_SHD_SL)
#define UMA_CODE_CMD_RD_BYTE(n) (UMA_FLD_EXEC | UMA_FIELD_DATA_##n | \
					UMA_FLD_SHD_SL)
#define UMA_CODE_RD_BYTE(n)     (UMA_FLD_EXEC | UMA_FLD_NO_CMD | \
					UMA_FIELD_DATA_##n | UMA_FLD_SHD_SL)
#define UMA_CODE_CMD_WR_ONLY    (UMA_FLD_EXEC | UMA_FLD_WRITE | \
					UMA_FLD_SHD_SL)
#define UMA_CODE_CMD_WR_BYTE(n) (UMA_FLD_EXEC | UMA_FLD_WRITE | \
					UMA_FIELD_DATA_##n | UMA_FLD_SHD_SL)
#define UMA_CODE_CMD_WR_ADR     (UMA_FLD_EXEC | UMA_FLD_WRITE | UMA_FLD_ADDR | \
				UMA_FLD_SHD_SL)

#define UMA_CODE_CMD_ADR_WR_BYTE(n) (UMA_FLD_EXEC | UMA_FLD_WRITE | \
					UMA_FLD_ADDR | UMA_FIELD_DATA_##n | \
					UMA_FLD_SHD_SL)
#endif /* _NUVOTON_NPCX_REG_DEF_CROS_H */
