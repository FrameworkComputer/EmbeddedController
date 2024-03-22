/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Register map for SCP
 */

#ifndef __CROS_EC_REGISTERS_H
#define __CROS_EC_REGISTERS_H

#include "common.h"
#include "compile_time_macros.h"

/* IRQ numbers */
#define SCP_IRQ_IPC0 0
#define SCP_IRQ_IPC1 1
#define SCP_IRQ_IPC2 2
#define SCP_IRQ_IPC3 3
#define SCP_IRQ_SPM 4
#define SCP_IRQ_CIRQ 5
#define SCP_IRQ_EINT 6
#define SCP_IRQ_PMIC 7
#define SCP_IRQ_UART0 8
#define SCP_IRQ_UART1 9
#define SCP_IRQ_I2C0 10
#define SCP_IRQ_I2C1 11
#define SCP_IRQ_I2C2 12
#define SCP_IRQ_CLOCK 13
#define SCP_IRQ_MAD_FIFO 14
#define SCP_IRQ_TIMER0 15
#define SCP_IRQ_TIMER1 16
#define SCP_IRQ_TIMER2 17
#define SCP_IRQ_TIMER3 18
#define SCP_IRQ_TIMER4 19
#define SCP_IRQ_TIMER5 20
#define SCP_IRQ_TIMER_STATUS 21
#define SCP_IRQ_UART0_RX 22
#define SCP_IRQ_UART1_RX 23
#define SCP_IRQ_DMA 24
#define SCP_IRQ_AUDIO 25
#define SCP_IRQ_MD1_F216 26
#define SCP_IRQ_MD1 27
#define SCP_IRQ_C2K 28
#define SCP_IRQ_SPI0 29
#define SCP_IRQ_SPI1 30
#define SCP_IRQ_SPI2 31
#define SCP_IRQ_AP_EINT 32
#define SCP_IRQ_DEBUG 33
#define SCP_CCIF0 34
#define SCP_CCIF1 35
#define SCP_CCIF2 36
#define SCP_IRQ_WDT 37
#define SCP_IRQ_USB0 38
#define SCP_IRQ_USB1 39
#define SCP_IRQ_TWAM 40
#define SCP_IRQ_INFRA 41
#define SCP_IRQ_HWDVFS_HIGH 42
#define SCP_IRQ_HWDVFS_LOW 43
#define SCP_IRQ_CLOCK2 44
/* RESERVED 45-52 */
#define SCP_IRQ_AP_EINT2 53
#define SCP_IRQ_AP_EINT_EVT 54
#define SCP_IRQ_MAD_DATA 55

#define SCP_CFG_BASE 0x405C0000

#define SCP_AP_RESOURCE REG32(SCP_CFG_BASE + 0x04)
#define SCP_BUS_RESOURCE REG32(SCP_CFG_BASE + 0x08)

#ifdef CHIP_VARIANT_MT8186
#define SCP_TCM_LOCK_CFG (CFGREG_BASE + 0x10)
#endif

/* SCP to host interrupt */
#define SCP_HOST_INT REG32(SCP_CFG_BASE + 0x1C)
#define IPC_SCP2HOST_SSHUB 0xff0000
#define WDT_INT 0x100
#define IPC_SCP2HOST 0xff
#define IPC_SCP2HOST_BIT 0x1

/* SCP to SPM interrupt */
#define SCP_SPM_INT REG32(SCP_CFG_BASE + 0x20)
#define SPM_INT_A2SPM BIT(0)
#define SPM_INT_B2SPM BIT(1)
#define SCP_SPM_INT2 REG32(SCP_CFG_BASE + 0x24)

/*
 * AP side to SCP IPC
 * APMCU writes 1 bit to trigger ith IPC to SCP.
 * SCP writes 1 bit to ith bit to clear ith IPC.
 */
#define SCP_GIPC_IN REG32(SCP_CFG_BASE + 0x28)
#define SCP_GIPC_IN_CLEAR_IPCN(n) (1 << (n))
#define SCP_GPIC_IN_CLEAR_ALL 0x7FFFF
#define SCP_CONN_INT REG32(SCP_CFG_BASE + 0x2C)

/* 8 general purpose registers, 0 ~ 7 */
#define SCP_GPR REG32_ADDR(SCP_CFG_BASE + 0x50)
/*
 * SCP_GPR[0]
 *   b15-b0   : scratchpad
 *   b31-b16  : saved flags
 * SCP_GPR[1]
 *   b15-b0   : power on state
 */
#define SCP_PWRON_STATE SCP_GPR[1]
#define PWRON_DEFAULT 0xdee80000
#define PWRON_WATCHDOG BIT(0)
#define PWRON_RESET BIT(1)
/* AP defined features */
#define SCP_EXPECTED_FREQ SCP_GPR[3]
#define SCP_CURRENT_FREQ SCP_GPR[4]
#define SCP_REBOOT SCP_GPR[5]
#define READY_TO_REBOOT 0x34
#define REBOOT_OK 1

/* Miscellaneous */
#define SCP_SEMAPHORE REG32(SCP_CFG_BASE + 0x90)
#define CORE_CONTROL REG32(SCP_CFG_BASE + 0xA0)
#define CORE_FPU_FLAGS REG32(SCP_CFG_BASE + 0xA4)
#define CORE_REG_SP REG32(SCP_CFG_BASE + 0xA8)
#define CORE_REG_LR REG32(SCP_CFG_BASE + 0xAC)
#define CORE_REG_PSP REG32(SCP_CFG_BASE + 0xB0)
#define CORE_REG_PC REG32(SCP_CFG_BASE + 0xB4)
#define SCP_SLP_PROTECT_CFG REG32(SCP_CFG_BASE + 0xC8)
#define P_CACHE_SLP_PROT_EN BIT(3)
#define D_CACHE_SLP_PROT_EN BIT(4)
#define SCP_ONE_TIME_LOCK REG32(SCP_CFG_BASE + 0xDC)
#define SCP_SECURE_CTRL REG32(SCP_CFG_BASE + 0xE0)

