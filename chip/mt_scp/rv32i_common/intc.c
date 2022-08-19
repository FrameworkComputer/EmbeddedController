/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* INTC control module */

#include "console.h"
#include "csr.h"
#include "intc_group.h"
#include "registers.h"

/*
 * Find current interrupt source.
 *
 * Lower group has higher priority.
 * Higher INT number has higher priority.
 */
#define EC_INT_MAGIC_NO_IRQ_OUT BIT(15)
#define EC_INT_MAGIC_NO_STA BIT(14)
int chip_get_ec_int(void)
{
	extern volatile int ec_int;
	unsigned int group, sta;
	int word;

	group = read_csr(CSR_VIC_MICAUSE);

	if (!SCP_CORE_INTC_IRQ_OUT) {
		ec_int = EC_INT_MAGIC_NO_IRQ_OUT | group;
		goto error;
	}

	for (word = SCP_INTC_GRP_LEN - 1; word >= 0; --word) {
		sta = SCP_CORE_INTC_IRQ_GRP_STA(group, word);
		if (sta) {
			ec_int = __fls(sta) + word * 32;
			return ec_int;
		}
	}

	ec_int = EC_INT_MAGIC_NO_STA | group;
error:
	/* unreachable, SCP crashes and dumps registers after returning */
	return -1;
}

int chip_get_intc_group(int irq)
{
	return intc_irq_group_get(irq);
}

void chip_enable_irq(int irq)
{
	unsigned int word, group, mask;

	word = SCP_INTC_WORD(irq);
	group = intc_irq_group_get(irq);
	mask = BIT(SCP_INTC_BIT(irq));

	/* disable interrupt */
	SCP_CORE_INTC_IRQ_EN(word) &= ~mask;
	/* set group */
	SCP_CORE_INTC_IRQ_GRP(group, word) |= mask;
	/* set as a wakeup source */
	SCP_CORE_INTC_SLP_WAKE_EN(word) |= mask;
	/* enable interrupt */
	SCP_CORE_INTC_IRQ_EN(word) |= mask;
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
	unsigned int group = intc_irq_group_get(irq);

	/* must clear interrupt source before writing this */
#ifdef CHIP_FAMILY_RV55
	write_csr(CSR_VIC_MILMS_G, group);
#else
	write_csr(CSR_VIC_MIEMS, group);
#endif
}

int chip_trigger_irq(int irq)
{
	extern volatile int ec_int;

	ec_int = irq;
	return intc_irq_group_get(irq);
}

void chip_init_irqs(void)
{
	unsigned int word, group;

	/* INTC init */
	/* clear enable and wakeup settings */
	for (word = 0; word < SCP_INTC_GRP_LEN; ++word) {
		SCP_CORE_INTC_IRQ_EN(word) = 0x0;
		SCP_CORE_INTC_SLP_WAKE_EN(word) = 0x0;

		/* clear group settings */
		for (group = 0; group < SCP_INTC_GRP_COUNT; ++group)
			SCP_CORE_INTC_IRQ_GRP(group, word) = 0x0;
	}
	/* reset to default polarity */
	SCP_CORE_INTC_IRQ_POL(0) = SCP_INTC_IRQ_POL0;
	SCP_CORE_INTC_IRQ_POL(1) = SCP_INTC_IRQ_POL1;
	SCP_CORE_INTC_IRQ_POL(2) = SCP_INTC_IRQ_POL2;
#if SCP_INTC_GRP_LEN > 3
	SCP_CORE_INTC_IRQ_POL(3) = SCP_INTC_IRQ_POL3;
#endif

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
