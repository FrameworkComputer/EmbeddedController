/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Registers and interrupts for Intel(R) Integrated Sensor Hub
 */

#ifndef __CROS_EC_REGISTERS_H
#define __CROS_EC_REGISTERS_H

#ifndef __ASSEMBLER__
#include "common.h"
#include "compile_time_macros.h"

/* ISH GPIO has only one port */
#define UNIMPLEMENTED_GPIO_BANK -1

/*
 * ISH3.0 has 3 controllers. Locking must occur by-controller (not by-port).
 */
enum ish_i2c_port {
	ISH_I2C0   = 0,      /* Controller 0 */
	ISH_I2C1   = 1,      /* Controller 1 */
	ISH_I2C2   = 2,      /* Controller 2 */
	I2C_PORT_COUNT,
};

#endif

#define ISH_I2C_PORT_COUNT	I2C_PORT_COUNT

/* In ISH, the devices are mapped to pre-defined addresses in the 32-bit
 * linear address space.
 */
#ifdef CHIP_VARIANT_ISH5P4
#define ISH_I2C0_BASE     0x00000000
#define ISH_I2C1_BASE     0x00002000
#define ISH_I2C2_BASE     0x00004000
#define ISH_UART_BASE     0x08100000
#define ISH_GPIO_BASE     0x00100000
#define ISH_PMU_BASE      0x04200000
#define ISH_OCP_BASE      0xFFFFFFFF
#define ISH_MISC_BASE     0x04400000
#define ISH_DMA_BASE      0x10100000
#define ISH_CCU_BASE      0x04300000
#define ISH_IPC_BASE      0x04100000
#define ISH_WDT_BASE      0x04900000
#define ISH_IOAPIC_BASE   0xFEC00000
#define ISH_HPET_BASE     0x04700000
#define ISH_LAPIC_BASE    0xFEE00000
#else
#define ISH_I2C0_BASE     0x00100000
#define ISH_I2C1_BASE     0x00102000
#define ISH_I2C2_BASE     0x00105000
#define ISH_UART_BASE     0x00103000
#define ISH_GPIO_BASE     0x001F0000
#define ISH_PMU_BASE      0x00800000
#define ISH_OCP_BASE      0x00700000
#define ISH_MISC_BASE     0x00C00000
#define ISH_DMA_BASE      0x00400000
#define ISH_CCU_BASE      0x00900000
#define ISH_IPC_BASE      0x00B00000
#define ISH_WDT_BASE      0xFDE00000
#define ISH_IOAPIC_BASE   0xFEC00000
#define ISH_HPET_BASE     0xFED00000
#define ISH_LAPIC_BASE    0xFEE00000
#endif

/* HW interrupt pins mapped to IOAPIC, from I/O sources */
#ifdef CHIP_VARIANT_ISH5P4
#define ISH_I2C0_IRQ               15
#define ISH_I2C1_IRQ               16
#define ISH_FABRIC_IRQ             12
#define ISH_I2C2_IRQ               17
#define ISH_WDT_IRQ                26
#define ISH_GPIO_IRQ               13
#define ISH_HPET_TIMER1_IRQ        14
#define ISH_IPC_HOST2ISH_IRQ       0
#define ISH_PMU_WAKEUP_IRQ         10
#define ISH_D3_RISE_IRQ            9
#define ISH_D3_FALL_IRQ            9
#define ISH_BME_RISE_IRQ           9
#define ISH_BME_FALL_IRQ           9
#define ISH_IPC_ISH2HOST_CLR_IRQ   0
#define ISH_UART0_IRQ              23
#define ISH_UART1_IRQ              24
#define ISH_RESET_PREP_IRQ         6
#else
#define ISH_I2C0_IRQ               0
#define ISH_I2C1_IRQ               1
#define ISH_FABRIC_IRQ             5
#define ISH_I2C2_IRQ               40
#define ISH_WDT_IRQ                6
#define ISH_GPIO_IRQ               7
#define ISH_HPET_TIMER1_IRQ        8
#define ISH_IPC_HOST2ISH_IRQ       12
#define ISH_PMU_WAKEUP_IRQ         18
#define ISH_D3_RISE_IRQ            19
#define ISH_D3_FALL_IRQ            29
#define ISH_BME_RISE_IRQ           50
#define ISH_BME_FALL_IRQ           51
#define ISH_IPC_ISH2HOST_CLR_IRQ   24
#define ISH_UART0_IRQ              34
#define ISH_UART1_IRQ              35
#define ISH_RESET_PREP_IRQ         62
#endif