#ifdef CHIP_VARIANT_MT8186
#define JTAG_DBG_REQ_BIT BIT(3)
#define DISABLE_REMAP BIT(31)
#else
#define DISABLE_REMAP BIT(22)
#endif

#define ENABLE_SPM_MASK_VREQ BIT(28)
#define DISABLE_JTAG BIT(21)
#define DISABLE_AP_TCM BIT(20)
#define SCP_SYS_CTRL REG32(SCP_CFG_BASE + 0xE4)
#define DDREN_FIX_VALUE BIT(28)
#define AUTO_DDREN BIT(18)

/* Memory remap control */
/*
 * EXT_ADDR3[29:24] remap register for addr msb 31~28 equal to 0x7
 * EXT_ADDR2[21:16] remap register for addr msb 31~28 equal to 0x6
 * EXT_ADDR1[13:8]  remap register for addr msb 31~28 equal to 0x3
 * EXT_ADDR0[5:0]   remap register for addr msb 31~28 equal to 0x2
 */
#define SCP_REMAP_CFG1 REG32(SCP_CFG_BASE + 0x120)
/*
 * EXT_ADDR7[29:24] remap register for addr msb 31~28 equal to 0xb
 * EXT_ADDR6[21:16] remap register for addr msb 31~28 equal to 0xa
 * EXT_ADDR5[13:8]  remap register for addr msb 31~28 equal to 0x9
 * EXT_ADDR4[5:0]   remap register for addr msb 31~28 equal to 0x8
 */
#define SCP_REMAP_CFG2 REG32(SCP_CFG_BASE + 0x124)
/*
 * AUD_ADDR[31:28]  remap register for addr msb 31~28 equal to 0xd
 * EXT_ADDR10[21:16]remap register for addr msb 31~28 equal to 0xf
 * EXT_ADDR9[13:8]  remap register for addr msb 31~28 equal to 0xe
 * EXT_ADDR8[5:0]   remap register for addr msb 31~28 equal to 0xc
 */
#define SCP_REMAP_CFG3 REG32(SCP_CFG_BASE + 0x128)

#define SCP_REMAP_ADDR_SHIFT 28
#define SCP_REMAP_ADDR_LSB_MASK (BIT(SCP_REMAP_ADDR_SHIFT) - 1)
#define SCP_REMAP_ADDR_MSB_MASK ((~0) << SCP_REMAP_ADDR_SHIFT)

/* Cached memory remap control */
#define SCP_L1_REMAP_CFG0 REG32(SCP_CFG_BASE + 0x12C)
/*
 * L1C_EXT_ADDR1[29:16] remap register for addr msb 31~20 equal to 0x401
 * L1C_EXT_ADDR0[13:0]  remap register for addr msb 31~20 equal to 0x400
 */
#define SCP_L1_REMAP_CFG1 REG32(SCP_CFG_BASE + 0x130)
/*
 * L1C_EXT_ADDR3[29:16] remap register for addr msb 31~20 equal to 0x403
 * L1C_EXT_ADDR2[13:0]  remap register for addr msb 31~20 equal to 0x402
 */
#define SCP_L1_REMAP_CFG2 REG32(SCP_CFG_BASE + 0x134)
/*
 * L1C_EXT_ADDR5[29:16] remap register for addr msb 31~20 equal to 0x405
 * L1C_EXT_ADDR4[13:0]  remap register for addr msb 31~20 equal to 0x404
 */
#define SCP_L1_REMAP_CFG3 REG32(SCP_CFG_BASE + 0x138)
/*
 * L1C_EXT_ADDR_OTHER1[13:8] Remap register for addr msb 31 to 28 equal to 0x1
 * L1C_EXT_ADDR_OTHER0[5:0] Remap register for addr msb 31 to 28 equal to 0x0
 * and not overlap with L1C_EXT_ADDR0 to L1C_EXT_ADDR7
 */
#define SCP_L1_REMAP_OTHER REG32(SCP_CFG_BASE + 0x13C)

#define SCP_L1_EXT_ADDR_SHIFT 20
#define SCP_L1_EXT_ADDR_OTHER_SHIFT 28
#define SCP_L1_EXT_ADDR_OTHER_LSB_MASK (BIT(SCP_REMAP_ADDR_SHIFT) - 1)
#define SCP_L1_EXT_ADDR_OTHER_MSB_MASK ((~0) << SCP_REMAP_ADDR_SHIFT)

