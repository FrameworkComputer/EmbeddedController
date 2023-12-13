/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * IT83xx chip-specific part of the IRQ handling.
 */

#include "common.h"
#include "irq_chip.h"
#include "registers.h"
#include "util.h"

#define IRQ_GROUP(n, cpu_ints...)                                            \
	{                                                                    \
		(uint32_t) & CONCAT2(IT83XX_INTC_ISR, n) - IT83XX_INTC_BASE, \
			(uint32_t) & CONCAT2(IT83XX_INTC_IER, n) -           \
					     IT83XX_INTC_BASE,               \
			##cpu_ints                                           \
	}

static const struct {
	uint8_t isr_off;
	uint8_t ier_off;
	uint8_t cpu_int[8];
} irq_groups[] = {
	IRQ_GROUP(0, { -1, 2, 5, 4, 6, 2, 2, 4 }),
	IRQ_GROUP(1, { 7, 6, 6, 5, 2, 2, 2, 8 }),
	IRQ_GROUP(2, { 6, 2, 8, 8, 8, 2, 12, 12 }),
	IRQ_GROUP(3, { 5, 4, 4, 4, 11, 11, 3, 2 }),
	IRQ_GROUP(4, { 11, 11, 11, 11, 8, 9, 9, 9 }),
	IRQ_GROUP(5, { 2, 2, 2, 2, 2, 2, 2, 2 }),
	IRQ_GROUP(6, { 2, 2, 2, 2, 2, 2, 2, 2 }),
	IRQ_GROUP(7, { 10, 10, 3, 12, 3, 3, 3, 3 }),
	IRQ_GROUP(8, { 4, 4, 4, 4, 4, 4, -1, 12 }),
	IRQ_GROUP(9, { 2, 2, 2, 2, 2, 2, 2, 2 }),
	IRQ_GROUP(10, { 3, 6, 12, 12, 5, 2, 2, 2 }),
	IRQ_GROUP(11, { 2, 2, 2, 2, 2, 2, 2, 2 }),
	IRQ_GROUP(12, { 2, 2, 2, 2, 2, 2, 2, 2 }),
	IRQ_GROUP(13, { 2, 2, 2, 2, 2, 2, 2, 2 }),
	IRQ_GROUP(14, { 2, 2, 2, 2, 2, 2, 2, 2 }),
	IRQ_GROUP(15, { 2, 2, 2, 2, 2, 2, 2, 2 }),
	IRQ_GROUP(16, { 2, 2, 2, 2, 2, 2, 2, 2 }),
	IRQ_GROUP(17, { 2, 2, 2, 2, 2, 2, 2, 2 }),
	IRQ_GROUP(18, { 2, 2, 2, 2, -1, 4, 4, 7 }),
	IRQ_GROUP(19, { 6, 6, 12, 3, 3, 3, 3, 3 }),
	IRQ_GROUP(20, { 12, 12, 12, 12, 12, 12, 12, -1 }),
#if defined(IT83XX_INTC_GROUP_21_22_SUPPORT)
	IRQ_GROUP(21, { 2, 2, 2, 2, 2, 2, 2, 2 }),
	IRQ_GROUP(22, { 2, 2, -1, -1, -1, -1, -1, -1 }),
#elif defined(CHIP_FAMILY_IT8XXX1) || defined(CHIP_FAMILY_IT8XXX2)
	IRQ_GROUP(21, { -1, -1, 12, 12, 12, 12, 12, 12 }),
	IRQ_GROUP(22, { 2, 2, 2, 2, 2, 2, 2, 2 }),
#else
	IRQ_GROUP(21, { -1, -1, -1, -1, -1, -1, -1, -1 }),
	IRQ_GROUP(22, { -1, -1, -1, -1, -1, -1, -1, -1 }),
#endif
	IRQ_GROUP(23, { 2, 2, -1, -1, -1, -1, -1, 2 }),
	IRQ_GROUP(24, { 2, 2, 2, 2, 2, 2, -1, 2 }),
	IRQ_GROUP(25, { 2, 2, 2, 2, -1, -1, -1, -1 }),
	IRQ_GROUP(26, { 2, 2, 2, 2, 2, 2, 2, -1 }),
	IRQ_GROUP(27, { 2, 2, 2, 2, 2, 2, -1, -1 }),
	IRQ_GROUP(28, { 2, 2, 2, 2, 2, 2, -1, -1 }),
};