/* Interrupt vectors 0-31 are architecture reserved.
 * Vectors 32-255 are user-defined.
 */
#define USER_VEC_START   32
/* Map IRQs to vectors after offset 10 for certain APIC interrupts */
#define IRQ_TO_VEC(irq)  ((irq) + USER_VEC_START + 10)
#define VEC_TO_IRQ(vec)  ((vec) - USER_VEC_START - 10)

/* ISH GPIO Registers */
#define ISH_GPIO_GCCR REG32(ISH_GPIO_BASE + 0x000) /* Direction lock */
#define ISH_GPIO_GPLR REG32(ISH_GPIO_BASE + 0x004) /* Pin level */
#define ISH_GPIO_GPDR REG32(ISH_GPIO_BASE + 0x01C) /* Pin direction */
#define ISH_GPIO_GPSR REG32(ISH_GPIO_BASE + 0x034) /* Output set */
#define ISH_GPIO_GPCR REG32(ISH_GPIO_BASE + 0x04C) /* Output clear */
#define ISH_GPIO_GRER REG32(ISH_GPIO_BASE + 0x064) /* Rising edge detect */
#define ISH_GPIO_GFER REG32(ISH_GPIO_BASE + 0x07C) /* Falling edge detect */
#define ISH_GPIO_GFBR REG32(ISH_GPIO_BASE + 0x094) /* Glitch Filter disable */
#define ISH_GPIO_GIMR REG32(ISH_GPIO_BASE + 0x0AC) /* Interrupt Enable */
#define ISH_GPIO_GISR REG32(ISH_GPIO_BASE + 0x0C4) /* Interrupt Source */
#define ISH_GPIO_GWMR REG32(ISH_GPIO_BASE + 0x100) /* Wake Enable */
#define ISH_GPIO_GWSR REG32(ISH_GPIO_BASE + 0x118) /* Wake Source */
#define ISH_GPIO_GSEC REG32(ISH_GPIO_BASE + 0x130) /* Secure Input */

/* APIC interrupt vectors */
#define ISH_TS_VECTOR              0x20  /* Task switch vector */
#define LAPIC_LVT_ERROR_VECTOR     0x21  /* Clears IOAPIC/LAPIC sync errors */
#define SOFTIRQ_VECTOR             0x22  /* Handles software generated IRQs */
#define LAPIC_SPURIOUS_INT_VECTOR  0xff

/* Interrupt to vector mapping. To be programmed into IOAPIC */
#define ISH_I2C0_VEC               IRQ_TO_VEC(ISH_I2C0_IRQ)
#define ISH_I2C1_VEC               IRQ_TO_VEC(ISH_I2C1_IRQ)
#define ISH_I2C2_VEC               IRQ_TO_VEC(ISH_I2C2_IRQ)
#define ISH_WDT_VEC                IRQ_TO_VEC(ISH_WDT_IRQ)
#define ISH_GPIO_VEC               IRQ_TO_VEC(ISH_GPIO_IRQ)
#define ISH_HPET_TIMER1_VEC        IRQ_TO_VEC(ISH_HPET_TIMER1_IRQ)
#define ISH_IPC_ISH2HOST_CLR_VEC   IRQ_TO_VEC(ISH_IPC_ISH2HOST_CLR_IRQ)
#define ISH_UART0_VEC              IRQ_TO_VEC(ISH_UART0_IRQ)
#define ISH_UART1_VEC              IRQ_TO_VEC(ISH_UART1_IRQ)
#define ISH_IPC_VEC                IRQ_TO_VEC(ISH_IPC_HOST2ISH_IRQ)
#define ISH_RESET_PREP_VEC         IRQ_TO_VEC(ISH_RESET_PREP_IRQ)
#define ISH_PMU_WAKEUP_VEC         IRQ_TO_VEC(ISH_PMU_WAKEUP_IRQ)
#define ISH_D3_RISE_VEC            IRQ_TO_VEC(ISH_D3_RISE_IRQ)
#define ISH_D3_FALL_VEC            IRQ_TO_VEC(ISH_D3_FALL_IRQ)
#define ISH_BME_RISE_VEC           IRQ_TO_VEC(ISH_BME_RISE_IRQ)
#define ISH_BME_FALL_VEC           IRQ_TO_VEC(ISH_BME_FALL_IRQ)
#define ISH_FABRIC_VEC             IRQ_TO_VEC(ISH_FABRIC_IRQ)