/* Audio/voice FIFO */
#define SCP_AUDIO_BASE (SCP_CFG_BASE + 0x1000)
#define SCP_VIF_FIFO_EN REG32(SCP_AUDIO_BASE)
#define VIF_FIFO_RSTN (1 << 0)
#define VIF_FIFO_IRQ_EN (1 << 1)
#define VIF_FIFO_SRAM_PWR (1 << 2)
#define VIF_FIFO_RSTN_STATUS (1 << 4)
#define SCP_VIF_FIFO_STATUS REG32(SCP_AUDIO_BASE + 0x04)
#define VIF_FIFO_VALID (1 << 0)
#define VIF_FIFO_FULL (1 << 4)
#define VIF_FIFO_LEVEL(status) (((status) >> 16) & 0xff)
#define VIF_FIFO_MAX 256
#define SCP_VIF_FIFO_DATA REG32(SCP_AUDIO_BASE + 0x08)
#define SCP_VIF_FIFO_DATA_THRE REG32(SCP_AUDIO_BASE + 0x0C)
/* VIF IRQ status clears on read! */
#define SCP_VIF_FIFO_IRQ_STATUS REG32(SCP_AUDIO_BASE + 0x10)
/* Audio/voice serial interface */
#define SCP_RXIF_CFG0 REG32(SCP_AUDIO_BASE + 0x14)
#define RXIF_CFG0_RESET_VAL 0x2A130001
#define RXIF_AFE_ON (1 << 0)
#define RXIF_SCKINV (1 << 1)
#define RXIF_RG_DL_2_IN_MODE(mode) (((mode) & 0xf) << 8)
#define RXIF_RGDL2_AMIC_16K (0x1 << 8)
#define RXIF_RGDL2_DMIC_16K (0x2 << 8)
#define RXIF_RGDL2_DMIC_LP_16K (0x3 << 8)
#define RXIF_RGDL2_AMIC_32K (0x5 << 8)
#define RXIF_RGDL2_MASK (0xf << 8)
#define RXIF_UP8X_RSP(p) (((p) & 0x7) << 16)
#define RXIF_RG_RX_READEN (1 << 19)
#define RXIF_MONO (1 << 20)
#define RXIF_RG_CLK_A16P7K_EN(cnt) (((cnt) & 0xff) << 24)
#define SCP_RXIF_CFG1 REG32(SCP_AUDIO_BASE + 0x18)
#define RXIF_CFG1_RESET_VAL 0x33180014
#define RXIF_RG_SYNC_CNT_TBL(t) ((t) & 0x1ff)
#define RXIF_RG_SYNC_SEARCH_TBL(t) (((t) & 0x1f) << 16)
#define RXIF_RG_SYNC_CHECK_ROUND(r) (((r) & 0xf) << 24)
#define RXIF_RG_INSYNC_CHECK_ROUND(r) (((r) & 0xf) << 28)
#define SCP_RXIF_CFG2 REG32(SCP_AUDIO_BASE + 0x1C)
#define RXIF_SYNC_WORD(w) ((w) & 0xffff)
#define SCP_RXIF_OUT REG32(SCP_AUDIO_BASE + 0x20)
#define SCP_RXIF_STATUS REG32(SCP_AUDIO_BASE + 0x24)
#define SCP_RXIF_IRQ_EN REG32(SCP_AUDIO_BASE + 0x28)

/* INTC control */
#define SCP_INTC_BASE (SCP_CFG_BASE + 0x2000)
#define SCP_INTC_IRQ_STATUS REG32(SCP_INTC_BASE)
#define SCP_INTC_IRQ_ENABLE REG32(SCP_INTC_BASE + 0x04)
#define IPC0_IRQ_EN BIT(0)
#define SCP_INTC_IRQ_OUTPUT REG32(SCP_INTC_BASE + 0x08)
#define SCP_INTC_IRQ_WAKEUP REG32(SCP_INTC_BASE + 0x0C)
#define SCP_INTC_NMI REG32(SCP_INTC_BASE + 0x10)
#define SCP_INTC_SPM_WAKEUP REG32(SCP_INTC_BASE + 0x14)
#define SCP_INTC_SPM_WAKEUP_MSB REG32(SCP_INTC_BASE + 0x18)
#define SCP_INTC_UART_RX_IRQ REG32(SCP_INTC_BASE + 0x1C)
#define SCP_INTC_IRQ_STATUS_MSB REG32(SCP_INTC_BASE + 0x80)
#define SCP_INTC_IRQ_ENABLE_MSB REG32(SCP_INTC_BASE + 0x84)
#define SCP_INTC_IRQ_OUTPUT_MSB REG32(SCP_INTC_BASE + 0x88)
#define SCP_INTC_IRQ_WAKEUP_MSB REG32(SCP_INTC_BASE + 0x8C)

/* Timer */
#define NUM_TIMERS 6
#define SCP_TIMER_BASE(n) (SCP_CFG_BASE + 0x3000 + (0x10 * (n)))
#define SCP_TIMER_EN(n) REG32(SCP_TIMER_BASE(n))
#define SCP_TIMER_RESET_VAL(n) REG32(SCP_TIMER_BASE(n) + 0x04)
#define SCP_TIMER_VAL(n) REG32(SCP_TIMER_BASE(n) + 0x08)
#define SCP_TIMER_IRQ_CTRL(n) REG32(SCP_TIMER_BASE(n) + 0x0C)
#define TIMER_IRQ_ENABLE BIT(0)
#define TIMER_IRQ_STATUS BIT(4)
#define TIMER_IRQ_CLEAR BIT(5)
#define SCP_TIMER_CLK_SEL(n) REG32(SCP_TIMER_BASE(n) + 0x40)
#define TIMER_CLK_32K (0 << 4)
#define TIMER_CLK_26M BIT(4)
#define TIMER_CLK_BCLK (2 << 4)
#define TIMER_CLK_PCLK (3 << 4)
#define TIMER_CLK_MASK (3 << 4)
/* OS timer */
#define SCP_OSTIMER_BASE (SCP_CFG_BASE + 0x3080)
#define SCP_OSTIMER_CON REG32(SCP_OSTIMER_BASE)
#define SCP_OSTIMER_INIT_L REG32(SCP_OSTIMER_BASE + 0x04)
#define SCP_OSTIMER_INIT_H REG32(SCP_OSTIMER_BASE + 0x08)
#define SCP_OSTIMER_VAL_L REG32(SCP_OSTIMER_BASE + 0x0C)
#define SCP_OSTIMER_VAL_H REG32(SCP_OSTIMER_BASE + 0x10)
#define SCP_OSTIMER_TVAL REG32(SCP_OSTIMER_BASE + 0x14)
#define SCP_OSTIMER_IRQ_ACK REG32(SCP_OSTIMER_BASE + 0x18)
#define OSTIMER_LATCH0_EN BIT(5)
#define OSTIMER_LATCH1_EN BIT(13)
#define OSTIMER_LATCH2_EN BIT(21)
#define SCP_OSTIMER_LATCH_CTRL REG32(SCP_OSTIMER_BASE + 0x20)
#define SCP_OSTIMER_LATCH0_L REG32(SCP_OSTIMER_BASE + 0x24)
#define SCP_OSTIMER_LATCH0_H REG32(SCP_OSTIMER_BASE + 0x28)
#define SCP_OSTIMER_LATCH1_L REG32(SCP_OSTIMER_BASE + 0x2C)
#define SCP_OSTIMER_LATCH1_H REG32(SCP_OSTIMER_BASE + 0x30)
#define SCP_OSTIMER_LATCH2_L REG32(SCP_OSTIMER_BASE + 0x34)
#define SCP_OSTIMER_LATCH2_H REG32(SCP_OSTIMER_BASE + 0x38)

