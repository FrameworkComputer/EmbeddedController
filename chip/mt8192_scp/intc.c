/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* INTC control module */

#include "console.h"
#include "csr.h"
#include "registers.h"

/*
 * INTC_GRP_0 is reserved.  See swirq of syscall_handler() in
 * core/riscv-rv32i/task.c for more details.
 *
 * Lower group has higher priority.
 */
enum INTC_GROUP {
	INTC_GRP_0 = 0x0,
	INTC_GRP_1,
	INTC_GRP_2,
	INTC_GRP_3,
	INTC_GRP_4,
	INTC_GRP_5,
	INTC_GRP_6,
	INTC_GRP_7,
	INTC_GRP_8,
	INTC_GRP_9,
	INTC_GRP_10,
	INTC_GRP_11,
	INTC_GRP_12,
	INTC_GRP_13,
	INTC_GRP_14,
};

static struct {
	uint8_t group;
} irqs[SCP_INTC_IRQ_COUNT] = {
	/* 0 */
	[SCP_IRQ_GIPC_IN0]		= { INTC_GRP_7 },
	[SCP_IRQ_GIPC_IN1]		= { INTC_GRP_0 },
	[SCP_IRQ_GIPC_IN2]		= { INTC_GRP_0 },
	[SCP_IRQ_GIPC_IN3]		= { INTC_GRP_0 },
	/* 4 */
	[SCP_IRQ_SPM]			= { INTC_GRP_0 },
	[SCP_IRQ_AP_CIRQ]		= { INTC_GRP_0 },
	[SCP_IRQ_EINT]			= { INTC_GRP_0 },
	[SCP_IRQ_PMIC]			= { INTC_GRP_0 },
	/* 8 */
	[SCP_IRQ_UART0_TX]		= { INTC_GRP_12 },
	[SCP_IRQ_UART1_TX]		= { INTC_GRP_12 },
	[SCP_IRQ_I2C0]			= { INTC_GRP_0 },
	[SCP_IRQ_I2C1_0]		= { INTC_GRP_0 },
	/* 12 */
	[SCP_IRQ_BUS_DBG_TRACKER]	= { INTC_GRP_0 },
	[SCP_IRQ_CLK_CTRL]		= { INTC_GRP_0 },
	[SCP_IRQ_VOW]			= { INTC_GRP_0 },
	[SCP_IRQ_TIMER0]		= { INTC_GRP_6 },
	/* 16 */
	[SCP_IRQ_TIMER1]		= { INTC_GRP_6 },
	[SCP_IRQ_TIMER2]		= { INTC_GRP_6 },
	[SCP_IRQ_TIMER3]		= { INTC_GRP_6 },
	[SCP_IRQ_TIMER4]		= { INTC_GRP_6 },
	/* 20 */
	[SCP_IRQ_TIMER5]		= { INTC_GRP_6 },
	[SCP_IRQ_OS_TIMER]		= { INTC_GRP_0 },
	[SCP_IRQ_UART0_RX]		= { INTC_GRP_12 },
	[SCP_IRQ_UART1_RX]		= { INTC_GRP_12 },
	/* 24 */
	[SCP_IRQ_GDMA]			= { INTC_GRP_0 },
	[SCP_IRQ_AUDIO]			= { INTC_GRP_0 },
	[SCP_IRQ_MD_DSP]		= { INTC_GRP_0 },
	[SCP_IRQ_ADSP]			= { INTC_GRP_0 },
	/* 28 */
	[SCP_IRQ_CPU_TICK]		= { INTC_GRP_0 },
	[SCP_IRQ_SPI0]			= { INTC_GRP_0 },
	[SCP_IRQ_SPI1]			= { INTC_GRP_0 },
	[SCP_IRQ_SPI2]			= { INTC_GRP_0 },
	/* 32 */
	[SCP_IRQ_NEW_INFRA_SYS_CIRQ]	= { INTC_GRP_0 },
	[SCP_IRQ_DBG]			= { INTC_GRP_0 },
	[SCP_IRQ_CCIF0]			= { INTC_GRP_0 },
	[SCP_IRQ_CCIF1]			= { INTC_GRP_0 },
	/* 36 */
	[SCP_IRQ_CCIF2]			= { INTC_GRP_0 },
	[SCP_IRQ_WDT]			= { INTC_GRP_0 },
	[SCP_IRQ_USB0]			= { INTC_GRP_0 },
	[SCP_IRQ_USB1]			= { INTC_GRP_0 },
	/* 40 */
	[SCP_IRQ_DPMAIF]		= { INTC_GRP_0 },
	[SCP_IRQ_INFRA]			= { INTC_GRP_0 },
	[SCP_IRQ_CLK_CTRL_CORE]		= { INTC_GRP_0 },
	[SCP_IRQ_CLK_CTRL2_CORE]	= { INTC_GRP_0 },
	/* 44 */
	[SCP_IRQ_CLK_CTRL2]		= { INTC_GRP_0 },
	[SCP_IRQ_GIPC_IN4]		= { INTC_GRP_0 },
	[SCP_IRQ_PERIBUS_TIMEOUT]	= { INTC_GRP_0 },
	[SCP_IRQ_INFRABUS_TIMEOUT]	= { INTC_GRP_0 },
	/* 48 */
	[SCP_IRQ_MET0]			= { INTC_GRP_0 },
	[SCP_IRQ_MET1]			= { INTC_GRP_0 },
	[SCP_IRQ_MET2]			= { INTC_GRP_0 },
	[SCP_IRQ_MET3]			= { INTC_GRP_0 },
	/* 52 */
	[SCP_IRQ_AP_WDT]		= { INTC_GRP_0 },
	[SCP_IRQ_L2TCM_SEC_VIO]		= { INTC_GRP_0 },
	[SCP_IRQ_CPU_TICK1]		= { INTC_GRP_0 },
	[SCP_IRQ_MAD_DATAIN]		= { INTC_GRP_0 },
	/* 56 */
	[SCP_IRQ_I3C0_IBI_WAKE]		= { INTC_GRP_0 },
	[SCP_IRQ_I3C1_IBI_WAKE]		= { INTC_GRP_0 },
	[SCP_IRQ_I3C2_IBI_WAKE]		= { INTC_GRP_0 },
	[SCP_IRQ_APU_ENGINE]		= { INTC_GRP_0 },
	/* 60 */
	[SCP_IRQ_MBOX0]			= { INTC_GRP_0 },
	[SCP_IRQ_MBOX1]			= { INTC_GRP_0 },
	[SCP_IRQ_MBOX2]			= { INTC_GRP_0 },
	[SCP_IRQ_MBOX3]			= { INTC_GRP_0 },
	/* 64 */
	[SCP_IRQ_MBOX4]			= { INTC_GRP_0 },
	[SCP_IRQ_SYS_CLK_REQ]		= { INTC_GRP_0 },
	[SCP_IRQ_BUS_REQ]		= { INTC_GRP_0 },
	[SCP_IRQ_APSRC_REQ]		= { INTC_GRP_0 },
	/* 68 */
	[SCP_IRQ_APU_MBOX]		= { INTC_GRP_0 },
	[SCP_IRQ_DEVAPC_SECURE_VIO]	= { INTC_GRP_0 },
	/* 72 */
	/* 76 */
	[SCP_IRQ_I2C1_2]		= { INTC_GRP_0 },
	[SCP_IRQ_I2C2]			= { INTC_GRP_0 },
	/* 80 */
	[SCP_IRQ_AUD2AUDIODSP]		= { INTC_GRP_0 },
	[SCP_IRQ_AUD2AUDIODSP_2]	= { INTC_GRP_0 },
	[SCP_IRQ_CONN2ADSP_A2DPOL]	= { INTC_GRP_0 },
	[SCP_IRQ_CONN2ADSP_BTCVSD]	= { INTC_GRP_0 },
	/* 84 */
	[SCP_IRQ_CONN2ADSP_BLEISO]	= { INTC_GRP_0 },
	[SCP_IRQ_PCIE2ADSP]		= { INTC_GRP_0 },
	[SCP_IRQ_APU2ADSP_ENGINE]	= { INTC_GRP_0 },
	[SCP_IRQ_APU2ADSP_MBOX]		= { INTC_GRP_0 },
	/* 88 */
	[SCP_IRQ_CCIF3]			= { INTC_GRP_0 },
	[SCP_IRQ_I2C_DMA0]		= { INTC_GRP_0 },
	[SCP_IRQ_I2C_DMA1]		= { INTC_GRP_0 },
	[SCP_IRQ_I2C_DMA2]		= { INTC_GRP_0 },
	/* 92 */
	[SCP_IRQ_I2C_DMA3]		= { INTC_GRP_0 },
};
BUILD_ASSERT(ARRAY_SIZE(irqs) == SCP_INTC_IRQ_COUNT);