#define ISH_DEBUG_UART			UART_PORT_0
#define ISH_DEBUG_UART_IRQ		ISH_UART0_IRQ
#define ISH_DEBUG_UART_VEC		ISH_UART0_VEC

/* IPC_Registers */
#define IPC_PISR			REG32(ISH_IPC_BASE + 0x0)
#define IPC_PISR_HOST2ISH_BIT		BIT(0)

#define IPC_PIMR			REG32(ISH_IPC_BASE + 0x4)
#define IPC_PIMR_HOST2ISH_BIT		BIT(0)
#define IPC_PIMR_ISH2HOST_CLR_BIT	BIT(11)
#define IPC_PIMR_CSME_CSR_BIT		BIT(23)
#define IPC_ISH2HOST_MSG_BASE		REG8_ADDR(ISH_IPC_BASE + 0x60)
#define IPC_ISH_FWSTS			REG32(ISH_IPC_BASE + 0x34)
#define IPC_HOST2ISH_DOORBELL_ADDR	REG32_ADDR(ISH_IPC_BASE + 0x48)
#define IPC_HOST2ISH_MSG_BASE		REG8_ADDR(ISH_IPC_BASE + 0xE0)
#define IPC_ISH2HOST_DOORBELL_ADDR	REG32_ADDR(ISH_IPC_BASE + 0x54)
#define IPC_ISH2PMC_DOORBELL		REG32(ISH_IPC_BASE + 0x58)
#define IPC_ISH2PMC_MSG_BASE		(ISH_IPC_BASE + 0x260)
#define IPC_ISH_RMP0			REG32(ISH_IPC_BASE + 0x360)
#define IPC_ISH_RMP1			REG32(ISH_IPC_BASE + 0x364)
#define IPC_ISH_RMP2			REG32(ISH_IPC_BASE + 0x368)
#define DMA_ENABLED_MASK		BIT(0)
#define IPC_BUSY_CLEAR			REG32(ISH_IPC_BASE + 0x378)
#define IPC_DB_CLR_STS_ISH2HOST_BIT	BIT(0)

#define IPC_UMA_RANGE_LOWER_0		REG32(ISH_IPC_BASE + 0x380)
#define IPC_UMA_RANGE_LOWER_1		REG32(ISH_IPC_BASE + 0x384)
#define IPC_UMA_RANGE_UPPER_0		REG32(ISH_IPC_BASE + 0x388)
#define IPC_UMA_RANGE_UPPER_1		REG32(ISH_IPC_BASE + 0x38C)