/* Clock, PMIC wrapper, etc. */
#define SCP_CLK_BASE (SCP_CFG_BASE + 0x4000)
#define SCP_CLK_SEL REG32(SCP_CLK_BASE)
#define CLK_SEL_SYS_26M 0
#define CLK_SEL_32K 1
#define CLK_SEL_ULPOSC_2 2
#define CLK_SEL_ULPOSC_1 3

#define SCP_CLK_EN REG32(SCP_CLK_BASE + 0x04)
#define EN_CLK_SYS BIT(0) /* System clock */
#define EN_CLK_HIGH BIT(1) /* ULPOSC */
#define CG_CLK_HIGH BIT(2)
#define EN_SYS_IRQ BIT(16)
#define EN_HIGH_IRQ BIT(17)
#define SCP_CLK_SAFE_ACK REG32(SCP_CLK_BASE + 0x08)
#define SCP_CLK_ACK REG32(SCP_CLK_BASE + 0x0C)
#define SCP_CLK_IRQ_ACK REG32(SCP_CLK_BASE + 0x10)
/*
 * System clock counter value.
 * CLK_SYS_VAL[9:0] System clock counter initial/reset value.
 */
#define SCP_CLK_SYS_VAL REG32(SCP_CLK_BASE + 0x14)
#define CLK_SYS_VAL_MASK 0x3ff /* 10 bits */
#define CLK_SYS_VAL(n) ((n) & CLK_SYS_VAL_MASK)
/*
 * ULPOSC clock counter value.
 * CLK_HIGH_VAL[9:0] ULPOSC clock counter initial/reset value.
 */
#define SCP_CLK_HIGH_VAL REG32(SCP_CLK_BASE + 0x18)
#define CLK_HIGH_VAL_MASK 0x3ff /* 10 bits */
#define CLK_HIGH_VAL(n) ((n) & CLK_HIGH_VAL_MASK)
#define SCP_CLK_SLOW_SEL REG32(SCP_CLK_BASE + 0x1C)
#define CKSW_SEL_SLOW_MASK 0x3
#define CKSW_SEL_SLOW_DIV_MASK 0x30
#define CKSW_SEL_SLOW_SYS_CLK 0
#define CKSW_SEL_SLOW_32K_CLK 1
#define CKSW_SEL_SLOW_ULPOSC2_CLK 2
#define CKSW_SEL_SLOW_ULPOSC1_CLK 3
/*
 * Sleep mode control.
 * VREQ_COUNT[7:1] Number of cycles to wait when requesting PMIC to raise the
 *     voltage after returning from  sleep mode.
 */
#define SCP_CLK_SLEEP_CTRL REG32(SCP_CLK_BASE + 0x20)
#define EN_SLEEP_CTRL BIT(0)

#ifdef CHIP_VARIANT_MT8186
#define VREQ_COUNTER_MASK 0x7F
#else
#define VREQ_COUNTER_MASK 0xfe
#endif

#define VREQ_COUNTER_VAL(v) (((v) << 1) & VREQ_COUNTER_MASK)
#define SPM_SLEEP_MODE BIT(8)
#define SPM_SLEEP_MODE_CLK_AO BIT(9)
#define SCP_CLK_DIV_SEL REG32(SCP_CLK_BASE + 0x24)
#define CLK_DIV1 0
#define CLK_DIV2 1
#define CLK_DIV4 2
#define CLK_DIV8 3
#define SCP_CLK_DEBUG REG32(SCP_CLK_BASE + 0x28)
#define SCP_CLK_SRAM_POWERDOWN REG32(SCP_CLK_BASE + 0x2C)
#define SCP_CLK_GATE REG32(SCP_CLK_BASE + 0x30)
#define CG_TIMER_M BIT(0)
#define CG_TIMER_B BIT(1)
#define CG_MAD_M BIT(2)
#define CG_I2C_M BIT(3)
#define CG_I2C_B BIT(4)
#define CG_GPIO_M BIT(5)
#define CG_AP2P_M BIT(6)
#define CG_UART_M BIT(7)
#define CG_UART_B BIT(8)
#define CG_UART_RSTN BIT(9)
#define CG_UART1_M BIT(10)
#define CG_UART1_B BIT(11)
#define CG_UART1_RSTN BIT(12)
#define CG_SPI0 BIT(13)
#define CG_SPI1 BIT(14)
#define CG_SPI2 BIT(15)
#define CG_DMA_CH0 BIT(16)
#define CG_DMA_CH1 BIT(17)
#define CG_DMA_CH2 BIT(18)
#define CG_DMA_CH3 BIT(19)
#define CG_TWAM BIT(20)
#define CG_CACHE_I_CTRL BIT(21)
#define CG_CACHE_D_CTRL BIT(22)
#define SCP_PMICW_CTRL REG32(SCP_CLK_BASE + 0x34)
#define PMICW_SLEEP_REQ BIT(0)
#define PMICW_SLEEP_ACK BIT(4)
#define PMICW_CLK_MUX BIT(8)
#define PMICW_DCM BIT(9)
#define SCP_SLEEP_WAKE_DEBUG REG32(SCP_CLK_BASE + 0x38)
#define SCP_DCM_EN REG32(SCP_CLK_BASE + 0x3C)
#define SCP_WAKE_CKSW REG32(SCP_CLK_BASE + 0x40)

