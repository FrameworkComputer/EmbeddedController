/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_INTC_H
#define __CROS_EC_INTC_H

/* INTC */
#define SCP_INTC_IRQ_POL0		0xef001f20
#define SCP_INTC_IRQ_POL1		0x0800001d
#define SCP_INTC_IRQ_POL2		0x00000020
#define SCP_INTC_GRP_LEN		3
#define SCP_INTC_IRQ_COUNT		96

/* IRQ numbers */
#define SCP_IRQ_GIPC_IN0		0
#define SCP_IRQ_GIPC_IN1		1
#define SCP_IRQ_GIPC_IN2		2
#define SCP_IRQ_GIPC_IN3		3
/* 4 */
#define SCP_IRQ_SPM			4
#define SCP_IRQ_AP_CIRQ			5
#define SCP_IRQ_EINT			6
#define SCP_IRQ_PMIC			7
/* 8 */
#define SCP_IRQ_UART0_TX		8
#define SCP_IRQ_UART1_TX		9
#define SCP_IRQ_I2C0			10
#define SCP_IRQ_I2C1_0			11
/* 12 */
#define SCP_IRQ_BUS_DBG_TRACKER		12
#define SCP_IRQ_CLK_CTRL		13
#define SCP_IRQ_VOW			14
#define SCP_IRQ_TIMER0			15
/* 16 */
#define SCP_IRQ_TIMER1			16
#define SCP_IRQ_TIMER2			17
#define SCP_IRQ_TIMER3			18
#define SCP_IRQ_TIMER4			19
/* 20 */
#define SCP_IRQ_TIMER5			20
#define SCP_IRQ_OS_TIMER		21
#define SCP_IRQ_UART0_RX		22
#define SCP_IRQ_UART1_RX		23
/* 24 */
#define SCP_IRQ_GDMA			24
#define SCP_IRQ_AUDIO			25
#define SCP_IRQ_MD_DSP			26
#define SCP_IRQ_ADSP			27
/* 28 */
#define SCP_IRQ_CPU_TICK		28
#define SCP_IRQ_SPI0			29
#define SCP_IRQ_SPI1			30
#define SCP_IRQ_SPI2			31
/* 32 */
#define SCP_IRQ_NEW_INFRA_SYS_CIRQ	32
#define SCP_IRQ_DBG			33
#define SCP_IRQ_CCIF0			34
#define SCP_IRQ_CCIF1			35
/* 36 */
#define SCP_IRQ_CCIF2			36
#define SCP_IRQ_WDT			37
#define SCP_IRQ_USB0			38
#define SCP_IRQ_USB1			39
/* 40 */
#define SCP_IRQ_DPMAIF			40
#define SCP_IRQ_INFRA			41
#define SCP_IRQ_CLK_CTRL_CORE		42
#define SCP_IRQ_CLK_CTRL2_CORE		43
/* 44 */
#define SCP_IRQ_CLK_CTRL2		44
#define SCP_IRQ_GIPC_IN4		45 /* HALT */
#define SCP_IRQ_PERIBUS_TIMEOUT		46
#define SCP_IRQ_INFRABUS_TIMEOUT	47
/* 48 */
#define SCP_IRQ_MET0			48
#define SCP_IRQ_MET1			49
#define SCP_IRQ_MET2			50
#define SCP_IRQ_MET3			51
/* 52 */
#define SCP_IRQ_AP_WDT			52
#define SCP_IRQ_L2TCM_SEC_VIO		53
#define SCP_IRQ_CPU_TICK1		54
#define SCP_IRQ_MAD_DATAIN		55
/* 56 */
#define SCP_IRQ_I3C0_IBI_WAKE		56
#define SCP_IRQ_I3C1_IBI_WAKE		57
#define SCP_IRQ_I3C2_IBI_WAKE		58
#define SCP_IRQ_APU_ENGINE		59
/* 60 */
#define SCP_IRQ_MBOX0			60
#define SCP_IRQ_MBOX1			61
#define SCP_IRQ_MBOX2			62
#define SCP_IRQ_MBOX3			63
/* 64 */
#define SCP_IRQ_MBOX4			64
#define SCP_IRQ_SYS_CLK_REQ		65
#define SCP_IRQ_BUS_REQ			66
#define SCP_IRQ_APSRC_REQ		67
/* 68 */
#define SCP_IRQ_APU_MBOX		68
#define SCP_IRQ_DEVAPC_SECURE_VIO	69
/* 72 */
/* 76 */
#define SCP_IRQ_I2C1_2			78
#define SCP_IRQ_I2C2			79
/* 80 */
#define SCP_IRQ_AUD2AUDIODSP		80
#define SCP_IRQ_AUD2AUDIODSP_2		81
#define SCP_IRQ_CONN2ADSP_A2DPOL	82
#define SCP_IRQ_CONN2ADSP_BTCVSD	83
/* 84 */
#define SCP_IRQ_CONN2ADSP_BLEISO	84
#define SCP_IRQ_PCIE2ADSP		85
#define SCP_IRQ_APU2ADSP_ENGINE		86
#define SCP_IRQ_APU2ADSP_MBOX		87
/* 88 */
#define SCP_IRQ_CCIF3			88
#define SCP_IRQ_I2C_DMA0		89
#define SCP_IRQ_I2C_DMA1		90
#define SCP_IRQ_I2C_DMA2		91
/* 92 */
#define SCP_IRQ_I2C_DMA3		92

#endif /* __CROS_EC_INTC_H */