/* PMU Registers */
#define PMU_SRAM_PG_EN			REG32(ISH_PMU_BASE + 0x0)
#ifndef CHIP_VARIANT_ISH5P4
#define PMU_D3_STATUS			REG32(ISH_PMU_BASE + 0x4)
#define PMU_D3_BIT_SET			BIT(0)
#define PMU_D3_BIT_RISING_EDGE_STATUS	BIT(1)
#define PMU_D3_BIT_FALLING_EDGE_STATUS	BIT(2)
#define PMU_D3_BIT_RISING_EDGE_MASK	BIT(3)
#define PMU_D3_BIT_FALLING_EDGE_MASK	BIT(4)
#define PMU_BME_BIT_SET		BIT(5)
#define PMU_BME_BIT_RISING_EDGE_STATUS	BIT(6)
#define PMU_BME_BIT_FALLING_EDGE_STATUS BIT(7)
#define PMU_BME_BIT_RISING_EDGE_MASK	BIT(8)
#define PMU_BME_BIT_FALLING_EDGE_MASK	BIT(9)
#else
#define PMU_STATUS_REG_ADDR		(ISH_PMU_BASE + 0xF00)
#define PMU_SCRATCHPAD0_REG_ADDR	(ISH_PMU_BASE + 0xF04)
#define PMU_SCRATCHPAD1_REG_ADDR	(ISH_PMU_BASE + 0xF08)
#define PMU_PG_EN_REG_ADDR		(ISH_PMU_BASE + 0xF10)
#define PMU_PMC_HOST_RST_CTL		REG32(ISH_PMU_BASE + 0xF20)
#define PMU_SW_PG_REQ			REG32(ISH_PMU_BASE + 0xF14)
#define PMU_PMC_PG_WAKE			REG32(ISH_PMU_BASE + 0xF18)
#define PMU_INTERNAL_PCE		REG32(ISH_PMU_BASE + 0xF30)
#define PMU_D3_STATUS			REG32(ISH_PMU_BASE + 0x100)
#define PMU_HOST_RST_B			BIT(0)
#define PMU_PCE_SHADOW_MASK		0x1F
#define PMU_PCE_PG_ALLOWED		BIT(4)
#define PMU_PCE_CHANGE_MASK		BIT(9)
#define PMU_PCE_CHANGE_DETECTED		BIT(8)
#define PMU_PCE_PMCRE			BIT(0)
#define PMU_SW_PG_REQ_B_VAL		BIT(0)
#define PMU_SW_PG_REQ_B_RISE		BIT(1)
#define PMU_SW_PG_REQ_B_FALL		BIT(2)
#define PMU_PMC_PG_WAKE_VAL		BIT(0)
#define PMU_PMC_PG_WAKE_RISE		BIT(1)
#define PMU_PMC_PG_WAKE_FALL		BIT(2)
#define PMU_PCE_PG_ALLOWED		BIT(4)
#define PMU_D0I3_ENABLE_MASK		BIT(23)
#define PMU_D3_BIT_SET			BIT(16)
#define PMU_D3_BIT_RISING_EDGE_STATUS	BIT(17)
#define PMU_D3_BIT_FALLING_EDGE_STATUS	BIT(18)
#define PMU_D3_BIT_RISING_EDGE_MASK	BIT(19)
#define PMU_D3_BIT_FALLING_EDGE_MASK	BIT(20)
#define PMU_BME_BIT_SET			BIT(24)
#define PMU_BME_BIT_RISING_EDGE_STATUS	BIT(25)
#define PMU_BME_BIT_FALLING_EDGE_STATUS	BIT(26)
#define PMU_BME_BIT_RISING_EDGE_MASK	BIT(27)
#define PMU_BME_BIT_FALLING_EDGE_MASK	BIT(28)
#endif

#define PMU_ISH_FABRIC_CNT		REG32(ISH_PMU_BASE + 0x18)

#define PMU_PGCB_CLKGATE_CTRL		REG32(ISH_PMU_BASE + 0x54)

#define PMU_VNN_REQ			REG32(ISH_PMU_BASE + 0x3c)
#define VNN_REQ_IPC_HOST_WRITE		BIT(3) /* Power for IPC host write */

#define PMU_VNN_REQ_ACK		REG32(ISH_PMU_BASE + 0x40)
#define PMU_VNN_REQ_ACK_STATUS		BIT(0) /* VNN req and ack status */

#define PMU_VNNAON_RED			REG32(ISH_PMU_BASE + 0x58)

#define PMU_RST_PREP			REG32(ISH_PMU_BASE + 0x5c)
#define PMU_RST_PREP_GET		BIT(0)
#define PMU_RST_PREP_AVAIL		BIT(1)
#define PMU_RST_PREP_INT_MASK		BIT(31)

#define VNN_ID_DMA0             4
#define VNN_ID_DMA(chan)        (VNN_ID_DMA0 + chan)

/* OCP registers */
#define OCP_IOSF2OCP_BRIDGE      (ISH_OCP_BASE + 0x9400)
#define OCP_AGENT_CONTROL        REG32(OCP_IOSF2OCP_BRIDGE + 0x20)
#define OCP_RESPONSE_TO_DISABLE  0xFFFFF8FF