#if defined(CHIP_FAMILY_IT8320) /* N8 core */
/* Number of CPU hardware interrupts (HW0 ~ HW15) */
int cpu_int_entry_number;
#endif

int chip_get_ec_int(void)
{
	extern volatile int ec_int;

#if defined(CHIP_FAMILY_IT8320) /* N8 core */
	int i;

	for (i = 0; i < IT83XX_IRQ_COUNT; i++) {
		ec_int = IT83XX_INTC_IVCT(cpu_int_entry_number);
		/*
		 * WORKAROUND: when the interrupt vector register isn't
		 * latched in a load operation,
		 * we read it again to make sure the value we got
		 * is the correct value.
		 */
		if (ec_int == IT83XX_INTC_IVCT(cpu_int_entry_number))
			break;
	}
	/* Determine interrupt number */
	ec_int -= 16;
#else /* defined(CHIP_FAMILY_IT8XXX2) RISCV core */
	/* wait until two equal interrupt values are read */
	do {
		ec_int = IT83XX_INTC_AIVCT;
	} while (ec_int != IT83XX_INTC_AIVCT);
	ec_int -= 0x10;
	/* Unsupported EC INT number. */
	if (chip_get_intc_group(ec_int) >= 16)
		return -1;
#endif
	return ec_int;
}

int chip_get_intc_group(int irq)
{
	return irq_groups[irq / 8].cpu_int[irq % 8];
}

void chip_enable_irq(int irq)
{
	int group = irq / 8;
	int bit = irq % 8;

	/* SOC's interrupts share CPU machine-mode external interrupt */
	if (IS_ENABLED(CHIP_CORE_RISCV))
		IT83XX_INTC_REG(irq_groups[group].ier_off) |= BIT(bit);

	/* SOC's interrupts use CPU HW interrupt 2 ~ 15 */
	if (IS_ENABLED(CHIP_CORE_NDS32))
		IT83XX_INTC_REG(IT83XX_INTC_EXT_IER_OFF(group)) |= BIT(bit);
}

void chip_disable_irq(int irq)
{
	int group = irq / 8;
	int bit = irq % 8;

	/* SOC's interrupts share CPU machine-mode external interrupt */
	if (IS_ENABLED(CHIP_CORE_RISCV)) {
		volatile uint8_t _ier __unused;

		IT83XX_INTC_REG(irq_groups[group].ier_off) &= ~BIT(bit);
		/*
		 * This load operation will guarantee the above modification of
		 * EC's register can be seen by any following instructions.
		 */
		_ier = IT83XX_INTC_REG(irq_groups[group].ier_off);
	}

	/* SOC's interrupts use CPU HW interrupt 2 ~ 15 */
	if (IS_ENABLED(CHIP_CORE_NDS32)) {
		volatile uint8_t _ext_ier __unused;

		IT83XX_INTC_REG(IT83XX_INTC_EXT_IER_OFF(group)) &= ~BIT(bit);
		/*
		 * This load operation will guarantee the above modification of
		 * EC's register can be seen by any following instructions.
		 */
		_ext_ier = IT83XX_INTC_REG(IT83XX_INTC_EXT_IER_OFF(group));
	}
}

void chip_clear_pending_irq(int irq)
{
	int group = irq / 8;
	int bit = irq % 8;

	/* always write 1 clear, no | */
	IT83XX_INTC_REG(irq_groups[group].isr_off) = BIT(bit);
}

int chip_trigger_irq(int irq)
{
	int group = irq / 8;
	int bit = irq % 8;

	return irq_groups[group].cpu_int[bit];
}

void chip_init_irqs(void)
{
	int i;

	/* Clear all IERx and EXT_IERx */
	for (i = 0; i < ARRAY_SIZE(irq_groups); i++) {
		IT83XX_INTC_REG(irq_groups[i].ier_off) = 0;
		if (IS_ENABLED(CHIP_CORE_NDS32))
			IT83XX_INTC_REG(IT83XX_INTC_EXT_IER_OFF(i)) = 0;
	}
}