#define WAKE_CKSW_SEL_SLOW_MASK 0x30
#define WAKE_CKSW_SEL_SLOW_DEFAULT 0x10

#define WAKE_CKSW_SEL_NORMAL_MASK 0x3
#define SCP_CLK_UART REG32(SCP_CLK_BASE + 0x44)
#define CLK_UART_SEL_MASK 0x3
#define CLK_UART_SEL_26M 0x0
#define CLK_UART_SEL_32K 0x1
/* This is named ulposc_div_to_26m in datasheet */
#define CLK_UART_SEL_ULPOSC1_DIV10 0x2
#define CLK_UART1_SEL_MASK (0x3 << 16)
#define CLK_UART1_SEL_26M (0x0 << 16)
#define CLK_UART1_SEL_32K (0x1 << 16)
/* This is named ulposc_div_to_26m in datasheet */
#define CLK_UART1_SEL_ULPOSC1_DIV10 (0x2 << 16)
#define SCP_CLK_BCLK REG32(SCP_CLK_BASE + 0x48)
#define CLK_BCLK_SEL_MASK 0x3
#define CLK_BCLK_SEL_SYS_DIV8 0x0
#define CLK_BCLK_SEL_32K 0x1
#define CLK_BCLK_SEL_ULPOSC1_DIV8 0x2
#define SCP_CLK_SPI_BCK REG32(SCP_CLK_BASE + 0x4C)
#define SCP_CLK_DIV_CNT REG32(SCP_CLK_BASE + 0x50)
#define SCP_CPU_VREQ REG32(SCP_CLK_BASE + 0x54)
#define CPU_VREQ_HW_MODE 0x10001

#ifdef CHIP_VARIANT_MT8186
#define VREQ_SEL BIT(0)
#define VREQ_PMIC_WRAP_SEL BIT(1)
#define VREQ_VALUE BIT(4)
#define VREQ_EXT_SEL BIT(8)
#define VREQ_DVFS_SEL BIT(16)
#define VREQ_DVFS_VALUE BIT(20)
#define VREQ_DVFS_EXT_SEL BIT(24)
#define VREQ_SRCLKEN_SEL BIT(27)
#define VREQ_SRCLKEN_VALUE BIT(28)
#endif

#define SCP_CLK_CLEAR REG32(SCP_CLK_BASE + 0x58)
#define SCP_CLK_HIGH_CORE REG32(SCP_CLK_BASE + 0x5C)
#define CLK_HIGH_CORE_CG (1 << 1)
#define SCP_SLEEP_IRQ2 REG32(SCP_CLK_BASE + 0x64)
#define SCP_CLK_ON_CTRL REG32(SCP_CLK_BASE + 0x6C)
#define HIGH_AO BIT(0)
#define HIGH_CG_AO BIT(2)
#define HIGH_CORE_AO BIT(4)
#define HIGH_CORE_DIS_SUB BIT(5)
#define HIGH_CORE_CG_AO BIT(6)
#define HIGH_FINAL_VAL_MASK 0x1f00
#define HIGH_FINAL_VAL_DEFAULT 0x300
#define SCP_CLK_L1_SRAM_PD REG32(SCP_CLK_BASE + 0x80)
#define SCP_CLK_TCM_TAIL_SRAM_PD REG32(SCP_CLK_BASE + 0x94)

#ifdef CHIP_VARIANT_MT8186
#define SCP_CLK_CTRL_GENERAL_CTRL REG32(SCP_CLK_BASE + 0x9C)
#endif

#define SCP_CLK_SLEEP REG32(SCP_CLK_BASE + 0xA0)
#define SLOW_WAKE_DISABLE 1
#define SCP_FAST_WAKE_CNT_END REG32(SCP_CLK_BASE + 0xA4)
#define FAST_WAKE_CNT_END_MASK 0xfff
#define FAST_WAKE_CNT_END_DEFAULT 0x18
#define MEM_CK_CS_ISO_CNT_END_MASK 0x7f0000

/* Peripherals */
#define SCP_I2C0_BASE (SCP_CFG_BASE + 0x5000)
#define SCP_I2C1_BASE (SCP_CFG_BASE + 0x6000)
#define SCP_I2C2_BASE (SCP_CFG_BASE + 0x7000)

#define SCP_GPIO_BASE (SCP_CFG_BASE + 0x8000)
#define SCP_UART0_BASE (SCP_CFG_BASE + 0x9000)
#define SCP_UART1_BASE (SCP_CFG_BASE + 0xE000)
#define SCP_UART_COUNT 2

