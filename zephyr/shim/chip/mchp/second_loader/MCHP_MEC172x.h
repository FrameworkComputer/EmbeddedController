/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef MCHP_MEC172X_H
#define MCHP_MEC172X_H

#ifdef __cplusplus
extern "C" {
#endif

/* Number of Bits used for Priority Levels */
#define __NVIC_PRIO_BITS 4

/* Interrupt Number Definition */
typedef enum {
	/* Cortex-M4 Processor Exceptions Numbers */
	/* 1  Reset Vector, invoked on Power up and warm reset */
	Reset_IRQn = -15,
	/* 2  Non maskable Interrupt, cannot be stopped or preempted */
	NonMaskableInt_IRQn = -14,
	/* 3  Hard Fault, all classes of Fault */
	HardFault_IRQn = -13,
	/*
	 * 4  Memory Management, MPU mismatch, including Access Violation
	 * and No Match.
	 */
	MemoryManagement_IRQn = -12,
	/*
	 * 5 Bus Fault, Pre-Fetch-, Memory Access Fault, other
	 * address/memory related Fault.
	 */
	BusFault_IRQn = -11,
	/* 6  Usage Fault, i.e. Undef Instruction, Illegal State Transition */
	UsageFault_IRQn = -10,
	/* 11  System Service Call via SVC instruction */
	SVCall_IRQn = -5,
	/* 12  Debug Monitor */
	DebugMonitor_IRQn = -4,
	/* 14  Pendable request for system service */
	PendSV_IRQn = -2,
	/* 15  System Tick Timer */
	SysTick_IRQn = -1,
	/* MCHP_MEC1503 Specific Interrupt Numbers */
	/* 0  GPIO[140:176], GIRQ08 */
	GPIO_140_175_IRQn = 0,
	/* 1  GPIO[100:137], GIRQ09 */
	GPIO_100_137_IRQn = 1,
	/* 2  GPIO[040:076], GIRQ10 */
	GPIO_040_076_IRQn = 2,
	/* 3  GPIO[000:036], GIRQ11 */
	GPIO_000_036_IRQn = 3,
	/* 4  GPIO[200:236], GIRQ12 */
	GPIO_200_236_IRQn = 4,
	/*
	 * 14  SWI_INT_0 -- GIRQ 23.11,
	 * SWI_INT_1 -- GIRQ 23.12,
	 * SWI_INT_2 -- GIRQ 23.13,
	 * SWI_INT_3 -- GIRQ 23.14
	 */
	SWI_INT_0123_IRQn = 14,
	/* 15  MSVW[00:06]_SRC[0:3], GIRQ 24 */
	MSVW00_06_IRQn = 15,
	/* 16  MSVW[07:10]_SRC[0:3], GIRQ 25 */
	MSVW07_10_IRQn = 16,
	/* 17  GPIO[240:276], GIRQ26 */
	GPIO_240_276_IRQn = 17,
	/* 20  SMB0, GIRQ 13.0 */
	SMB_0_IRQn = 20,
	/* 21  SMB1, GIRQ 13.1 */
	SMB_1_IRQn = 21,
	/* 22  SMB2, GIRQ 13.2 */
	SMB_2_IRQn = 22,
	/* 23  SMB3, GIRQ 13.3 */
	SMB_3_IRQn = 23,
	/* 24  DMA0, GIRQ14.0 */
	DMA0_IRQn = 24,
	/* 25  DMA1 */
	DMA1_IRQn = 25,
	/* 26  DMA2 */
	DMA2_IRQn = 26,
	/* 27  DMA3 */
	DMA3_IRQn = 27,
	/* 28  DMA4 */
	DMA4_IRQn = 28,
	/* 29  DMA5 */
	DMA5_IRQn = 29,
	/* 30  DMA6 */
	DMA6_IRQn = 30,
	/* 31  DMA7 */
	DMA7_IRQn = 31,
	/* 32  DMA8 */
	DMA8_IRQn = 32,
	/* 33  DMA9 */
	DMA9_IRQn = 33,
	/* 34  DMA10 */
	DMA10_IRQn = 34,
	/* 35  DMA11 */
	DMA11_IRQn = 35,
	/* 40  UART 0, GIRQ 15.0 */
	UART_0_IRQn = 40,
	/* 41  UART 1, GIRQ 15.1 */
	UART_1_IRQn = 41,
	/* 42  EMI_0, GIRQ 15.2 */
	EMI_0_IRQn = 42,
	/* 43  EMI_1, GIRQ 15.3 */
	EMI_1_IRQn = 43,
	/* 45  ACPIEC[0] IBF, GIRQ 15.5 */
	ACPIEC0_IBF_IRQn = 45,
	/* 46  ACPIEC[0] OBF, GIRQ 15.6 */
	ACPIEC0_OBF_IRQn = 46,
	/* 47  ACPIEC[1] IBF, GIRQ 15.7 */
	ACPIEC1_IBF_IRQn = 47,
	/* 48  ACPIEC[1] OBF, GIRQ 15.8 */
	ACPIEC1_OBF_IRQn = 48,
	/* 49  ACPIEC[2] IBF, GIRQ 15.9 */
	ACPIEC2_IBF_IRQn = 49,
	/* 50  ACPIEC[2] OBF, GIRQ 15.10 */
	ACPIEC2_OBF_IRQn = 50,
	/* 51  ACPIEC[3] IBF, GIRQ 15.11 */
	ACPIEC3_IBF_IRQn = 51,
	/* 52  ACPIEC[3] OBF, GIRQ 15.12 */
	ACPIEC3_OBF_IRQn = 52,
	/* 55  ACPIPM1_CTL, GIRQ 15.15 */
	ACPIPM1_CTL_IRQn = 55,
	/* 56  ACPIPM1_EN, GIRQ 15.16 */
	ACPIPM1_EN_IRQn = 56,
	/* 57  ACPIPM1_STS, GIRQ 15.17 */
	ACPIPM1_STS_IRQn = 57,
	/* 58  8042EM OBE, GIRQ 15.18 */
	KBC8042_OBF_IRQn = 58,
	/* 59  8042EM IBF, GIRQ 15.19 */
	KBC8042_IBF_IRQn = 59,
	/* 60  MAILBOX, GIRQ 15.20 */
	MAILBOX_IRQn = 60,
	/* 62  PORT80_DEBUG_0, GIRQ 15.22 */
	PORT80_DEBUG_0_IRQn = 62,
	/* 63  PORT80_DEBUG_1, GIRQ 15.23 */
	PORT80_DEBUG_1_IRQn = 63,
	/* 64  ASIF_INT, GIRQ 15.24 */
	ASIF_INT_IRQn = 64,
	/* 70  PECIHOST, GIRQ 17.0 */
	PECIHOST_IRQn = 70,
	/* 71  TACH_0, GIRQ 17.1 */
	TACH_0_IRQn = 71,
	/* 72  TACH_1, GIRQ 17.2 */
	TACH_1_IRQn = 72,
	/* 73  TACH_2, GIRQ 17.3 */
	TACH_2_IRQn = 73,
	/* 78  ADC_SNGL, GIRQ 17.8 */
	ADC_SNGL_IRQn = 78,
	/* 79  ADC_RPT, GIRQ 17.9 */
	ADC_RPT_IRQn = 79,
	/* 83  Breathing LED 0, GIRQ 17.13 */
	LED_0_IRQn = 83,
	/* 84  Breathing LED 1, GIRQ 17.14 */
	LED_1_IRQn = 84,
	/* 85  Breathing LED 2, GIRQ 17.15 */
	LED_2_IRQn = 85,
	/* 91  QMSPI, GIRQ 18.1 */
	QMSPI_INT_IRQn = 91,
	/* 100  PS2 Controller 0 Activity, GIRQ 18.10 */
	PS2_0_ACT_IRQn = 100,
	/* 101  PS2 Controller 1 Activity, GIRQ 18.11 */
	PS2_1_ACT_IRQn = 101,
	/* 103  PC, GIRQ 19.0 */
	INTR_PC_IRQn = 103,
	/* 104  BM1, GIRQ 19.1 */
	INTR_BM1_IRQn = 104,
	/* 105  BM2, GIRQ 19.2 */
	INTR_BM2_IRQn = 105,
	/* 106  LTR, GIRQ 19.3 */
	INTR_LTR_IRQn = 106,
	/* 107  OOB_UP, GIRQ 19.4 */
	INTR_OOB_UP_IRQn = 107,
	/* 108  OOB_DOWN, GIRQ 19.5 */
	INTR_OOB_DOWN_IRQn = 108,
	/* 109  FLASH, GIRQ 19.6 */
	INTR_FLASH_IRQn = 109,
	/* 110  ESPI_RESET, GIRQ 19.7 */
	ESPI_RESET_IRQn = 110,
	/* 111  RTOS_TIMER, GIRQ 23.10 */
	RTOS_TIMER_IRQn = 111,
	/* 112  HTIMER0, GIRQ 23.16 */
	HTIMER0_IRQn = 112,
	/* 113  HTIMER1, GIRQ 23.17 */
	HTIMER1_IRQn = 113,
	/* 114  WEEK_ALARM_INT, GIRQ 21.3 */
	WEEK_ALARM_INT_IRQn = 114,
	/* 115  SUB_WEEK_ALARM_INT, GIRQ 21.4 */
	SUB_WEEK_ALARM_IRQn = 115,
	/* 116  ONE_SECOND, GIRQ 21.5 */
	ONE_SECOND_IRQn = 116,
	/* 117  SUB_SECOND, GIRQ 21.6 */
	SUB_SECOND_IRQn = 117,
	/* 118  SYSPWR_PRES, GIRQ 21.7 */
	SYSPWR_PRES_IRQn = 118,
	/* 119  RTC, GIRQ 21.8 */
	RTC_INT_IRQn = 119,
	/* 120  RTC ALARM, GIRQ 21.9 */
	RTC_ALARM_IRQn = 120,
	/* 121  VCI_OVRD_IN, GIRQ 21.10 */
	VCI_OVRD_IN_IRQn = 121,
	/* 122  VCI_IN0, GIRQ 21.11 */
	VCI_IN0_IRQn = 122,
	/* 123  VCI_IN1, GIRQ 21.12 */
	VCI_IN1_IRQn = 123,
	/* 124  VCI_IN2, GIRQ 21.13 */
	VCI_IN2_IRQn = 124,
	/* 125  VCI_IN3, GIRQ 21.14 */
	VCI_IN3_IRQn = 125,
	/* 129  PS2 Controller 0 Port A Wake, GIRQ 21.18 */
	PS2_0A_WK_IRQn = 129,
	/* 130  PS2 Controller 0 Port B Wake, GIRQ 21.19 */
	PS2_0B_WK_IRQn = 130,
	/* 132  PS2 Controller 1 Port B Wake, GIRQ 21.21 */
	PS2_1B_WK_IRQn = 132,
	/* 135  KSC, GIRQ 21.25 */
	KSC_INT_IRQn = 135,
	/* 136  TIMER16_0, GIRQ 23.0 */
	TIMER16_0_IRQn = 136,
	/* 137  TIMER_16_1, GIRQ 23.1 */
	TIMER_1_IRQn = 137,
	/* 140  TIMER32_0, GIRQ 23.4 */
	TIMER32_0_IRQn = 140,
	/* 141  TIMER32_1, GIRQ 23.5 */
	TIMER32_1_IRQn = 141,
	/* 146  CAPTURE_TIMER, GIRQ 18.20 */
	CAPTURE_TIMER_IRQn = 146,
	/* 147  CAPTURE_0, GIRQ 18.21 */
	CAPTURE_0_IRQn = 147,
	/* 148  CAPTURE_1, GIRQ 18.22 */
	CAPTURE_1_IRQn = 148,
	/* 149  CAPTURE_2, GIRQ 18.23 */
	CAPTURE_2_IRQn = 149,
	/* 150  CAPTURE_3, GIRQ 18.24 */
	CAPTURE_3_IRQn = 150,
	/* 151  CAPTURE_4, GIRQ 18.25 */
	CAPTURE_4_IRQn = 151,
	/* 152  CAPTURE_5, GIRQ 18.26 */
	CAPTURE_5_IRQn = 152,
	/* 153  COMPARE_0, GIRQ 18.27 */
	COMPARE_0_IRQn = 153,
	/* 154  COMPARE_1, GIRQ 18.28 */
	COMPARE_1_IRQn = 154,
	/* 155  EEPROM, GIRQ 18.13 */
	EEPROM_INT_IRQn = 155,
	/* 156  VWIRE_ENABLE, GIRQ 19.8 */
	VWIRE_ENABLE_IRQn = 156,
	/* 158  SMB4, GIRQ 13.4 */
	SMB_4_IRQn = 158,
	/* 159  TACH_3, GIRQ 17.4 */
	TACH_3_IRQn = 159,
	/* 166  EC_CMPLTN, GIRQ 19.9 */
	EC_CMPLTN_IRQn = 166,
	/* 167  ESPI_ERROR, GIRQ 19.10 */
	ESPI_ERROR_IRQn = 167
} IRQn_Type;

/* Cortex-M4 processor and core peripherals  */
#include "core_cm4.h"

/* Device Specific Peripheral Section  */
/* PCR_INST */
/*
 * The Power, Clocks, and Resets (PCR) Section identifies all the power
 * supplies, clock sources, and reset inputs to the chip and defines all the
 * derived power, clock, and reset signals.  (PCR_INST)
 */

struct PCR_INST_Type {
	/* PCR_INST Structure */
	union {
		__IO uint32_t SYS_SLP_CNTRL;

		struct {
			__IO uint32_t SLEEP_MODE : 1;
			__IO uint32_t RESV : 1;
			__IO uint32_t TEST : 1;
			__IO uint32_t SLEEP_ALL : 1;
		} SYS_SLP_CNTRL_b;
	};

	union {
		__IO uint32_t PROC_CLK_CNTRL;

		struct {
			__IO uint32_t PROCESSOR_CLOCK_DIVIDE : 8;
		} PROC_CLK_CNTRL_b;
	};

	union {
		/* Configures the EC_CLK clock domain (+0x08) */
		__IO uint32_t SLOW_CLK_CNTRL;

		struct {
			__IO uint32_t SLOW_CLOCK_DIVIDE : 10;
		} SLOW_CLK_CNTRL_b;
	};

	union {
		/* Oscillator ID Register (+0x0C) */
		__IO uint32_t OSC_ID;

		struct {
			__IO uint32_t TEST : 8;
			__IO uint32_t PLL_LOCK : 1;
		} OSC_ID_b;
	};

	union {
		/* PCR Power Reset Status Register (+0x10) */
		__IO uint32_t PCR_PWR_RST_STS;

		struct {
			__IO uint32_t RESV_1 : 2;

			__I uint32_t VCC_PWRGD_STATUS : 1;
			__I uint32_t RESET_HOST_STATUS : 1;
			__IO uint32_t RESET_VTR_STATUS : 1;
			__IO uint32_t VBAT_RESET_STATUS : 1;
			__IO uint32_t RESET_SYS_STATUS : 1;

			__I uint32_t JTAG_RESET_STATUS : 1;
			__IO uint32_t WDT_EVENT : 1;
			__IO uint32_t RESV_2 : 1;

			__I uint32_t _32K_ACTIVE : 1;
			__I uint32_t PCICLK_ACTIVE : 1;
			__I uint32_t ESPI_CLK_ACTIVE : 1;
		} PCR_PWR_RST_STS_b;
	};

	union {
		/* Power Reset Control Register (+0x14) */
		__IO uint32_t PWR_RST_CNTRL;

		struct {
			__IO uint32_t PWR_INV : 1;
			__IO uint32_t RESV : 7;
			__IO uint32_t HOST_RESET_SELECT : 1;
		} PWR_RST_CNTRL_b;
	};

	union {
		/* System Reset Register (+0x18) */
		__IO uint32_t SYS_RST;

		struct {
			__IO uint32_t RESV : 8;
			__IO uint32_t SOFT_SYS_RESET : 1;
		} SYS_RST_b;
	};

	union {
		/* TURBO clock control Register (+0x1C) */
		__IO uint32_t TURBO_CLK;

		struct {
			__IO uint32_t RESV : 2;
			__IO uint32_t FAST_MODE_ENABLE : 1;
		} TURBO_CLK_b;
	};

	/* (+0x20, 0x24, 0x28, 0x2C) */
	__I uint32_t RESERVED[4];

	union {
		/* Sleep Enable 0 Register (+0x30) */
		__IO uint32_t SLP_EN_0;

		struct {
			__IO uint32_t JTAG_STAP_SLP_EN : 1;
			__IO uint32_t EFUSE_SLP_EN : 1;
		} SLP_EN_0_b;
	};

	union {
		/* Sleep Enable 1 Register (+0x34) */
		__IO uint32_t SLP_EN_1;

		struct {
			__IO uint32_t INT_SLP_EN : 1;
			__IO uint32_t PECI_SLP_EN : 1;
			__IO uint32_t TACH0_SLP_EN : 1;
			__IO uint32_t RESV_1 : 1;
			__IO uint32_t PWM0_SLP_EN : 1;
			__IO uint32_t PMC_SLP_EN : 1;
			__IO uint32_t DMA_SLP_EN : 1;
			__IO uint32_t TFDP_SLP_EN : 1;
			__IO uint32_t PROCESSOR_SLP_EN : 1;
			__IO uint32_t WDT_SLP_EN : 1;
			__IO uint32_t SMB0_SLP_EN : 1;
			__IO uint32_t TACH1_SLP_EN : 1;
			__IO uint32_t TACH2_SLP_EN : 1;
			__IO uint32_t TACH3_SLP_EN : 1;
			__IO uint32_t RESV_2 : 6;
			__IO uint32_t PWM1_SLP_EN : 1;
			__IO uint32_t PWM2_SLP_EN : 1;
			__IO uint32_t PWM3_SLP_EN : 1;
			__IO uint32_t PWM4_SLP_EN : 1;
			__IO uint32_t PWM5_SLP_EN : 1;
			__IO uint32_t PWM6_SLP_EN : 1;
			__IO uint32_t PWM7_SLP_EN : 1;
			__IO uint32_t PWM8_SLP_EN : 1;
			__IO uint32_t RESV_3 : 1;
			__IO uint32_t EC_REG_BANK_SLP_EN : 1;
			__IO uint32_t TIMER16_0_SLP_EN : 1;
			__IO uint32_t TIMER16_1_SLP_EN : 1;
		} SLP_EN_1_b;
	};

	union {
		/* Sleep Enable 2 Register (+0x38) */
		__IO uint32_t SLP_EN_2;

		struct {
			__IO uint32_t IMAP_SLP_EN : 1;
			__IO uint32_t UART_0_SLP_EN : 1;
			__IO uint32_t UART_1_SLP_EN : 1;
			__IO uint32_t RESV_1 : 5;
			__IO uint32_t INTRUDER_SLP_EN : 1;
			__IO uint32_t RESV_2 : 3;
			__IO uint32_t GLBL_CFG_SLP_EN : 1;
			__IO uint32_t ACPI_EC_0_SLP_EN : 1;
			__IO uint32_t ACPI_EC_1_SLP_EN : 1;
			__IO uint32_t ACPI_PM1_SLP_EN : 1;
			__IO uint32_t KBCEM_SLP_EN : 1;
			__IO uint32_t MBX_SLP_EN : 1;
			__IO uint32_t RTC_SLP_EN : 1;
			__IO uint32_t ESPI_SLP_EN : 1;
			__IO uint32_t SCRATCH_16_SLP_EN : 1;
			__IO uint32_t ACPI_EC_2_SLP_EN : 1;
			__IO uint32_t ACPI_EC_3_SLP_EN : 1;
			__IO uint32_t ACPI_EC_4_SLP_EN : 1;
			__IO uint32_t ASIF_SLP_EN : 1;
			__IO uint32_t PORT80_0_SLP_EN : 1;
			__IO uint32_t PORT80_1_SLP_EN : 1;
			__IO uint32_t SAF_BRDG_SLP_EN : 1;
			__IO uint32_t UART_2_SLP_EN : 1;
			__IO uint32_t GLUE_SLP_EN : 1;
		} SLP_EN_2_b;
	};

	union {
		/* Sleep Enable 3 Register (+0x3C) */
		__IO uint32_t SLP_EN_3;

		struct {
			__IO uint32_t RESV_1 : 1;
			__IO uint32_t HDMICEC_SLP_EN : 1;
			__IO uint32_t RESV_2 : 1;
			__IO uint32_t ADC_SLP_EN : 1;
			__IO uint32_t RESV_3 : 1;
			__IO uint32_t PS2_0_SLP_EN : 1;
			__IO uint32_t PS2_1_SLP_EN : 1;
			__IO uint32_t RESV_4 : 3;
			__IO uint32_t HTIMER_0_SLP_EN : 1;
			__IO uint32_t KEYSCAN_SLP_EN : 1;
			__IO uint32_t RESV_5 : 1;
			__IO uint32_t SMB1_SLP_EN : 1;
			__IO uint32_t SMB2_SLP_EN : 1;
			__IO uint32_t SMB3_SLP_EN : 1;
			__IO uint32_t LED0_SLP_EN : 1;
			__IO uint32_t LED1_SLP_EN : 1;
			__IO uint32_t LED2_SLP_EN : 1;
			__IO uint32_t RESV_6 : 1;
			__IO uint32_t SMB4_SLP_EN : 1;
			__IO uint32_t RESV_7 : 2;
			__IO uint32_t TIMER32_0_SLP_EN : 1;
			__IO uint32_t TIMER32_1_SLP_EN : 1;
			__IO uint32_t RESV_8 : 1;
			__IO uint32_t PKE_SLP_EN : 1;
			__IO uint32_t RNG_SLP_EN : 1;
			__IO uint32_t AES_HASH_SLP_EN : 1;
			__IO uint32_t HTIMER_1_SLP_EN : 1;
			__IO uint32_t CCTIMER_SLP_EN : 1;
		} SLP_EN_3_b;
	};

	union {
		/* Sleep Enable 4 Register (+0x40) */
		__IO uint32_t SLP_EN_4;

		struct {
			__IO uint32_t RESV_1 : 6;
			__IO uint32_t RTOS_SLP_EN : 1;
			__IO uint32_t RESV_2 : 1;
			__IO uint32_t QSPI_SLP_EN : 1;
			__IO uint32_t RESV_3 : 5;
			__IO uint32_t EEPROM_SLP_EN : 1;
		} SLP_EN_4_b;
	};
	/* (+0x44, 0x48, 0x4C) */
	__I uint32_t RESERVED1[3];

	union {
		/* Clock Required 0 Register (+0x50) */
		__IO uint32_t CLK_REQ_0;

		struct {
			__IO uint32_t JTAG_STAP_CLK_REQ : 1;
			__IO uint32_t EFUSE_CLK_REQ : 1;
		} CLK_REQ_0_b;
	};

	union {
		/* Clock Required 1 Register (+0x54) */
		__IO uint32_t CLK_REQ_1;

		struct {
			__IO uint32_t INT_CLK_REQ : 1;
			__IO uint32_t PECI_CLK_REQ : 1;
			__IO uint32_t TACH0_CLK_REQ : 1;
			__IO uint32_t RESV_1 : 1;
			__IO uint32_t PWM0_CLK_REQ : 1;
			__IO uint32_t PMC_CLK_REQ : 1;
			__IO uint32_t DMA_CLK_REQ : 1;
			__IO uint32_t TFDP_CLK_REQ : 1;
			__IO uint32_t PROCESSOR_CLK_REQ : 1;
			__IO uint32_t WDT_CLK_REQ : 1;
			__IO uint32_t SMB0_CLK_REQ : 1;
			__IO uint32_t TACH1_CLK_REQ : 1;
			__IO uint32_t TACH2_CLK_REQ : 1;
			__IO uint32_t TACH3_CLK_REQ : 1;
			__IO uint32_t RESV_2 : 6;
			__IO uint32_t PWM1_CLK_REQ : 1;
			__IO uint32_t PWM2_CLK_REQ : 1;
			__IO uint32_t PWM3_CLK_REQ : 1;
			__IO uint32_t PWM4_CLK_REQ : 1;
			__IO uint32_t PWM5_CLK_REQ : 1;
			__IO uint32_t PWM6_CLK_REQ : 1;
			__IO uint32_t PWM7_CLK_REQ : 1;
			__IO uint32_t PWM8_CLK_REQ : 1;
			__IO uint32_t RESV_3 : 1;
			__IO uint32_t EC_REG_BANK_CLK_REQ : 1;
			__IO uint32_t TIMER16_0_CLK_REQ : 1;
			__IO uint32_t TIMER16_1_CLK_REQ : 1;
		} CLK_REQ_1_b;
	};

	union {
		/* Clock Required 2 Register (+0x58) */
		__IO uint32_t CLK_REQ_2;

		struct {
			__IO uint32_t IMAP_CLK_REQ : 1;
			__IO uint32_t UART_0_CLK_REQ : 1;
			__IO uint32_t UART_1_CLK_REQ : 1;
			__IO uint32_t RESV_1 : 5;
			__IO uint32_t INTRUDER_CLK_REQ : 1;
			__IO uint32_t RESV_2 : 3;
			__IO uint32_t GLBL_CFG_CLK_REQ : 1;
			__IO uint32_t ACPI_EC_0_CLK_REQ : 1;
			__IO uint32_t ACPI_EC_1_CLK_REQ : 1;
			__IO uint32_t ACPI_PM1_CLK_REQ : 1;
			/* 8042EM Clock Required (8042EM_CLK_REQ) */
			__IO uint32_t KBCEM_CLK_REQ : 1;
			__IO uint32_t MBX_CLK_REQ : 1;
			__IO uint32_t RTC_CLK_REQ : 1;
			__IO uint32_t ESPI_CLK_REQ : 1;
			__IO uint32_t SCRATCH_16_CLK_REQ : 1;
			__IO uint32_t ACPI_EC_2_CLK_REQ : 1;
			__IO uint32_t ACPI_EC_3_CLK_REQ : 1;
			__IO uint32_t RESV_3 : 1;
			__IO uint32_t ASIF_CLK_REQ : 1;
			__IO uint32_t PORT80_0_CLK_REQ : 1;
			__IO uint32_t PORT80_1_CLK_REQ : 1;
			__IO uint32_t SAF_BRDG_CLK_REQ : 1;
			__IO uint32_t UART_2_CLK_REQ : 1;
			__IO uint32_t GLUE_CLK_REQ : 1;
		} CLK_REQ_2_b;
	};

	union {
		/* Clock Required 3 Register (+0x5C) */
		__IO uint32_t CLK_REQ_3;

		struct {
			__IO uint32_t RESV_1 : 1;
			__IO uint32_t HDMICEC_CLK_REQ : 1;
			__IO uint32_t RESV_2 : 1;
			__IO uint32_t ADC_CLK_REQ : 1;
			__IO uint32_t RESV_3 : 1;
			__IO uint32_t PS2_0_CLK_REQ : 1;
			__IO uint32_t PS2_1_CLK_REQ : 1;
			__IO uint32_t RESV_4 : 3;
			__IO uint32_t HTIMER_0_CLK_REQ : 1;
			__IO uint32_t KEYSCAN_CLK_REQ : 1;
			__IO uint32_t RESV_5 : 1;
			__IO uint32_t SMB1_CLK_REQ : 1;
			__IO uint32_t SMB2_CLK_REQ : 1;
			__IO uint32_t SMB3_CLK_REQ : 1;
			__IO uint32_t LED0_CLK_REQ : 1;
			__IO uint32_t LED1_CLK_REQ : 1;
			__IO uint32_t LED2_CLK_REQ : 1;
			__IO uint32_t RESV_6 : 1;
			__IO uint32_t SMB_4_CLK_REQ : 1;
			__IO uint32_t RESV_7 : 2;
			__IO uint32_t TIMER32_0_CLK_REQ : 1;
			__IO uint32_t TIMER32_1_CLK_REQ : 1;
			__IO uint32_t RESV_8 : 1;
			__IO uint32_t PKE_CLK_REQ : 1;
			__IO uint32_t RNG_CLK_REQ : 1;
			__IO uint32_t AES_HASH_CLK_REQ : 1;
			__IO uint32_t HTIMER_1_CLK_REQ : 1;
			__IO uint32_t CCTIMER_CLK_REQ : 1;
		} CLK_REQ_3_b;
	};

	union {
		/* Clock Required 4 Register (+0x60) */
		__IO uint32_t CLK_REQ_4;

		struct {
			__IO uint32_t RESV_1 : 6;
			__IO uint32_t RTOS_CLK_REQ : 1;
			__IO uint32_t RESV_2 : 1;
			__IO uint32_t QSPI_CLK_REQ : 1;
			__IO uint32_t RESV_3 : 5;
			__IO uint32_t EEPROM_CLK_REQ : 1;
		} CLK_REQ_4_b;
	};
	/* (+0x54, 0x58, 0x5C) */
	__I uint32_t RESERVED2[3];

	union {
		/* Reset Enable 0 Register (+0x70) */
		__IO uint32_t RST_EN_0;

		struct {
			__IO uint32_t JTAG_STAP_RST_EN : 1;
			__IO uint32_t EFUSE_RST_EN : 1;
		} RST_EN_0_b;
	};

	union {
		/* Reset Enable 1 Register (+0x74) */
		__IO uint32_t RST_EN_1;

		struct {
			__IO uint32_t INT_RST_EN : 1;
			__IO uint32_t PECI_RST_EN : 1;
			__IO uint32_t TACH0_RST_EN : 1;
			__IO uint32_t RESV_1 : 1;
			__IO uint32_t PWM0_RST_EN : 1;
			__IO uint32_t RESV_2 : 1;
			__IO uint32_t DMA_RST_EN : 1;
			__IO uint32_t TFDP_RST_EN : 1;
			__IO uint32_t RESV_3 : 1;
			__IO uint32_t WDT_RST_EN : 1;
			__IO uint32_t SMB0_RST_EN : 1;
			__IO uint32_t TACH1_RST_EN : 1;
			__IO uint32_t TACH2_RST_EN : 1;
			__IO uint32_t TACH3_RST_EN : 1;
			__IO uint32_t RESV_4 : 6;
			__IO uint32_t PWM1_RST_EN : 1;
			__IO uint32_t PWM2_RST_EN : 1;
			__IO uint32_t PWM3_RST_EN : 1;
			__IO uint32_t PWM4_RST_EN : 1;
			__IO uint32_t PWM5_RST_EN : 1;
			__IO uint32_t PWM6_RST_EN : 1;
			__IO uint32_t PWM7_RST_EN : 1;
			__IO uint32_t PWM8_RST_EN : 1;
			__IO uint32_t RESV_5 : 2;
			__IO uint32_t TIMER16_0_RST_EN : 1;
			__IO uint32_t TIMER16_1_RST_EN : 1;
		} RST_EN_1_b;
	};

	union {
		/* Reset Enable 2 Register (+0x78) */
		__IO uint32_t RST_EN_2;

		struct {
			__IO uint32_t IMAP_RST_EN : 1;
			__IO uint32_t UART_0_RST_EN : 1;
			__IO uint32_t UART_1_RST_EN : 1;
			__IO uint32_t RESV_1 : 5;
			__IO uint32_t INTRUDER_RST_EN : 1;
			__IO uint32_t RESV_2 : 3;
			__IO uint32_t GLBL_CFG_RST_EN : 1;
			__IO uint32_t ACPI_EC_0_RST_EN : 1;
			__IO uint32_t ACPI_EC_1_RST_EN : 1;
			__IO uint32_t ACPI_PM1_RST_EN : 1;
			/* 8042EM Reset Enable (8042EM_RST_EN) */
			__IO uint32_t KBCEM_RST_EN : 1;
			__IO uint32_t MBX_RST_EN : 1;
			__IO uint32_t RTC_RST_EN : 1;
			__IO uint32_t RESV_3 : 1;
			__IO uint32_t SCRATCH_16_RST_EN : 1;
			__IO uint32_t ACPI_EC_2_RST_EN : 1;
			__IO uint32_t ACPI_EC_3_RST_EN : 1;
			__IO uint32_t RESV_4 : 1;
			__IO uint32_t ASIF_RST_EN : 1;
			__IO uint32_t PORT80_0_RST_EN : 1;
			__IO uint32_t PORT80_1_RST_EN : 1;
			__IO uint32_t RESV_5 : 1;
			__IO uint32_t UART_2_RST_EN : 1;
			__IO uint32_t GLUE_RST_EN : 1;
		} RST_EN_2_b;
	};

	union {
		/* Reset Enable 3 Register (+0x7C) */
		__IO uint32_t RST_EN_3;

		struct {
			__IO uint32_t RESV_1 : 1;
			__IO uint32_t HDMICEC_RST_EN : 1;
			__IO uint32_t RESV_2 : 1;
			__IO uint32_t ADC_RST_EN : 1;
			__IO uint32_t RESV_3 : 1;
			__IO uint32_t PS2_0_RST_EN : 1;
			__IO uint32_t PS2_1_RST_EN : 1;
			__IO uint32_t RESV_4 : 3;
			__IO uint32_t HTIMER_0_RST_EN : 1;
			__IO uint32_t KEYSCAN_RST_EN : 1;
			__IO uint32_t RESV_5 : 1;
			__IO uint32_t SMB1_RST_EN : 1;
			__IO uint32_t SMB2_RST_EN : 1;
			__IO uint32_t SMB3_RST_EN : 1;
			__IO uint32_t LED0_RST_EN : 1;
			__IO uint32_t LED1_RST_EN : 1;
			__IO uint32_t LED2_RST_EN : 1;
			__IO uint32_t RESV_6 : 1;
			__IO uint32_t SMB_4_RST_EN : 1;
			__IO uint32_t RESV_7 : 2;
			__IO uint32_t TIMER32_0_RST_EN : 1;
			__IO uint32_t TIMER32_1_RST_EN : 1;
			__IO uint32_t RESV_8 : 1;
			__IO uint32_t PKE_RST_EN : 1;
			__IO uint32_t RNG_RST_EN : 1;
			__IO uint32_t AES_HASH_RST_EN : 1;
			__IO uint32_t HTIMER_1_RST_EN : 1;
			__IO uint32_t CCTIMER_RST_EN : 1;
		} RST_EN_3_b;
	};

	union {
		/* Reset Enable 4 Register (+0x80) */
		__IO uint32_t RST_EN_4;

		struct {
			__IO uint32_t RESV_1 : 6;
			__IO uint32_t RTOS_RST_EN : 1;
			__IO uint32_t RESV_2 : 1;
			__IO uint32_t QSPI_RST_EN : 1;
			__IO uint32_t RESV_3 : 5;
			__IO uint32_t EEPROM_RST_EN : 1;
		} RST_EN_4_b;
	};

	union {
		/* Peripheral Reset Lock Register (+0x84) */
		__IO uint32_t PERIPH_RESET_LOCK_REG;

		struct {
			__IO uint32_t PCR_RST_EN_LOCK : 32;
		} PERIPH_RESET_LOCK_REG_b;
	};

	union {
		/* VBAT Soft RESET Register (+0x88) */
		__IO uint32_t VBAT_SOFT_RESET;

		struct {
			__IO uint32_t SOFT_VBAT_POR : 1;
		} VBAT_SOFT_RESET_b;
	};

	union {
		/* VBAT Soft Reset Register (+0x8c) */
		__IO uint32_t VTR_32K_SRC;

		struct {
			__IO uint32_t PLL_REF_SOURCE : 2;
		} VTR_32K_SRC_b;
	};

	/* (+0x90, 0x94, 0x98, 0x9c, 0xa0, 0xa4, 0xa8, 0xac,0xb0,0xb4,0xb8,0xbc)
	 */
	__I uint32_t RESERVED3[12];

	union {
		/* VBAT Soft Reset Register (+0xC0) */
		__IO uint32_t PERIOD_CNT_32K;

		struct {
			__IO uint32_t PERIOD_COUNTER : 16;
		} PERIOD_CNT_32K_b;
	};
	union {
		/* 32Khz high pulse count Register (+0xC4) */
		__IO uint32_t HI_PULSE_CNT_32K;

		struct {
			__IO uint32_t HIGH_COUNTER : 16;
		} HI_PULSE_CNT_32K_b;
	};
	union {
		/* 32Khz period MIN count Register (+0xC8) */
		__IO uint32_t MIN_PERIOD_CNT_32K;

		struct {
			__IO uint32_t PERIOD_MIN_COUNTER : 16;
		} MIN_PERIOD_CNT_32K_b;
	};
	union {
		/* 32Khz period MAX count Register (+0xCC) */
		__IO uint32_t MAX_PERIOD_CNT_32K;

		struct {
			__IO uint32_t PERIOD_MAX_COUNTER : 16;
		} MAX_PERIOD_CNT_32K_b;
	};
	union {
		/* 32Khz duty cycle variation Register (+0xD0) */
		__IO uint32_t DUTY_CYCLE_VAR_CNT_32K;

		struct {
			__IO uint32_t DUTY_VAR_COUNTER : 16;
		} DUTY_CYCLE_VAR_CNT_32K_b;
	};
	union {
		/* 32Khz duty cycle variation MAX Register (+0xD4) */
		__IO uint32_t DUTY_CYCLE_VAR_MAX_CNT_32K;

		struct {
			__IO uint32_t DUTY_VAR_MAX : 16;
		} DUTY_CYCLE_VAR_MAX_CNT_32K_b;
	};
	union {
		/* 32Khz Valid Count Register (+0xD8) */
		__IO uint32_t VALID_CNT_32K;

		struct {
			__IO uint32_t VALID_COUNT : 16;
		} VALID_CNT_32K_b;
	};
	union {
		/* 32Khz Valid Count MIN Register (+0xDC) */
		__IO uint32_t VALID_CNT_MIN_32K;

		struct {
			__IO uint32_t VALID_MIN_COUNT : 8;
		} VALID_CNT_MIN_32K_b;
	};
	union {
		/* 32Khz Control Register (+0xE0) */
		__IO uint32_t CONTROL_32K;

		struct {
			__IO uint32_t PERIOD_CNT_ENABLE : 1;
			__IO uint32_t DUTY_CYCLE_CNT_ENABLE : 1;
			__IO uint32_t VALID_ENABLE : 1;
			__IO uint32_t RESV_1 : 1;
			__IO uint32_t SOURCE : 1;
			__IO uint32_t RESV_2 : 19;
			__IO uint32_t CLR_COUNTERS : 1;
			__IO uint32_t RESV_3 : 7;
		} CONTROL_32K_b;
	};
	union {
		/* 32Khz Source Interrupt Register (+0xE4) */
		__IO uint32_t SRC_INT_32K;

		struct {
			__IO uint32_t PULSE_RDY_INT : 1;
			__IO uint32_t PASS_PERIOD_INT : 1;
			__IO uint32_t PASS_DUTY_INT : 1;
			__IO uint32_t FAIL_INT : 1;
			__IO uint32_t STALL_INT : 1;
			__IO uint32_t VALID_INT : 1;
			__IO uint32_t UNWELL_INT : 1;
			__IO uint32_t RESV : 25;

		} SRC_INT_32K_b;
	};
	union {
		/* 32Khz Source Interrupt Enable Register (+0xE8) */
		__IO uint32_t SRC_INT_ENABLE_32K;

		struct {
			__IO uint32_t PULSE_RDY_ENABLE : 1;
			__IO uint32_t PASS_PERIOD_ENABLE : 1;
			__IO uint32_t PASS_DUTY_ENABLE : 1;
			__IO uint32_t FAIL_ENABLE : 1;
			__IO uint32_t STALL_ENABLE : 1;
			__IO uint32_t VALID_ENABLE : 1;
			__IO uint32_t UNWELL_ENABLE : 1;
			__IO uint32_t RESV : 25;
		} SRC_INT_ENABLE_32K_b;
	};
};

/* DMA Main Registers (DMA_MAIN_INST) */

struct DMA_MAIN_INST_Type {
	/* DMA_MAIN_INST Structure */
	union {
		__IO uint8_t DMA_MAIN_CONTROL;

		struct {
			__IO uint8_t ACTIVATE : 1;

			__O uint8_t SOFT_RESET : 1;
		} DMA_MAIN_CONTROL_b;
	};
	__I uint8_t RESERVED[3];
	__I uint32_t DATA_PACKET;
};

/* DMA Channel 00 Registers (DMA_CHAN00_INST) */

struct DMA_CHAN00_INST_Type {
	/* DMA_CHAN00_INST Structure */
	union {
		__IO uint8_t DMA_CHANNEL_ACTIVATE;

		struct {
			__IO uint8_t CHANNEL_ACTIVATE : 1;
		} DMA_CHANNEL_ACTIVATE_b;
	};
	__I uint8_t RESERVED[3];
	__IO uint32_t MEMORY_START_ADDRESS;
	__IO uint32_t MEMORY_END_ADDRESS;
	__IO uint32_t DEVICE_ADDRESS;

	union {
		/* DMA Channel N Control */
		__IO uint32_t CONTROL;

		struct {
			__IO uint32_t RUN : 1;
			__IO uint32_t REQUEST : 1;
			__IO uint32_t DONE : 1;
			__IO uint32_t STATUS : 2;
			__IO uint32_t BUSY : 1;
			__IO uint32_t RESV_1 : 2;
			__IO uint32_t TX_DIRECTION : 1;
			__IO uint32_t HARDWARE_FLOW_CONTROL_DEVICE : 7;
			__IO uint32_t INCREMENT_MEM_ADDR : 1;
			__IO uint32_t INCREMENT_DEVICE_ADDR : 1;
			__IO uint32_t LOCK : 1;
			__IO uint32_t DISABLE_HW_FLOW_CONTROL : 1;
			__IO uint32_t TRANSFER_SIZE : 3;
			__IO uint32_t RESV_2 : 1;
			__IO uint32_t TRANSFER_GO : 1;
			__IO uint32_t TRANSFER_ABORT : 1;
		} CONTROL_b;
	};

	union {
		/* DMA Channel N Interrupt Status */
		__IO uint8_t INT_STATUS;

		struct {
			__IO uint8_t BUS_ERROR : 1;
			__IO uint8_t FLOW_CONTROL : 1;
			__IO uint8_t DONE : 1;
		} INT_STATUS_b;
	};
	__I uint8_t RESERVED1[3];

	union {
		/* DMA CHANNEL N INTERRUPT ENABLE */
		__IO uint8_t INT_EN;

		struct {
			__IO uint8_t STATUS_ENABLE_BUS_ERROR : 1;
			__IO uint8_t STATUS_ENABLE_FLOW_CONTROL : 1;
			__IO uint8_t STATUS_ENABLE_DONE : 1;
		} INT_EN_b;
	};
	__I uint8_t RESERVED2[7];

	union {
		/* DMA CHANNEL N CRC ENABLE */
		__IO uint32_t CRC_ENABLE;

		struct {
			__IO uint32_t CRC_MODE_ENABLE : 1;
			__IO uint32_t CRC_POST_TRANSFER_ENABLE : 1;
		} CRC_ENABLE_b;
	};

	union {
		/* DMA CHANNEL N CRC DATA */
		__IO uint32_t CRC_DATA;

		struct {
			__IO uint32_t CRC : 32;
		} CRC_DATA_b;
	};

	union {
		/* DMA CHANNEL N CRC POST STATUS */
		__IO uint32_t CRC_POST_STATUS;

		struct {
			__I uint32_t CRC_DONE : 1;
			__I uint32_t CRC_RUNNING : 1;
			__I uint32_t CRC_DATA_DONE : 1;
			__I uint32_t CRC_DATA_READY : 1;
		} CRC_POST_STATUS_b;
	};
};

/*
 * The UART is a full-function Two Pin Serial Port that supports the standard
 * RS-232 Interface. (UART0_INST)
 */

struct UART0_INST_Type {
	/* UART0_INST Structure */

	union {
		/* UART Programmable BAUD Rate Generator (LSB) Register (DLAB=1)
		 */
		__IO uint8_t BAUDRATE_LSB;
		/* UART Transmit (Write) Buffer Register (DLAB=0) */
		__O uint8_t TX_DATA;
		/* UART Receive (Read) Buffer Register (DLAB=0) */
		__I uint8_t RX_DATA;
	};

	union {
		union {
			__IO uint8_t INT_EN;

			struct {
				/*
				 * ERDAI This bit enables the Received Data
				 * Available Interrupt (and timeout interrupts
				 * in the FIFO mode) when set to logic '1'.
				 */
				__IO uint8_t ERDAI : 1;
				/*
				 * ETHREI This bit enables the Transmitter
				 * Holding Register Empty Interrupt when set to
				 * logic '1'.
				 */
				__IO uint8_t ETHREI : 1;
				/*
				 * ELSI This bit enables the Received Line
				 * Status Interrupt when set to logic '1'.
				 */
				__IO uint8_t ELSI : 1;
				/*
				 * EMSI This bit enables the MODEM Status
				 * Interrupt when set to logic '1'.
				 */
				__IO uint8_t EMSI : 1;
			} INT_EN_b;
		};
		/*
		 * UART Programmable BAUD Rate Generator (MSB) Register
		 * (DLAB=1). [6:0] BAUD_RATE_DIVISOR_MSB, [7:7] BAUD_CLK_SEL
		 * 1=If CLK_SRC is '0', the baud clock is derived from the
		 *   1.8432MHz_Clk.
		 *   If CLK_SRC is '1', this bit has no effect
		 * 0=If CLK_SRC is '0', the baud clock is derived from the
		 * 24MHz_Clk. If CLK_SRC is '1', this bit has no effect
		 */
		__IO uint8_t BAUDRATE_MSB;
	};

	union {
		union {
			/* UART Interrupt Identification Register */
			__IO uint8_t INT_ID;

			struct {
				/*
				 * IPEND This bit can be used in either a
				 * hardwired prioritized or polled environment
				 * to indicate whether an interrupt is pending.
				 */
				__I uint8_t IPEND : 1;
				/*
				 * INTID These bits identify the highest
				 * priority interrupt pending
				 */
				__I uint8_t INTID : 3;
				__IO uint8_t RESV : 2;
				/* These two bits are set when the FIFO CONTROL
				 * Register bit 0 equals 1.
				 */
				__I uint8_t FIFO_EN : 2;
			} INT_ID_b;
		};

		union {
			/* UART FIFO Control Register */
			__IO uint8_t FIFO_CR;

			struct {
				/* EXRF Enable XMIT and RECV FIFO. */
				__O uint8_t EXRF : 1;
				/*
				 * CLEAR_RECV_FIFO Setting this bit to a logic
				 * '1' clears all bytes in the RCVR FIFO and
				 * resets its counter logic to '0'.
				 */
				__O uint8_t CLEAR_RECV_FIFO : 1;
				/*
				 * CLEAR_XMIT_FIFO Setting this bit to a logic
				 * '1' clears all bytes in the XMIT FIFO and
				 * resets its counter logic to '0'. The shift
				 * register is not cleared. This bit is
				 * self-clearing.
				 */
				__O uint8_t CLEAR_XMIT_FIFO : 1;
				/*
				 * DMA_MODE_SELECT Writing to this bit has no
				 * effect on the operation of the UART. The
				 * RXRDY and TXRDY pins are not available on
				 * this chip.
				 */
				__IO uint8_t DMA_MODE_SELECT : 1;
				__IO uint8_t RESV : 2;
				/*
				 * RECV_FIFO_TRIGGER_LEVEL These bits are used
				 * to set the trigger level for the RCVR FIFO
				 * interrupt.
				 */
				__IO uint8_t RECV_FIFO_TRIGGER_LEVEL : 2;
			} FIFO_CR_b;
		};
	};

	union {
		/* UART Line Control Register */
		__IO uint8_t LINE_CR;

		struct {
			/*
			 * WORD_LENGTH These two bits specify the number of bits
			 * in each transmitted or received serial character.
			 */
			__IO uint8_t WORD_LENGTH : 2;
			/*
			 * STOP_BITS This bit specifies the number of stop bits
			 * in each transmitted or received serial character.
			 */
			__IO uint8_t STOP_BITS : 1;
			/* ENABLE_PARITY Parity Enable bit. */
			__IO uint8_t ENABLE_PARITY : 1;
			/* PARITY_SELECT Even Parity Select bit. */
			__IO uint8_t PARITY_SELECT : 1;
			/* STICK_PARITY Stick Parity bit. */
			__IO uint8_t STICK_PARITY : 1;
			/* BREAK_CONTROL Set Break Control bit */
			__IO uint8_t BREAK_CONTROL : 1;
			/* DLAB Divisor Latch Access Bit (DLAB). */
			__IO uint8_t DLAB : 1;
		} LINE_CR_b;
	};

	union {
		/* UART Modem Control Register */
		__IO uint8_t MODEM_CR;

		struct {
			/*
			 * DTR This bit controls the Data Terminal Ready (nDTR)
			 * output.
			 */
			__IO uint8_t DTR : 1;
			/*
			 * RTS This bit controls the Request To Send (nRTS)
			 * output.
			 */
			__IO uint8_t RTS : 1;
			/* OUT1 This bit controls the Output 1 (OUT1) bit. */
			__IO uint8_t OUT1 : 1;
			/* OUT2 This bit is used to enable an UART interrupt. */
			__IO uint8_t OUT2 : 1;
			/*
			 * LOOPBACK This bit provides the loopback feature for
			 * diagnostic testing of the Serial Port.
			 */
			__IO uint8_t LOOPBACK : 1;
		} MODEM_CR_b;
	};

	union {
		/* UART Line Status Register */
		__I uint8_t LINE_STS;

		struct {
			/*
			 * DATA_READY Data Ready. It is set to a logic '1'
			 * whenever a complete incoming character has been
			 * received and transferred into the Receiver Buffer
			 * Register or the FIFO.
			 */
			__I uint8_t DATA_READY : 1;
			/* OVERRUN Overrun Error. */
			__I uint8_t OVERRUN : 1;
			/* PARITY ERROR Parity Error. */
			__I uint8_t PE : 1;
			/* FRAME_ERROR Framing Error. */
			__I uint8_t FRAME_ERROR : 1;
			/* BREAK_INTERRUPT Break Interrupt. */
			__I uint8_t BREAK_INTERRUPT : 1;
			/*
			 * TRANSMIT_EMPTY Transmitter Holding Register Empty Bit
			 * 5 indicates that the Serial Port is ready to accept a
			 * new character for transmission.
			 */
			__I uint8_t TRANSMIT_EMPTY : 1;
			/*
			 * Transmitter Empty. Bit 6 is set to a logic '1'
			 * whenever the Transmitter Holding Register (THR) and
			 * Transmitter Shift Register (TSR) are both empty.
			 */
			__I uint8_t TRANSMIT_ERROR : 1;
			__I uint8_t FIFO_ERROR : 1;
		} LINE_STS_b;
	};

	union {
		/* UART Modem Status Register */
		__I uint8_t MODEM_STS;

		struct {
			/* CTS Delta Clear To Send (DCTS). */
			__I uint8_t CTS : 1;
			/* DSR Delta Data Set Ready (DDSR). */
			__I uint8_t DSR : 1;
			/* RI Trailing Edge of Ring Indicator (TERI). */
			__I uint8_t RI : 1;
			/* DCD Delta Data Carrier Detect (DDCD). */
			__I uint8_t DCD : 1;
			/*
			 * nCTS This bit is the complement of the Clear To Send
			 * (nCTS) input.
			 */
			__IO uint8_t nCTS : 1;
			/*
			 * This bit is the complement of the Data Set Ready
			 * (nDSR) input.
			 */
			__IO uint8_t nDSR : 1;
			/*
			 * nRI This bit is the complement of the Ring Indicator
			 * (nRI) input.
			 */
			__IO uint8_t nRI : 1;
			/*
			 * nDCD This bit is the complement of the Data Carrier
			 * Detect (nDCD) input.
			 */
			__IO uint8_t nDCD : 1;
		} MODEM_STS_b;
	};
	/*
	 * UART Scratchpad Register This 8 bit read/write register has no effect
	 * on the operation of the Serial Port. It is intended as a scratchpad
	 * register to be used by the programmer to hold data temporarily.
	 */
	__IO uint8_t SCRATCHPAD;
	__I uint32_t RESERVED[202];
	/*
	 * UART Activate Register. [0:0] ACTIVATE When this bit is 1, the UART
	 * logical device is powered and functional. When this bit is 0, the
	 * UART logical device is powered down and inactive.
	 */
	__IO uint8_t ACTIVATE;
	__I uint8_t RESERVED1[191];

	union {
		/* UART Config Select Register */
		__IO uint8_t CONFIG;

		struct {
			/*
			 * CLK_SRC
			 * 1=The UART Baud Clock is derived from an external
			 * clock source, 0=The UART Baud Clock is derived from
			 * one of the two internal clock sources
			 */
			__IO uint8_t CLK_SRC : 1;
			/*
			 * POWER 1=The RESET reset signal is derived from
			 * nSIO_RESET, 0=The RESET reset signal is derived from
			 * VCC1_RESET
			 */
			__IO uint8_t POWER : 1;
			/*
			 * POLARITY
			 * 1=The UART_TX and UART_RX pins functions are
			 * inverted, 0=The UART_TX and UART_RX pins functions
			 * are not inverted
			 */
			__IO uint8_t POLARITY : 1;
		} CONFIG_b;
	};
};

struct GPIO_PIN_CTRL1_Type {
	union {
		__IO uint32_t GPIO_PIN_CONTROL1;

		struct {
			__IO uint32_t PU_PD : 2;
			__IO uint32_t POWER_GATING : 2;
			__IO uint32_t INTERRUPT_DETECTION : 3;
			__IO uint32_t EDGE_ENABLE : 1;
			__IO uint32_t OUTPUT_BUFFER_TYPE : 1;
			__IO uint32_t GPIO_DIRECTION : 1;
			__IO uint32_t GPIO_OUTPUT_SELECT : 1;
			__IO uint32_t POLARITY : 1;
			__IO uint32_t MUX_CONTROL : 2;
			__IO uint32_t RESV_1 : 1;
			__IO uint32_t INPUT_DISABLE : 1;
			__IO uint32_t ALT_GPIO_DATA : 1;
			__IO uint32_t RESV_2 : 7;
			__I uint32_t GPIO_INPUT : 1;
			__IO uint32_t RESV_3 : 7;
		} bit_field;
	};
};

/*
 * The Quad SPI Master Controller may be used to communicate with
 * various peripheral devices that use a Serial Peripheral Interface, such as
 * EEPROMS, DACs and ADCs. The controller can be configured to support advanced
 * SPI Flash devices with multi-phase access protocols.  (QMSPI_INST)
 */

struct QMSPI_INST_Type {
	/* QMSPI_INST Structure */

	union {
		__IO uint32_t QMSPI_MODE;

		struct {
			__IO uint32_t ACTIVATE : 1;
			__O uint32_t SOFT_RESET : 1;
			__IO uint32_t RESV_1 : 6;
			__IO uint32_t CPOL : 1;
			__IO uint32_t CHPA_MOSI : 1;
			__IO uint32_t CHPA_MISO : 1;
			__IO uint32_t RESV_2 : 5;
			__IO uint32_t CLOCK_DIVIDE : 9;
		} QMSPI_MODE_b;
	};

	union {
		__IO uint32_t QMSPI_CTRL;

		struct {
			__IO uint32_t INTERFACE_MODE : 2;
			__IO uint32_t TX_TRANSFER_ENABLE : 2;
			__IO uint32_t TX_DMA_ENABLE : 2;
			__IO uint32_t RX_TRANSFER_ENABLE : 1;
			__IO uint32_t RX_DMA_ENABLE : 2;
			__IO uint32_t CLOSE_TRANSFER_ENABLE : 1;
			__IO uint32_t TRANSFER_UNITS : 2;
			__IO uint32_t DESCRIPTION_BUFFER_POINTER : 4;
			__IO uint32_t DESCRIPTION_BUFFER_ENABLE : 1;
			__IO uint32_t TRANSFER_LENGTH : 15;
		} QMSPI_CONTROL_b;
	};

	union {
		__IO uint32_t QMSPI_EXECUTE;

		struct {
			__O uint32_t START : 1;
			__O uint32_t STOP : 1;
			__O uint32_t CLEAR_DATA_BUFFER : 1;
		} QMSPI_EXECUTE_b;
	};

	union {
		__IO uint32_t QMSPI_INTERFACE_CONTROL;

		struct {
			__IO uint32_t WRITE_PROTECT_OUT_VALUE : 1;
			__IO uint32_t WRITE_PROTECT_OUT_ENABLE : 1;
			__IO uint32_t HOLD_OUT_VALUE : 1;
			__IO uint32_t HOLD_OUT_ENABLE : 1;
			__IO uint32_t PULLDOWN_ON_NOT_SELECTED : 1;
			__IO uint32_t PULLUP_ON_NOT_SELECTED : 1;
			__IO uint32_t PULLDOWN_ON_NOT_DRIVEN : 1;
			__IO uint32_t PULLUP_ON_NOT_DRIVEN : 1;
		} QMSPI_INTERFACE_CONTROL_b;
	};

	union {
		__IO uint32_t QMSPI_STATUS;

		struct {
			__IO uint32_t TRANSFER_COMPLETE : 1;
			__IO uint32_t DMA_COMPLETE : 1;
			__IO uint32_t TRANSMIT_BUFFER_ERROR : 1;
			__IO uint32_t RECEIVE_BUFFER_ERROR : 1;
			__IO uint32_t PROGRAMMING_ERROR : 1;
			__IO uint32_t RESV_1 : 3;
			__I uint32_t TRANSMIT_BUFFER_FULL : 1;
			__I uint32_t TRANSMIT_BUFFER_EMPTY : 1;
			__IO uint32_t TRANSMIT_BUFFER_REQUEST : 1;
			__IO uint32_t TRANSMIT_BUFFER_STALL : 1;
			__I uint32_t RECEIVE_BUFFER_FULL : 1;
			__I uint32_t RECEIVE_BUFFER_EMPTY : 1;
			__IO uint32_t RECEIVE_BUFFER_REQUEST : 1;
			__IO uint32_t RECEIVE_BUFFER_STALL : 1;
			__I uint32_t TRANSFER_ACTIVE : 1;
			__IO uint32_t RESV_2 : 7;
			__I uint32_t CURRENT_DESCRIPTION_BUFFER : 4;
		} QMSPI_STATUS_b;
	};

	union {
		__IO uint32_t QMSPI_BUFFER_COUNT_STATUS;

		struct {
			__IO uint32_t TRANSMIT_BUFFER_COUNT : 16;
			__IO uint32_t RECEIVE_BUFFER_COUNT : 16;
		} QMSPI_BUFFER_COUNT_STATUS_b;
	};

	union {
		__IO uint32_t QMSPI_INTERRUPT_ENABLE;

		struct {
			__IO uint32_t TRANSFER_COMPLETE_ENABLE : 1;
			__IO uint32_t DMA_COMPLETE_ENABLE : 1;
			__IO uint32_t TRANSMIT_BUFFER_ERROR_ENABLE : 1;
			__IO uint32_t RECEIVE_BUFFER_ERROR_ENABLE : 1;
			__IO uint32_t PROGRAMMING_ERROR_ENABLE : 1;
			__IO uint32_t RESV_1 : 3;
			__I uint32_t TRANSMIT_BUFFER_FULL_ENABLE : 1;
			__I uint32_t TRANSMIT_BUFFER_EMPTY_ENABLE : 1;
			__IO uint32_t TRANSMIT_BUFFER_REQUEST_ENABLE : 1;
			__IO uint32_t RESV_2 : 1;
			__I uint32_t RECEIVE_BUFFER_FULL_ENABLE : 1;
			__I uint32_t RECEIVE_BUFFER_EMPTY_ENABLE : 1;
			__IO uint32_t RECEIVE_BUFFER_REQUEST_ENABLE : 1;
		} QMSPI_INTERRUPT_ENABLE_b;
	};

	union {
		__IO uint32_t QMSPI_BUFFER_COUNT_TRIGGER;

		struct {
			__IO uint32_t TRANSMIT_BUFFER_TRIGGER : 16;
			__IO uint32_t RECEIVE_BUFFER_TRIGGER : 16;
		} QMSPI_BUFFER_COUNT_TRIGGER_b;
	};

	union {
		__IO uint32_t QMSPI_TRANSMIT_BUFFER;

		struct {
			__O uint32_t TRANSMIT_BUFFER : 32;
		} QMSPI_TRANSMIT_BUFFER_b;
	};

	union {
		__IO uint32_t QMSPI_RECEIVE_BUFFER;

		struct {
			__I uint32_t RECEIVE_BUFFER : 32;
		} QMSPI_RECEIVE_BUFFER_b;
	};

	union {
		__IO uint32_t QMSPI_CS_TIMING_REG;

		struct {
			__IO uint32_t DLY_CS_ON_TO_CLOCK_START : 4;
			__IO uint32_t RESV_1 : 4;
			__IO uint32_t DLY_CLK_STOP_TO_CS_OFF : 4;
			__IO uint32_t RESV_2 : 4;
			__IO uint32_t DLY_LAST_DATA_HOLD : 4;
			__IO uint32_t RESV_3 : 3;
			__IO uint32_t DLY_CS_OFF_TO_CS_ON : 9;
		} QMSPI_CS_TIMING_REG_b;
	};
	__I uint32_t RESERVED;

	union {
		__IO uint32_t QMSPI_DESCRIPTION_BUFFER_0;

		struct {
			__IO uint32_t INTERFACE_MODE : 2;
			__IO uint32_t TX_TRANSFER_ENABLE : 2;
			__IO uint32_t TX_DMA_ENABLE : 2;
			__IO uint32_t RX_TRANSFER_ENABLE : 1;
			__I uint32_t RX_DMA_ENABLE : 2;
			__I uint32_t CLOSE_TRANFSER_ENABLE : 1;
			__IO uint32_t TRANSFER_LENGTH_BITS : 1;
			__I uint32_t DESCRIPTION_BUFFER_LAST : 1;
			__I uint32_t DESCRIPTION_BUFFER_NEXT_POINTER : 4;
			__IO uint32_t TRANSFER_LENGTH : 16;
		} QMSPI_DESCRIPTION_BUFFER_0_b;
	};

	union {
		__IO uint32_t QMSPI_DESCRIPTION_BUFFER_1;

		struct {
			__IO uint32_t INTERFACE_MODE : 2;
			__IO uint32_t TX_TRANSFER_ENABLE : 2;
			__IO uint32_t TX_DMA_ENABLE : 2;
			__IO uint32_t RX_TRANSFER_ENABLE : 1;
			__I uint32_t RX_DMA_ENABLE : 2;
			__I uint32_t CLOSE_TRANFSER_ENABLE : 1;
			__IO uint32_t TRANSFER_LENGTH_BITS : 1;
			__I uint32_t DESCRIPTION_BUFFER_LAST : 1;
			__I uint32_t DESCRIPTION_BUFFER_NEXT_POINTER : 4;
			__IO uint32_t TRANSFER_LENGTH : 16;
		} QMSPI_DESCRIPTION_BUFFER_1_b;
	};

	union {
		__IO uint32_t QMSPI_DESCRIPTION_BUFFER_2;

		struct {
			__IO uint32_t INTERFACE_MODE : 2;
			__IO uint32_t TX_TRANSFER_ENABLE : 2;
			__IO uint32_t TX_DMA_ENABLE : 2;
			__IO uint32_t RX_TRANSFER_ENABLE : 1;
			__I uint32_t RX_DMA_ENABLE : 2;
			__I uint32_t CLOSE_TRANFSER_ENABLE : 1;
			__IO uint32_t TRANSFER_LENGTH_BITS : 1;
			__I uint32_t DESCRIPTION_BUFFER_LAST : 1;
			__I uint32_t DESCRIPTION_BUFFER_NEXT_POINTER : 4;
			__IO uint32_t TRANSFER_LENGTH : 16;
		} QMSPI_DESCRIPTION_BUFFER_2_b;
	};

	union {
		__IO uint32_t QMSPI_DESCRIPTION_BUFFER_3;

		struct {
			__IO uint32_t INTERFACE_MODE : 2;
			__IO uint32_t TX_TRANSFER_ENABLE : 2;
			__IO uint32_t TX_DMA_ENABLE : 2;
			__IO uint32_t RX_TRANSFER_ENABLE : 1;
			__I uint32_t RX_DMA_ENABLE : 2;
			__I uint32_t CLOSE_TRANFSER_ENABLE : 1;
			__IO uint32_t TRANSFER_LENGTH_BITS : 1;
			__I uint32_t DESCRIPTION_BUFFER_LAST : 1;
			__I uint32_t DESCRIPTION_BUFFER_NEXT_POINTER : 4;
			__IO uint32_t TRANSFER_LENGTH : 16;
		} QMSPI_DESCRIPTION_BUFFER_3_b;
	};

	union {
		__IO uint32_t QMSPI_DESCRIPTION_BUFFER_4;

		struct {
			__IO uint32_t INTERFACE_MODE : 2;
			__IO uint32_t TX_TRANSFER_ENABLE : 2;
			__IO uint32_t TX_DMA_ENABLE : 2;
			__IO uint32_t RX_TRANSFER_ENABLE : 1;
			__I uint32_t RX_DMA_ENABLE : 2;
			__I uint32_t CLOSE_TRANFSER_ENABLE : 1;
			__IO uint32_t TRANSFER_LENGTH_BITS : 1;
			__I uint32_t DESCRIPTION_BUFFER_LAST : 1;
			__I uint32_t DESCRIPTION_BUFFER_NEXT_POINTER : 4;
			__IO uint32_t TRANSFER_LENGTH : 16;
		} QMSPI_DESCRIPTION_BUFFER_4_b;
	};

	union {
		__IO uint32_t QMSPI_DESCRIPTION_BUFFER_5;

		struct {
			__IO uint32_t INTERFACE_MODE : 2;
			__IO uint32_t TX_TRANSFER_ENABLE : 2;
			__IO uint32_t TX_DMA_ENABLE : 2;
			__IO uint32_t RX_TRANSFER_ENABLE : 1;
			__I uint32_t RX_DMA_ENABLE : 2;
			__I uint32_t CLOSE_TRANFSER_ENABLE : 1;
			__IO uint32_t TRANSFER_LENGTH_BITS : 1;
			__I uint32_t DESCRIPTION_BUFFER_LAST : 1;
			__I uint32_t DESCRIPTION_BUFFER_NEXT_POINTER : 4;
			__IO uint32_t TRANSFER_LENGTH : 16;
		} QMSPI_DESCRIPTION_BUFFER_5_b;
	};

	union {
		__IO uint32_t QMSPI_DESCRIPTION_BUFFER_6;

		struct {
			__IO uint32_t INTERFACE_MODE : 2;
			__IO uint32_t TX_TRANSFER_ENABLE : 2;
			__IO uint32_t TX_DMA_ENABLE : 2;
			__IO uint32_t RX_TRANSFER_ENABLE : 1;
			__I uint32_t RX_DMA_ENABLE : 2;
			__I uint32_t CLOSE_TRANFSER_ENABLE : 1;
			__IO uint32_t TRANSFER_LENGTH_BITS : 1;
			__I uint32_t DESCRIPTION_BUFFER_LAST : 1;
			__I uint32_t DESCRIPTION_BUFFER_NEXT_POINTER : 4;
			__IO uint32_t TRANSFER_LENGTH : 16;
		} QMSPI_DESCRIPTION_BUFFER_6_b;
	};

	union {
		__IO uint32_t QMSPI_DESCRIPTION_BUFFER_7;

		struct {
			__IO uint32_t INTERFACE_MODE : 2;
			__IO uint32_t TX_TRANSFER_ENABLE : 2;
			__IO uint32_t TX_DMA_ENABLE : 2;
			__IO uint32_t RX_TRANSFER_ENABLE : 1;
			__I uint32_t RX_DMA_ENABLE : 2;
			__I uint32_t CLOSE_TRANFSER_ENABLE : 1;
			__IO uint32_t TRANSFER_LENGTH_BITS : 1;
			__I uint32_t DESCRIPTION_BUFFER_LAST : 1;
			__I uint32_t DESCRIPTION_BUFFER_NEXT_POINTER : 4;
			__IO uint32_t TRANSFER_LENGTH : 16;
		} QMSPI_DESCRIPTION_BUFFER_7_b;
	};

	union {
		__IO uint32_t QMSPI_DESCRIPTION_BUFFER_8;

		struct {
			__IO uint32_t INTERFACE_MODE : 2;
			__IO uint32_t TX_TRANSFER_ENABLE : 2;
			__IO uint32_t TX_DMA_ENABLE : 2;
			__IO uint32_t RX_TRANSFER_ENABLE : 1;
			__I uint32_t RX_DMA_ENABLE : 2;
			__I uint32_t CLOSE_TRANFSER_ENABLE : 1;
			__IO uint32_t TRANSFER_LENGTH_BITS : 1;
			__I uint32_t DESCRIPTION_BUFFER_LAST : 1;
			__I uint32_t DESCRIPTION_BUFFER_NEXT_POINTER : 4;
			__IO uint32_t TRANSFER_LENGTH : 16;
		} QMSPI_DESCRIPTION_BUFFER_8_b;
	};

	union {
		__IO uint32_t QMSPI_DESCRIPTION_BUFFER_9;

		struct {
			__IO uint32_t INTERFACE_MODE : 2;
			__IO uint32_t TX_TRANSFER_ENABLE : 2;
			__IO uint32_t TX_DMA_ENABLE : 2;
			__IO uint32_t RX_TRANSFER_ENABLE : 1;
			__I uint32_t RX_DMA_ENABLE : 2;
			__I uint32_t CLOSE_TRANFSER_ENABLE : 1;
			__IO uint32_t TRANSFER_LENGTH_BITS : 1;
			__I uint32_t DESCRIPTION_BUFFER_LAST : 1;
			__I uint32_t DESCRIPTION_BUFFER_NEXT_POINTER : 4;
			__IO uint32_t TRANSFER_LENGTH : 16;
		} QMSPI_DESCRIPTION_BUFFER_9_b;
	};

	union {
		__IO uint32_t QMSPI_DESCRIPTION_BUFFER_10;

		struct {
			__IO uint32_t INTERFACE_MODE : 2;
			__IO uint32_t TX_TRANSFER_ENABLE : 2;
			__IO uint32_t TX_DMA_ENABLE : 2;
			__IO uint32_t RX_TRANSFER_ENABLE : 1;
			__I uint32_t RX_DMA_ENABLE : 2;
			__I uint32_t CLOSE_TRANFSER_ENABLE : 1;
			__IO uint32_t TRANSFER_LENGTH_BITS : 1;
			__I uint32_t DESCRIPTION_BUFFER_LAST : 1;
			__I uint32_t DESCRIPTION_BUFFER_NEXT_POINTER : 4;
			__IO uint32_t TRANSFER_LENGTH : 16;
		} QMSPI_DESCRIPTION_BUFFER_10_b;
	};

	union {
		__IO uint32_t QMSPI_DESCRIPTION_BUFFER_11;

		struct {
			__IO uint32_t INTERFACE_MODE : 2;
			__IO uint32_t TX_TRANSFER_ENABLE : 2;
			__IO uint32_t TX_DMA_ENABLE : 2;
			__IO uint32_t RX_TRANSFER_ENABLE : 1;
			__I uint32_t RX_DMA_ENABLE : 2;
			__I uint32_t CLOSE_TRANFSER_ENABLE : 1;
			__IO uint32_t TRANSFER_LENGTH_BITS : 1;
			__I uint32_t DESCRIPTION_BUFFER_LAST : 1;
			__I uint32_t DESCRIPTION_BUFFER_NEXT_POINTER : 4;
			__IO uint32_t TRANSFER_LENGTH : 16;
		} QMSPI_DESCRIPTION_BUFFER_11_b;
	};

	union {
		__IO uint32_t QMSPI_DESCRIPTION_BUFFER_12;

		struct {
			__IO uint32_t INTERFACE_MODE : 2;
			__IO uint32_t TX_TRANSFER_ENABLE : 2;
			__IO uint32_t TX_DMA_ENABLE : 2;
			__IO uint32_t RX_TRANSFER_ENABLE : 1;
			__I uint32_t RX_DMA_ENABLE : 2;
			__I uint32_t CLOSE_TRANFSER_ENABLE : 1;
			__IO uint32_t TRANSFER_LENGTH_BITS : 1;
			__I uint32_t DESCRIPTION_BUFFER_LAST : 1;
			__I uint32_t DESCRIPTION_BUFFER_NEXT_POINTER : 4;
			__IO uint32_t TRANSFER_LENGTH : 16;
		} QMSPI_DESCRIPTION_BUFFER_12_b;
	};

	union {
		__IO uint32_t QMSPI_DESCRIPTION_BUFFER_13;

		struct {
			__IO uint32_t INTERFACE_MODE : 2;
			__IO uint32_t TX_TRANSFER_ENABLE : 2;
			__IO uint32_t TX_DMA_ENABLE : 2;
			__IO uint32_t RX_TRANSFER_ENABLE : 1;
			__I uint32_t RX_DMA_ENABLE : 2;
			__I uint32_t CLOSE_TRANFSER_ENABLE : 1;
			__IO uint32_t TRANSFER_LENGTH_BITS : 1;
			__I uint32_t DESCRIPTION_BUFFER_LAST : 1;
			__I uint32_t DESCRIPTION_BUFFER_NEXT_POINTER : 4;
			__IO uint32_t TRANSFER_LENGTH : 16;
		} QMSPI_DESCRIPTION_BUFFER_13_b;
	};

	union {
		__IO uint32_t QMSPI_DESCRIPTION_BUFFER_14;

		struct {
			__IO uint32_t INTERFACE_MODE : 2;
			__IO uint32_t TX_TRANSFER_ENABLE : 2;
			__IO uint32_t TX_DMA_ENABLE : 2;
			__IO uint32_t RX_TRANSFER_ENABLE : 1;
			__I uint32_t RX_DMA_ENABLE : 2;
			__I uint32_t CLOSE_TRANFSER_ENABLE : 1;
			__IO uint32_t TRANSFER_LENGTH_BITS : 1;
			__I uint32_t DESCRIPTION_BUFFER_LAST : 1;
			__I uint32_t DESCRIPTION_BUFFER_NEXT_POINTER : 4;
			__IO uint32_t TRANSFER_LENGTH : 16;
		} QMSPI_DESCRIPTION_BUFFER_14_b;
	};

	union {
		__IO uint32_t QMSPI_DESCRIPTION_BUFFER_15;

		struct {
			__IO uint32_t INTERFACE_MODE : 2;
			__IO uint32_t TX_TRANSFER_ENABLE : 2;
			__IO uint32_t TX_DMA_ENABLE : 2;
			__IO uint32_t RX_TRANSFER_ENABLE : 1;
			__I uint32_t RX_DMA_ENABLE : 2;
			__I uint32_t CLOSE_TRANFSER_ENABLE : 1;
			__IO uint32_t TRANSFER_LENGTH_BITS : 1;
			__I uint32_t DESCRIPTION_BUFFER_LAST : 1;
			__I uint32_t DESCRIPTION_BUFFER_NEXT_POINTER : 4;
			__IO uint32_t TRANSFER_LENGTH : 16;
		} QMSPI_DESCRIPTION_BUFFER_15_b;
	};
};

/*
 * The VBAT Register Bank block is a block implemented for aggregating
 * miscellaneous battery-backed registers required the host and by the Embedded
 * Controller (EC) Subsystem that are not unique to a block implemented in the
 * EC subsystem.  (VBAT_INST)
 */

struct VBAT_INST_Type {
	/* VBAT_INST Structure */

	union {
		__IO uint8_t PFR_STS;

		struct {
			__IO uint8_t RESV : 2;
			__IO uint8_t SOFT : 1;
			__IO uint8_t TEST : 1;
			__IO uint8_t RESETI : 1;
			__IO uint8_t WDT_EVT : 1;
			__IO uint8_t SYSRESETREQ : 1;
			__IO uint8_t VBAT_RST : 1;
		} PFR_STS_b;
	};
	__I uint32_t RESERVED[1]; /* (+0x04) */

	union {
		/* (+0x08) VBAT SOURCE 32KHz Register CLOCK ENABLE */
		__IO uint32_t VBAT_SRC_32K;

		struct {
			__IO uint32_t INTERNAL_32K_ENABLE : 1;
			__IO uint32_t RESV_1 : 7;
			__IO uint32_t XTEL_ENABLE : 1;
			__IO uint32_t XTAL_XOSEL : 1;
			__IO uint32_t XTAL_START_DISABLE : 1;
			__IO uint32_t XTAL_CNTR : 2;
			__IO uint32_t RESV_2 : 3;

			__IO uint32_t PERIPH_32K_SOURCE : 2;
			__IO uint32_t INTERNAL_32K_SUPPRESS : 1;
			__IO uint32_t RESV_3 : 13;

		} VBAT_SRC_32K_b;
	};
	/* (+0x0C, 0x10) */
	__I uint32_t RESERVED2[2];

	union {
		/* (+0x14) */
		__IO uint32_t TRIM_CNT_32K;

		struct {
			__IO uint32_t TRIM_VAL : 8;
		} TRIM_CNT_32K_b;
	};
	/* (+0x18, 0x1C) */
	__I uint32_t RESERVED3[2];

	union {
		__IO uint32_t MONOTONIC_COUNTER;

		struct {
			__I uint32_t MONOTONIC_COUNTER : 32;
		} MONOTONIC_COUNTER_b;
	};

	union {
		/* COUNTER HIWORD    (+0x24) */
		__IO uint32_t COUNTER_HIWORD;

		struct {
			__IO uint32_t COUNTER_HIWORD : 32;
		} COUNTER_HIWORD_b;
	};

	union {
		/* (+0x28) */
		__IO uint32_t ROM_FEATURE;

		struct {
			__IO uint32_t ROM_STUFF : 8;
		} ROM_FEATURE_b;
	};
};

/*
 * This block is designed to be accessed internally by the EC via the
 * register interface.  (EC_REG_BANK_INST)
 */

struct EC_REG_BANK_INST_Type {
	/* EC_REG_BANK_INST Structure */
	__I uint32_t RESERVED;
	__IO uint32_t AHB_ERROR_ADDRESS;
	__I uint32_t RESERVED1[3];
	__IO uint8_t AHB_ERROR_CONTROL;
	__I uint8_t RESERVED2[3];
	__IO uint32_t INTERRUPT_CONTROL;
	__IO uint32_t ETM_TRACE_ENABLE;

	union {
		__IO uint32_t DEBUG_Enable;

		struct {
			__IO uint32_t DEBUG_EN : 1;
			__IO uint32_t DEBUG_PIN_CFG : 2;
			__IO uint32_t DEBUG_PU_EN : 1;
			__IO uint32_t BSP_EN : 1;
		} DEBUG_Enable_b;
	};

	union {
		__IO uint32_t LOCK;

		struct {
			__IO uint32_t TEST : 1;
			__IO uint32_t VBAT_RAM_LOCK : 1;
			__IO uint32_t VBAT_REG_LOCK : 1;
		} LOCK_b;
	};
	__IO uint32_t WDT_EVENT_COUNT;

	union {
		__IO uint32_t AES_HASH_BYTE_SWAP_CONTROL;

		struct {
			__I uint32_t INPUT_BYTE_SWAP_ENABLE : 1;
			__IO uint32_t OUTPUT_BYTE_SWAP_ENABLE : 1;
			__IO uint32_t INPUT_BLOCK_SWAP_ENABLE : 3;
			__IO uint32_t OUTPUT_BLOCK_SWAP_ENABLE : 3;
		} AES_HASH_BYTE_SWAP_CONTROL_b;
	};
	__I uint32_t RESERVED3[4];

	union {
		__IO uint32_t PECI_DISABLE;

		struct {
			__O uint32_t PECI_DISABLE : 1;
		} PECI_DISABLE_b;
	};
	__I uint32_t RESERVED4[2];

	union {
		__I uint32_t STM_REG;

		struct {
			__I uint32_t QA_MODE : 1;
			__I uint32_t VLD_MODE : 1;
			__I uint32_t BS_STATUS : 1;
			__I uint32_t INT_SPI_RECOV : 1;
		} STM_REG_b;
	};

	union {
		__IO uint32_t VCI_FWOVRD;

		struct {
			__IO uint32_t VCI_FW_OVRD : 1;
		} VCI_FWOVRD_b;
	};

	union {
		__IO uint8_t VTR_RSTBR_STAT;

		struct {
			__IO uint8_t RST_SYS_STAT : 1;
			__IO uint8_t WDT_STAT : 1;
		} VTR_RSTBR_STAT_b;
	};
	__I uint8_t RESERVED5[7];

	union {
		__IO uint32_t CRYPTO_SOFT_RESET;

		struct {
			__O uint32_t RNG_SOFT_RESET : 1;
			__O uint32_t PUBLIC_KEY_SOFT_RESET : 1;
			__O uint32_t AES_HASH_SOFT_RESET : 1;
		} CRYPTO_SOFT_RESET_b;
	};
	__I uint32_t RESERVED6;

	union {
		__IO uint32_t GPIO_BANK_PWR;

		struct {
			__IO uint32_t TEST : 1;
			__IO uint32_t VTR_LEVEL2 : 1;
			__IO uint32_t VTR_LEVEL3 : 1;
			__IO uint32_t RESV : 4;
			__IO uint32_t GPIO_BANK_POWER_LOCK : 1;
		} GPIO_BANK_POWER_b;
	};
	__I uint32_t RESERVED7[2];

	union {
		__IO uint32_t JTAG_MASTER_CFG;

		struct {
			__IO uint32_t JTM_CLK : 3;
			__IO uint32_t MASTER_SLAVE : 1;
		} JTAG_MASTER_CFG_b;
	};

	union {
		__I uint32_t JTAG_MASTER_STS;

		struct {
			__I uint32_t JTM_DONE : 1;
		} JTAG_MASTER_STS_b;
	};

	union {
		__IO uint32_t JTAG_MASTER_TDO;

		struct {
			__IO uint32_t JTM_TDO : 32;
		} JTAG_MASTER_TDO_b;
	};

	union {
		__IO uint32_t JTAG_MASTER_TDI;

		struct {
			__IO uint32_t JTM_TDI : 32;
		} JTAG_MASTER_TDI_b;
	};

	union {
		__IO uint32_t JTAG_MASTER_TMS;

		struct {
			__IO uint32_t JTM_TMS : 32;
		} JTAG_MASTER_TMS_b;
	};

	union {
		__IO uint32_t JTAG_MASTER_CMD;

		struct {
			__IO uint32_t JTM_COUNT : 5;
		} JTAG_MASTER_CMD_b;
	};
	__I uint32_t RESERVED8[3];

	union {
		__IO uint8_t ANALOG_COMPCTRL;

		struct {
			__IO uint8_t COMP0ENABLE : 1;
			__IO uint8_t RESV_1 : 1;
			__IO uint8_t CONF0LCK : 1;
			__IO uint8_t RESV_2 : 1;
			__IO uint8_t COMP1ENABLE : 1;
		} ANALOG_COMPCTRL_b;
	};
	__I uint8_t RESERVED9[3];

	union {
		__IO uint8_t ANLG_COM_SLEEPCTRL;

		struct {
			__IO uint8_t COMP0SLEEP_EN : 1;
			__IO uint8_t COMP1SLEEP_EN : 1;
		} ANLG_COM_SLEEPCTRL_b;
	};
};
/* End of section using anonymous unions */

/* UART0_INST LINE_STS: DATA_READY (Bitfield-Mask: 0x01) */
#define UART0_STS_DATA_RDY_Msk (0x1UL)
/* EC_REG_BANK_INST GPIO_BANK_PWR: VTR_LEVEL2 (Bit 1) */
#define EC_REG_BANK_INST_GPIO_BANK_PWR_VTR_LVL2_Pos (1UL)

/* Peripheral memory map */
#define PCR_INST_BASE 0x40080100UL
#define DMA_MAIN_INST_BASE 0x40002400UL
#define DMA_CHAN00_INST_BASE 0x40002440UL

#define UART0_INST_BASE 0x400F2400UL
#define GPIO_000_036_INST_BASE 0x40081000UL

#define QMSPI_INST_BASE 0x40070000UL
#define VBAT_INST_BASE 0x4000A400UL
#define EC_REG_BANK_INST_BASE 0x4000FC00UL

/* Peripheral declaration */
#define PCR_INST ((struct PCR_INST_Type *)PCR_INST_BASE)
#define DMA_MAIN_INST ((struct DMA_MAIN_INST_Type *)DMA_MAIN_INST_BASE)
#define DMA_CHAN00_INST ((struct DMA_CHAN00_INST_Type *)DMA_CHAN00_INST_BASE)
#define UART0_INST ((struct UART0_INST_Type *)UART0_INST_BASE)

/* Note - use octal notation (leading 0) for GPIO number */
#define GPIO_PIN_CONTROL1_ADDR(gpio_num) \
	(GPIO_000_036_INST_BASE + (gpio_num * 4))

#define QMSPI_INST ((struct QMSPI_INST_Type *)QMSPI_INST_BASE)
#define VBAT_INST ((struct VBAT_INST_Type *)VBAT_INST_BASE)
#define EC_REG_BANK_INST ((struct EC_REG_BANK_INST_Type *)EC_REG_BANK_INST_BASE)

#ifdef __cplusplus
}
#endif
#endif