/* MISC registers */
#define MISC_REG_BASE            ISH_MISC_BASE
#define DMA_REG_BASE             ISH_DMA_BASE
#ifndef CHIP_VARIANT_ISH5P4
#define MISC_CHID_CFG_REG        REG32(MISC_REG_BASE + 0x40)
#define MISC_DMA_CTL_REG(ch)     REG32(MISC_REG_BASE + (4 * (ch)))
#define MISC_SRC_FILLIN_DMA(ch)  REG32(MISC_REG_BASE + 0x20 + (4 * (ch)))
#define MISC_DST_FILLIN_DMA(ch)  REG32(MISC_REG_BASE + 0x80 + (4 * (ch)))
#define MISC_ISH_ECC_ERR_SRESP   REG32(MISC_REG_BASE + 0x94)
#else
#define DMA_MISC_OFFSET          0x1000
#define DMA_MISC_BASE            (DMA_REG_BASE + DMA_MISC_OFFSET)
#define MISC_CHID_CFG_REG        REG32(DMA_MISC_BASE + 0x400)
#define MISC_DMA_CTL_REG(ch)     REG32(DMA_MISC_BASE + (4 * (ch)))
#define MISC_SRC_FILLIN_DMA(ch)  REG32(DMA_MISC_BASE + 0x100 + (4 * (ch)))
#define MISC_DST_FILLIN_DMA(ch)  REG32(DMA_MISC_BASE + 0x200 + (4 * (ch)))
#define MISC_ISH_ECC_ERR_SRESP   REG32(DMA_MISC_BASE + 0x404)
#endif
#define MISC_ISH_RTC_COUNTER0	REG32(ISH_MISC_BASE + 0x70)
#define MISC_ISH_RTC_COUNTER1	REG32(ISH_MISC_BASE + 0x74)

/* DMA registers */
#define DMA_CH_REGS_SIZE              0x58
#define DMA_CLR_BLOCK_REG             REG32(DMA_REG_BASE + 0x340)
#define DMA_CLR_ERR_REG               REG32(DMA_REG_BASE + 0x358)
#define DMA_EN_REG_ADDR               (DMA_REG_BASE + 0x3A0)
#define DMA_EN_REG                    REG32(DMA_EN_REG_ADDR)
#define DMA_CFG_REG                   REG32(DMA_REG_BASE + 0x398)
#define DMA_PSIZE_01                  REG32(DMA_REG_BASE + 0x400)
#define DMA_PSIZE_CHAN0_SIZE          512
#define DMA_PSIZE_CHAN0_OFFSET        0
#define DMA_PSIZE_CHAN1_SIZE          128
#define DMA_PSIZE_CHAN1_OFFSET        13
#define DMA_PSIZE_UPDATE	      BIT(26)
#define DMA_MAX_CHANNEL               4
#define DMA_SAR(chan)                 REG32(chan + 0x000)
#define DMA_DAR(chan)                 REG32(chan + 0x008)
#define DMA_LLP(chan)                 REG32(chan + 0x010)
#define DMA_CTL_LOW(chan)             REG32(chan + 0x018)
#define DMA_CTL_HIGH(chan)            REG32(chan + 0x018 + 0x4)
#define DMA_CTL_INT_ENABLE	      BIT(0)
#define DMA_CTL_DST_TR_WIDTH_SHIFT    1
#define DMA_CTL_SRC_TR_WIDTH_SHIFT    4
#define DMA_CTL_DINC_SHIFT            7
#define DMA_CTL_SINC_SHIFT            9
#define DMA_CTL_ADDR_INC              0
#define DMA_CTL_DEST_MSIZE_SHIFT      11
#define DMA_CTL_SRC_MSIZE_SHIFT       14
#define DMA_CTL_TT_FC_SHIFT           20
#define DMA_CTL_TT_FC_M2M_DMAC        0
#define DMA_ENABLE		      BIT(0)
#define DMA_CH_EN_BIT(n)	      BIT(n)
#define DMA_CH_EN_WE_BIT(n)	      BIT(8 + (n))
#define DMA_MAX_BLOCK_SIZE	      (4096)
#define SRC_TR_WIDTH                  2
#define SRC_BURST_SIZE                3
#define DEST_TR_WIDTH                 2
#define DEST_BURST_SIZE               3