/* External GPIO interrupt */
#define SCP_EINT_BASE (SCP_CFG_BASE + 0xA000)
#define SCP_EINT_STATUS REG32_ADDR(SCP_EINT_BASE)
#define SCP_EINT_ACK REG32_ADDR(SCP_EINT_BASE + 0x040)
#define SCP_EINT_MASK_GET REG32_ADDR(SCP_EINT_BASE + 0x080)
#define SCP_EINT_MASK_SET REG32_ADDR(SCP_EINT_BASE + 0x0C0)
#define SCP_EINT_MASK_CLR REG32_ADDR(SCP_EINT_BASE + 0x100)
#define SCP_EINT_SENS_GET REG32_ADDR(SCP_EINT_BASE + 0x140)
#define SCP_EINT_SENS_SET REG32_ADDR(SCP_EINT_BASE + 0x180)
#define SCP_EINT_SENS_CLR REG32_ADDR(SCP_EINT_BASE + 0x1C0)
#define SCP_EINT_SOFT_GET REG32_ADDR(SCP_EINT_BASE + 0x200)
#define SCP_EINT_SOFT_SET REG32_ADDR(SCP_EINT_BASE + 0x240)
#define SCP_EINT_SOFT_CLR REG32_ADDR(SCP_EINT_BASE + 0x280)
#define SCP_EINT_POLARITY_GET REG32_ADDR(SCP_EINT_BASE + 0x300)
#define SCP_EINT_POLARITY_SET REG32_ADDR(SCP_EINT_BASE + 0x340)
#define SCP_EINT_POLARITY_CLR REG32_ADDR(SCP_EINT_BASE + 0x380)
#define SCP_EINT_D0_EN REG32_ADDR(SCP_EINT_BASE + 0x400)
#define SCP_EINT_D1_EN REG32_ADDR(SCP_EINT_BASE + 0x420)
#define SCP_EINT_DBNC_GET REG32_ADDR(SCP_EINT_BASE + 0x500)
#define SCP_EINT_DBNC_SET REG32_ADDR(SCP_EINT_BASE + 0x600)
#define SCP_EINT_DBNC_CLR REG32_ADDR(SCP_EINT_BASE + 0x700)

#define SCP_PMICWP2P_BASE (SCP_CFG_BASE + 0xB000)
#define PMICW_WACS_CMD REG32(SCP_PMICWP2P_BASE + 0x200)
#define PMICW_WACS_RDATA REG32(SCP_PMICWP2P_BASE + 0x204)
#define PMICW_WACS_VLDCLR REG32(SCP_PMICWP2P_BASE + 0x208)
#define SCP_SPMP2P_BASE (SCP_CFG_BASE + 0xC000)
#define SCP_DMA_BASE (SCP_CFG_BASE + 0xD000)
#define DMA_ACKINT_CHX REG32(SCP_DMA_BASE + 0x20)
#define SCP_SPI0_BASE (SCP_CFG_BASE + 0xF000)
#define SCP_SPI1_BASE (SCP_CFG_BASE + 0x10000)
#define SCP_SPI2_BASE (SCP_CFG_BASE + 0x11000)

#define CACHE_ICACHE 0
#define CACHE_DCACHE 1
#define CACHE_COUNT 2
#define SCP_CACHE_BASE (SCP_CFG_BASE + 0x14000)
#define SCP_CACHE_SEL(x) (SCP_CACHE_BASE + (x) * 0x3000)
#define SCP_CACHE_CON(x) REG32(SCP_CACHE_SEL(x))
#define SCP_CACHE_CON_MCEN BIT(0)
#define SCP_CACHE_CON_CNTEN0 BIT(2)
#define SCP_CACHE_CON_CNTEN1 BIT(3)
#define SCP_CACHE_CON_CACHESIZE_SHIFT 8
#define SCP_CACHE_CON_CACHESIZE_MASK (0x3 << SCP_CACHE_CON_CACHESIZE_SHIFT)
#define SCP_CACHE_CON_CACHESIZE_0KB (0x0 << SCP_CACHE_CON_CACHESIZE_SHIFT)
#define SCP_CACHE_CON_CACHESIZE_8KB (0x1 << SCP_CACHE_CON_CACHESIZE_SHIFT)
#define SCP_CACHE_CON_CACHESIZE_16KB (0x2 << SCP_CACHE_CON_CACHESIZE_SHIFT)
#define SCP_CACHE_CON_CACHESIZE_32KB (0x3 << SCP_CACHE_CON_CACHESIZE_SHIFT)
#define SCP_CACHE_CON_WAYEN BIT(10)

#define SCP_CACHE_OP(x) REG32(SCP_CACHE_SEL(x) + 0x04)
#define SCP_CACHE_OP_EN BIT(0)
#define SCP_CACHE_OP_OP_SHIFT 1
#define SCP_CACHE_OP_OP_MASK (0xf << SCP_CACHE_OP_OP_SHIFT)

#define OP_INVALIDATE_ALL_LINES (0x1 << SCP_CACHE_OP_OP_SHIFT)
#define OP_INVALIDATE_ONE_LINE_BY_ADDRESS (0x2 << SCP_CACHE_OP_OP_SHIFT)
#define OP_INVALIDATE_ONE_LINE_BY_SET_WAY (0x4 << SCP_CACHE_OP_OP_SHIFT)
#define OP_CACHE_FLUSH_ALL_LINES (0x9 << SCP_CACHE_OP_OP_SHIFT)
#define OP_CACHE_FLUSH_ONE_LINE_BY_ADDRESS (0xa << SCP_CACHE_OP_OP_SHIFT)
#define OP_CACHE_FLUSH_ONE_LINE_BY_SET_WAY (0xc << SCP_CACHE_OP_OP_SHIFT)

#define SCP_CACHE_OP_TADDR_SHIFT 5
#define SCP_CACHE_OP_TADDR_MASK (0x7ffffff << SCP_CACHE_OP_TADDR_SHIFT)
#define SCP_CACHE_LINE_SIZE BIT(SCP_CACHE_OP_TADDR_SHIFT)