/*
 * Find current interrupt source.
 *
 * Lower group has higher priority.
 * Higher INT number has higher priority.
 */
int chip_get_ec_int(void)
{
	extern volatile int ec_int;
	unsigned int group, sta;
	int word;

	if (!SCP_CORE0_INTC_IRQ_OUT)
		goto error;

	group = read_csr(CSR_VIC_MICAUSE);

	for (word = SCP_INTC_GRP_LEN - 1; word >= 0; --word) {
		sta = SCP_CORE0_INTC_IRQ_GRP_STA(group, word);
		if (sta) {
			ec_int = __fls(sta) + word * 32;
			return ec_int;
		}
	}

error:
	/* unreachable, SCP crashes and dumps registers after returning */
	return -1;
}

int chip_get_intc_group(int irq)
{
	return irqs[irq].group;
}

void chip_enable_irq(int irq)
{
	unsigned int word, group, mask;

	word = SCP_INTC_WORD(irq);
	group = irqs[irq].group;
	mask = BIT(SCP_INTC_BIT(irq));

	/* disable interrupt */
	SCP_CORE0_INTC_IRQ_EN(word) &= ~mask;
	/* set group */
	SCP_CORE0_INTC_IRQ_GRP(group, word) |= mask;
	/* set as a wakeup source */
	SCP_CORE0_INTC_SLP_WAKE_EN(word) |= mask;
	/* enable interrupt */
	SCP_CORE0_INTC_IRQ_EN(word) |= mask;
}

