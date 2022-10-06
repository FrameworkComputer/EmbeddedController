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
 * Monotonic Counter (MTC) device registers
 */
struct mtc_reg {
	/* 0x000: Timing Ticks Count Register */
	volatile uint32_t TTC;
	/* 0x004: Wake-Up Ticks Count Register */
	volatile uint32_t WTC;
};

/* MTC register fields */
#define NPCX_WTC_PTO 30
#define NPCX_WTC_WIE 31

/* SHI (Serial Host Interface) registers */
struct shi_reg {
	volatile uint8_t reserved1;
	/* 0x001: SHI Configuration 1 */
	volatile uint8_t SHICFG1;
	/* 0x002: SHI Configuration 2 */
	volatile uint8_t SHICFG2;
	volatile uint8_t reserved2[2];
	/* 0x005: Event Enable */
	volatile uint8_t EVENABLE;
	/* 0x006: Event Status */
	volatile uint8_t EVSTAT;
	/* 0x007: SHI Capabilities */
	volatile uint8_t CAPABILITY;
	/* 0x008: Status */
	volatile uint8_t STATUS;
	volatile uint8_t reserved3;
	/* 0x00A: Input Buffer Status */
	volatile uint8_t IBUFSTAT;
	/* 0x00B: Output Buffer Status */
	volatile uint8_t OBUFSTAT;
	/* 0x00C: SHI Configuration 3 */
	volatile uint8_t SHICFG3;
	/* 0x00D: SHI Configuration 4 */
	volatile uint8_t SHICFG4;
	/* 0x00E: SHI Configuration 5 */
	volatile uint8_t SHICFG5;
	/* 0x00F: Event Status 2 */
	volatile uint8_t EVSTAT2;
	/* 0x010: Event Enable 2 */
	volatile uint8_t EVENABLE2;
	volatile uint8_t reserved4[15];
	/* 0x20~0x9F: Output Buffer */
	volatile uint8_t OBUF[128];
	/* 0xA0~0x11F: Input Buffer */
	volatile uint8_t IBUF[128];
};

/* SHI register fields */
#define NPCX_SHICFG1_EN 0
#define NPCX_SHICFG1_MODE 1
#define NPCX_SHICFG1_WEN 2
#define NPCX_SHICFG1_AUTIBF 3
#define NPCX_SHICFG1_AUTOBE 4
#define NPCX_SHICFG1_DAS 5
#define NPCX_SHICFG1_CPOL 6
#define NPCX_SHICFG1_IWRAP 7
#define NPCX_SHICFG2_SIMUL 0
#define NPCX_SHICFG2_BUSY 1
#define NPCX_SHICFG2_ONESHOT 2
#define NPCX_SHICFG2_SLWU 3
#define NPCX_SHICFG2_REEN 4
#define NPCX_SHICFG2_RESTART 5
#define NPCX_SHICFG2_REEVEN 6
#define NPCX_EVENABLE_OBEEN 0
#define NPCX_EVENABLE_OBHEEN 1
#define NPCX_EVENABLE_IBFEN 2
#define NPCX_EVENABLE_IBHFEN 3
#define NPCX_EVENABLE_EOREN 4
#define NPCX_EVENABLE_EOWEN 5
#define NPCX_EVENABLE_STSREN 6
#define NPCX_EVENABLE_IBOREN 7
#define NPCX_EVSTAT_OBE 0
#define NPCX_EVSTAT_OBHE 1
#define NPCX_EVSTAT_IBF 2
#define NPCX_EVSTAT_IBHF 3
#define NPCX_EVSTAT_EOR 4
#define NPCX_EVSTAT_EOW 5
#define NPCX_EVSTAT_STSR 6
#define NPCX_EVSTAT_IBOR 7
#define NPCX_STATUS_OBES 6
#define NPCX_STATUS_IBFS 7
#define NPCX_SHICFG3_OBUFLVLDIS 7
#define NPCX_SHICFG4_IBUFLVLDIS 7
#define NPCX_SHICFG5_IBUFLVL2 FIELD(0, 6)
#define NPCX_SHICFG5_IBUFLVL2DIS 7
#define NPCX_EVSTAT2_IBHF2 0
#define NPCX_EVSTAT2_CSNRE 1
#define NPCX_EVSTAT2_CSNFE 2
#define NPCX_EVENABLE2_IBHF2EN 0
#define NPCX_EVENABLE2_CSNREEN 1
#define NPCX_EVENABLE2_CSNFEEN 2

#endif /* _NUVOTON_NPCX_REG_DEF_CROS_H */