/* Cache statistics */
#define SCP_CACHE_HCNT0L(x) REG32(SCP_CACHE_SEL(x) + 0x08)
#define SCP_CACHE_HCNT0U(x) REG32(SCP_CACHE_SEL(x) + 0x0c)
#define SCP_CACHE_CCNT0L(x) REG32(SCP_CACHE_SEL(x) + 0x10)
#define SCP_CACHE_CCNT0U(x) REG32(SCP_CACHE_SEL(x) + 0x14)
#define SCP_CACHE_HCNT1L(x) REG32(SCP_CACHE_SEL(x) + 0x18)
#define SCP_CACHE_HCNT1U(x) REG32(SCP_CACHE_SEL(x) + 0x1c)
#define SCP_CACHE_CCNT1L(x) REG32(SCP_CACHE_SEL(x) + 0x20)
#define SCP_CACHE_CCNT1U(x) REG32(SCP_CACHE_SEL(x) + 0x24)

#define SCP_CACHE_REGION_EN(x) REG32(SCP_CACHE_SEL(x) + 0x2c)

#define SCP_CACHE_ENTRY_BASE(x) (SCP_CACHE_SEL(x) + 0x2000)
#define SCP_CACHE_ENTRY(x, reg) REG32(SCP_CACHE_ENTRY_BASE(x) + (reg) * 4)
#define SCP_CACHE_END_ENTRY_BASE(x) (SCP_CACHE_SEL(x) + 0x2040)
#define SCP_CACHE_END_ENTRY(x, reg) \
	REG32(SCP_CACHE_END_ENTRY_BASE(x) + (reg) * 4)
#define SCP_CACHE_ENTRY_C BIT(8)
#define SCP_CACHE_ENTRY_BASEADDR_MASK (0xfffff << 12)

/* ARMV7 regs */
#define ARM_SCB_SCR REG32(0xE000ED10)
#define SCR_DEEPSLEEP BIT(2)

/* AP regs */
#define AP_BASE 0xA0000000
#define TOPCK_BASE AP_BASE /* Top clock */
#define SCP_UART2_BASE (AP_BASE + 0x01002000) /* AP UART0 */

/* CLK_CFG_5 regs */
#define AP_CLK_CFG_5 REG32(TOPCK_BASE + 0x0090)
#define PWRAP_ULPOSC_MASK (0x3000000)
#define CLK26M (0 << 24)
#define OSC_D16 (1 << 24)
#define OSC_D4 (2 << 24)
#define OSC_D8 (3 << 24)
#define AP_CLK_CFG_5_CLR REG32(TOPCK_BASE + 0x0098)
#define PWRAP_ULPOSC_CG BIT(31)

#ifdef CHIP_VARIANT_MT8186
/* SCP PLL MUX RG */
#define CLK_CFG_UPDATE (TOPCK_BASE + 0x0004)
#define SCP_CK_UPDATE_SHFT 1
#define CLK_CFG_0 (TOPCK_BASE + 0x0040)
#define CLK_CFG_0_SET (TOPCK_BASE + 0x0044)
#define CLK_CFG_0_CLR (TOPCK_BASE + 0x0048)
#define CLK_SCP_SEL_MSK 0x7
#define CLK_SCP_SEL_SHFT 8
#endif

/* OSC meter */
#ifdef CHIP_VARIANT_MT8186
#define AP_CLK_MISC_CFG_0 REG32(TOPCK_BASE + 0x0140)
#define AP_CLK_DBG_CFG REG32(TOPCK_BASE + 0x017C)
#else
#define AP_CLK_MISC_CFG_0 REG32(TOPCK_BASE + 0x0104)
#define AP_CLK_DBG_CFG REG32(TOPCK_BASE + 0x010C)
#endif

#define MISC_METER_DIVISOR_MASK 0xff000000
#define MISC_METER_DIV_1 0
#define DBG_MODE_MASK 3
#define DBG_MODE_SET_CLOCK 0
#define DBG_BIST_SOURCE_MASK (0x3f << 16)

#ifdef CHIP_VARIANT_MT8186
#define DBG_BIST_SOURCE_ULPOSC1 (35 << 16)
#define DBG_BIST_SOURCE_ULPOSC2 (34 << 16)
#else
#define DBG_BIST_SOURCE_ULPOSC1 (0x26 << 16)
#define DBG_BIST_SOURCE_ULPOSC2 (0x25 << 16)
#endif

#define AP_SCP_CFG_0 REG32(TOPCK_BASE + 0x0220)
#define CFG_FREQ_METER_RUN (1 << 4)
#define CFG_FREQ_METER_ENABLE (1 << 12)
#define AP_SCP_CFG_1 REG32(TOPCK_BASE + 0x0224)
#define CFG_FREQ_COUNTER(CFG1) ((CFG1) & 0xFFFF)

/* GPIO */
#define AP_GPIO_BASE (AP_BASE + 0x00005000)
/*
 * AP_GPIO_DIR
 * GPIO input/out direction, 1 bit per pin.
 * 0:input 1:output
 */
#define AP_GPIO_DIR(n) REG32(AP_GPIO_BASE + ((n) << 4))
/*
 * AP_GPIO_DOUT, n in [0..5]
 * GPIO output level, 1 bit per pin
 * 0:low 1:high
 */
#define AP_GPIO_DOUT(n) REG32(AP_GPIO_BASE + 0x100 + ((n) << 4))
/*
 * AP_GPIO_DIN, n in [0..5]
 * GPIO input level, 1 bit per pin
 * 0:low 1:high
 */