void chip_disable_irq(int irq)
{
	/*
	 * Disabling INTC IRQ in runtime is unstable in MT8192 SCP.
	 * See b/163682416#comment17.
	 *
	 * Ideally, this function will be removed by LTO.
	 */
	ccprints("WARNING: %s is unsupported", __func__);
}

void chip_clear_pending_irq(int irq)
{
	unsigned int group = irqs[irq].group;

	/* must clear interrupt source before writing this */
	write_csr(CSR_VIC_MIEMS, group);
}

int chip_trigger_irq(int irq)
{
	extern volatile int ec_int;

	ec_int = irq;
	return irqs[irq].group;
}

void chip_init_irqs(void)
{
	unsigned int word, group;

	/* INTC init */
	/* clear enable and wakeup settings */
	for (word = 0; word < SCP_INTC_GRP_LEN; ++word) {
		SCP_CORE0_INTC_IRQ_EN(word) = 0x0;
		SCP_CORE0_INTC_SLP_WAKE_EN(word) = 0x0;

		/* clear group settings */
		for (group = 0; group < SCP_INTC_GRP_COUNT; ++group)
			SCP_CORE0_INTC_IRQ_GRP(group, word) = 0x0;
	}
	/* reset to default polarity */
	SCP_CORE0_INTC_IRQ_POL(0) = SCP_INTC_IRQ_POL0;
	SCP_CORE0_INTC_IRQ_POL(1) = SCP_INTC_IRQ_POL1;
	SCP_CORE0_INTC_IRQ_POL(2) = SCP_INTC_IRQ_POL2;

	/* GVIC init */
	/* enable all groups as interrupt sources */
	write_csr(CSR_VIC_MIMASK_G0, 0xffffffff);
	/* use level trigger */
	write_csr(CSR_VIC_MILSEL_G0, 0xffffffff);
	/* enable all groups as wakeup sources */
	write_csr(CSR_VIC_MIWAKEUP_G0, 0xffffffff);

	/* enable GVIC */
	set_csr(CSR_MCTREN, CSR_MCTREN_VIC);
}