#define PMU_MASK_EVENT			REG32(ISH_PMU_BASE + 0x10)
#define PMU_MASK_EVENT_BIT_GPIO(pin)	BIT(pin)
#define PMU_MASK_EVENT_BIT_HPET	BIT(16)
#define PMU_MASK_EVENT_BIT_IPC		BIT(17)
#define PMU_MASK_EVENT_BIT_D3		BIT(18)
#define PMU_MASK_EVENT_BIT_DMA		BIT(19)
#define PMU_MASK_EVENT_BIT_I2C0	BIT(20)
#define PMU_MASK_EVENT_BIT_I2C1	BIT(21)
#define PMU_MASK_EVENT_BIT_SPI		BIT(22)
#define PMU_MASK_EVENT_BIT_UART	BIT(23)
#define PMU_MASK_EVENT_BIT_ALL		(0xffffffff)

#define PMU_RF_ROM_PWR_CTRL		REG32(ISH_PMU_BASE + 0x30)

#define PMU_LDO_CTRL			REG32(ISH_PMU_BASE + 0x44)
#define PMU_LDO_ENABLE_BIT		BIT(0)
#define PMU_LDO_RETENTION_BIT		BIT(1)
#define PMU_LDO_CALIBRATION_BIT	BIT(2)
#define PMU_LDO_READY_BIT		BIT(3)

/* CCU Registers */
#define CCU_TCG_EN		REG32(ISH_CCU_BASE + 0x0)
#define CCU_BCG_EN		REG32(ISH_CCU_BASE + 0x4)
#ifndef CHIP_VARIANT_ISH5P4
#define CCU_WDT_CD		REG32(ISH_CCU_BASE + 0x8)
#define CCU_RST_HST		REG32(ISH_CCU_BASE + 0x34) /* Reset history */
#define CCU_TCG_ENABLE		REG32(ISH_CCU_BASE + 0x38)
#define CCU_BCG_ENABLE		REG32(ISH_CCU_BASE + 0x3c)
#else
#define CCU_WDT_CD		REG32(ISH_CCU_BASE + 0x7c)
#define CCU_RST_HST		REG32(ISH_CCU_BASE + 0x3c) /* Reset history */
#define CCU_TCG_ENABLE		REG32(ISH_CCU_BASE + 0x40)
#define CCU_BCG_ENABLE		REG32(ISH_CCU_BASE + 0x44)
#endif
#define CCU_BCG_MIA		REG32(ISH_CCU_BASE + 0x4)
#define CCU_BCG_UART		REG32(ISH_CCU_BASE + 0x8)
#define CCU_BCG_I2C		REG32(ISH_CCU_BASE + 0xc)
#define CCU_BCG_SPI		REG32(ISH_CCU_BASE + 0x10)
#define CCU_BCG_GPIO		REG32(ISH_CCU_BASE + 0x14)
#define CCU_BCG_DMA		REG32(ISH_CCU_BASE + 0x28)
#define CCU_AONCG_EN		REG32(ISH_CCU_BASE + 0xdc)
#define CCU_BCG_BIT_MIA	BIT(0)
#define CCU_BCG_BIT_DMA	BIT(1)
#define CCU_BCG_BIT_I2C0	BIT(2)
#define CCU_BCG_BIT_I2C1	BIT(3)
#define CCU_BCG_BIT_SPI	BIT(4)
#define CCU_BCG_BIT_SRAM	BIT(5)
#define CCU_BCG_BIT_HPET	BIT(6)
#define CCU_BCG_BIT_UART	BIT(7)
#define CCU_BCG_BIT_GPIO	BIT(8)
#define CCU_BCG_BIT_I2C2	BIT(9)
#define CCU_BCG_BIT_SPI2	BIT(10)
#define CCU_BCG_BIT_ALL	(0x7ff)

/* Bitmasks for CCU_RST_HST */
#define CCU_SW_RST	BIT(0)  /* Used to indicate SW reset */
#define CCU_WDT_RST	BIT(1)  /* Used to indicate WDT reset */
#define CCU_MIASS_RST	BIT(2)  /* Used to indicate UIA shutdown reset */
#define CCU_SRECC_RST	BIT(3)  /* Used to indicate SRAM ECC reset */

/* Fabric Agent Status register */
#define FABRIC_AGENT_STATUS		REG32(ISH_OCP_BASE + 0x7828)
#define FABRIC_INBAND_ERR_SECONDARY_BIT BIT(29)
#define FABRIC_INBAND_ERR_PRIMARY_BIT	BIT(28)
#define FABRIC_M_ERR_BIT		BIT(24)
#define FABRIC_MIA_STATUS_BIT_ERR	(FABRIC_INBAND_ERR_SECONDARY_BIT | \
					 FABRIC_INBAND_ERR_PRIMARY_BIT | \
					 FABRIC_M_ERR_BIT)