#define AP_GPIO_DIN(n) REG32(AP_GPIO_BASE + 0x200 + ((n) << 4))
/*
 * AP_GPIO_MODE, n in [0..22]
 * Pin mode selection, 4 bit per pin
 * bit3   - write enable, set to 1 for hw to fetch bit2,1,0.
 * bit2-0 - mode 0 ~ 7
 */
#define AP_GPIO_MODE(n) REG32(AP_GPIO_BASE + 0x300 + ((n) << 4))
#define AP_GPIO_TRAP REG32(AP_GPIO_BASE + 0x6B0)
#define AP_GPIO_UNIMPLEMENTED REG32(AP_GPIO_BASE + 0x6C0)
#define AP_GPIO_DBG REG32(AP_GPIO_BASE + 0x6D0)
#define AP_GPIO_BANK REG32(AP_GPIO_BASE + 0x6E0)
/* AP_GPIO_SEC, n in [0..5] */
#define AP_GPIO_SEC(n) REG32(AP_GPIO_BASE + 0xF00 + ((n) << 4))

#ifdef CHIP_VARIANT_MT8186
#define AP_PLL_CON0 REG32(AP_BASE + 0xC000)
#define LTECLKSQ_EN BIT(0)
#define LTECLKSQ_LPF_EN BIT(1)
#define LTECLKSQ_HYS_EN BIT(2)
#define LTECLKSQ_VOD_EN BIT(3)
#define LTECLKSQ_HYS_SEL (0x1 << 4)
#define CLKSQ_RESERVE (0x1 << 10)
#define SSUSB26M_CK2_EN BIT(13)
#define SSUSB26M_CK_EN BIT(14)
#define XTAL26M_CK_EN BIT(15)
#define ULPOSC_CTRL_SEL (0xf << 16)
#endif

/*
 * PLL ULPOSC
 * ULPOSC1:  AP_ULPOSC_CON[0] AP_ULPOSC_CON[1]
 * ULPOSC2:  AP_ULPOSC_CON[2] AP_ULPOSC_CON[3]
 * osc: 0 for ULPOSC1, 1 for ULPSOC2.
 */
#ifdef CHIP_VARIANT_MT8186
#define AP_ULPOSC_BASE0 (AP_BASE + 0xC500)
#define AP_ULPOSC_BASE1 (AP_BASE + 0xC504)
#define AP_ULPOSC_CON02(osc) REG32(AP_ULPOSC_BASE0 + (osc) * 0x80)
#define AP_ULPOSC_CON13(osc) REG32(AP_ULPOSC_BASE1 + (osc) * 0x80)
#else
#define AP_ULPOSC_BASE0 (AP_BASE + 0xC700)
#define AP_ULPOSC_BASE1 (AP_BASE + 0xC704)
#define AP_ULPOSC_CON02(osc) REG32(AP_ULPOSC_BASE0 + (osc) * 0x8)
#define AP_ULPOSC_CON13(osc) REG32(AP_ULPOSC_BASE1 + (osc) * 0x8)
#endif
/*
 * AP_ULPOSC_CON[0,2]
 * bit0-5:   calibration
 * bit6-12:  I-band
 * bit13-16: F-band
 * bit17-22: div
 * bit23:    CP_EN
 * bit24-31: reserved
 */
#ifdef CHIP_VARIANT_MT8186
#define OSC_CALI_MASK 0x3f
#define OSC_IBAND_SHIFT 6
#define OSC_FBAND_MASK 0xf
#define OSC_FBAND_SHIFT 13
#define OSC_DIV_SHIFT 17
#else
#define OSC_CALI_MSK (0x3f << 0)
#define OSC_CALI_BITS 6
#define OSC_IBAND_MASK (0x7f << 6)
#define OSC_FBAND_MASK (0x0f << 13)
#define OSC_DIV_MASK (0x1f << 17)
#define OSC_DIV_BITS 5
#define OSC_RESERVED_MASK (0xff << 24)
#endif

#define OSC_CP_EN BIT(23)
/* AP_ULPOSC_CON[1,3] */
#define OSC_MOD_MASK (0x03 << 0)
#define OSC_DIV2_EN BIT(2)

#define UNIMPLEMENTED_GPIO_BANK 0

#ifndef __ASSEMBLER__

/*
 * Cortex-M4 mod
 * Available power saving features:
 * 1. FPU freeze - freeze FPU operand when FPU is not used
 * 2. LSU gating - gate LSU clock when not LSU operation
 * 3. Trace clk disable - gate trace clock
 * 4. DCM for CPU stall - gate CPU clock when CPU stall
 */
#define CM4_MODIFICATION REG32(0xE00FE000)
#define CM4_DCM_FEATURE REG32(0xE00FE004)
/* UART, 16550 compatible */
#define SCP_UART_BASE(n) CONCAT3(SCP_UART, n, _BASE)
#define UART_REG(n, offset) REG32_ADDR(SCP_UART_BASE(n))[offset]
#define UART_IRQ(n) CONCAT2(SCP_IRQ_UART, n)
#define UART_RX_IRQ(n) CONCAT3(SCP_IRQ_UART, n, _RX)

/* Watchdog */
#define SCP_WDT_BASE (SCP_CFG_BASE + 0x84)
#define SCP_WDT_REG(offset) REG32(SCP_WDT_BASE + offset)
#define SCP_WDT_CFG SCP_WDT_REG(0)
#define SCP_WDT_FREQ 33825
#define SCP_WDT_MAX_PERIOD 0xFFFFF /* 31 seconds */
#define SCP_WDT_PERIOD(ms) (SCP_WDT_FREQ * (ms) / 1000)
#define SCP_WDT_ENABLE BIT(31)
#define SCP_WDT_RELOAD SCP_WDT_REG(4)
#define SCP_WDT_RELOAD_VALUE 1

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_REGISTERS_H */