/* CSME Registers */
#ifdef CHIP_VARIANT_ISH5P4
#define SEC_OFFSET			0x10000
#else
#define SEC_OFFSET			0x0
#endif
#define ISH_RST_REG			REG32(ISH_IPC_BASE + SEC_OFFSET + 0x44)
#define IPC_PIMR_CIM_SEC		(ISH_IPC_BASE + SEC_OFFSET + 0x10)

/* IOAPIC registers */
#define IOAPIC_IDX			REG32(ISH_IOAPIC_BASE + 0x0)
#define IOAPIC_WDW			REG32(ISH_IOAPIC_BASE + 0x10)
/* Bare address needed for assembler (ISH_IOAPIC_BASE + 0x40) */
#define IOAPIC_EOI_REG_ADDR		0xFEC00040
#define IOAPIC_EOI_REG			REG32(IOAPIC_EOI_REG_ADDR)

#define IOAPIC_VERSION			(0x1)
#define IOAPIC_IOREDTBL			(0x10)
#define IOAPIC_REDTBL_DELMOD_FIXED	(0x00000000)
#define IOAPIC_REDTBL_DESTMOD_PHYS	(0x00000000)
#define IOAPIC_REDTBL_INTPOL_HIGH	(0x00000000)
#define IOAPIC_REDTBL_INTPOL_LOW	(0x00002000)
#define IOAPIC_REDTBL_IRR		(0x00004000)
#define IOAPIC_REDTBL_TRIGGER_EDGE	(0x00000000)
#define IOAPIC_REDTBL_TRIGGER_LEVEL	(0x00008000)
#define IOAPIC_REDTBL_MASK		(0x00010000)

/* WDT (Watchdog Timer) Registers */
#define WDT_CONTROL			REG32(ISH_WDT_BASE + 0x0)
#define WDT_RELOAD			REG32(ISH_WDT_BASE + 0x4)
#define WDT_VALUES			REG32(ISH_WDT_BASE + 0x8)
#define WDT_CONTROL_ENABLE_BIT		BIT(17)

/* LAPIC registers */
/* Bare address needed for assembler (ISH_LAPIC_BASE + 0xB0) */
#define LAPIC_EOI_REG_ADDR		0xFEE000B0
#define LAPIC_EOI_REG			REG32(LAPIC_EOI_REG_ADDR)
#define LAPIC_ISR_REG			REG32(ISH_LAPIC_BASE + 0x100)
#define LAPIC_ISR_LAST_REG		REG32(ISH_LAPIC_BASE + 0x170)
#define LAPIC_IRR_REG			REG32(ISH_LAPIC_BASE + 0x200)
#define LAPIC_ESR_REG			REG32(ISH_LAPIC_BASE + 0x280)
#define LAPIC_ERR_RECV_ILLEGAL		BIT(6)
#define LAPIC_ICR_REG			REG32(ISH_LAPIC_BASE + 0x300)

/* SRAM control registers */
#ifndef CHIP_VARIANT_ISH5P4
#define ISH_SRAM_CTRL_BASE		0x00500000
#else
#define ISH_SRAM_CTRL_BASE		0x10500000
#endif
#define ISH_SRAM_CTRL_CSFGR		REG32(ISH_SRAM_CTRL_BASE + 0x00)
#define ISH_SRAM_CTRL_INTR		REG32(ISH_SRAM_CTRL_BASE + 0x04)
#define ISH_SRAM_CTRL_INTR_MASK	REG32(ISH_SRAM_CTRL_BASE + 0x08)
#define ISH_SRAM_CTRL_ERASE_CTRL	REG32(ISH_SRAM_CTRL_BASE + 0x0c)
#define ISH_SRAM_CTRL_ERASE_ADDR	REG32(ISH_SRAM_CTRL_BASE + 0x10)
#define ISH_SRAM_CTRL_BANK_STATUS	REG32(ISH_SRAM_CTRL_BASE + 0x2c)

#endif /* __CROS_EC_REGISTERS_H */
