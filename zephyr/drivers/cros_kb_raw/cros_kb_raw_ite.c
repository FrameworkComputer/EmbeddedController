/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT ite_it8xxx2_cros_kb_raw

#include <assert.h>
#include <drivers/cros_kb_raw.h>
#include <drivers/clock_control.h>
#include <drivers/gpio.h>
#include <kernel.h>
#include <soc.h>
#include <soc/ite_it8xxx2/reg_def_cros.h>

#include "ec_tasks.h"
#include "keyboard_raw.h"
#include "task.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(cros_kb_raw, LOG_LEVEL_ERR);

#define KSOH_PIN_MASK (((1 << (KEYBOARD_COLS_MAX - 8)) - 1) & 0xff)

/* Device config */
struct cros_kb_raw_ite_config {
	/* keyboard scan controller base address */
	uintptr_t base;
	/* Keyboard scan input (KSI) wake-up irq */
	int irq;
};

static int kb_raw_ite_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	/* Clock default is on */
	return 0;
}

/* Cros ec keyboard raw api functions */
static int cros_kb_raw_ite_enable_interrupt(const struct device *dev,
					    int enable)
{
	const struct cros_kb_raw_ite_config *config = dev->config;

	if (enable) {
		ECREG(IT8XXX2_WUC_WUESR3) = 0xFF;
		ite_intc_isr_clear(config->irq);
		irq_enable(config->irq);
	} else {
		irq_disable(config->irq);
	}

	return 0;
}

static int cros_kb_raw_ite_read_row(const struct device *dev)
{
	const struct cros_kb_raw_ite_config *config = dev->config;
	struct kscan_it8xxx2_regs *const inst =
				(struct kscan_it8xxx2_regs *) config->base;

	/* Bits are active-low, so invert returned levels */
	return ((inst->KBS_KSI) ^ 0xff);
}

static int cros_kb_raw_ite_drive_column(const struct device *dev, int col)
{
	int mask;
	unsigned int key;
	const struct cros_kb_raw_ite_config *config = dev->config;
	struct kscan_it8xxx2_regs *const inst =
				(struct kscan_it8xxx2_regs *) config->base;

	/* Tri-state all outputs */
	if (col == KEYBOARD_COLUMN_NONE)
		mask = 0xffff;
	/* Assert all outputs */
	else if (col == KEYBOARD_COLUMN_ALL)
		mask = 0;
	/* Assert a single output */
	else
		mask = 0xffff ^ BIT(col);
#ifdef CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED
	/* KSO[2] is inverted. */
	mask ^= BIT(2);
#endif
	inst->KBS_KSOL = mask & 0xff;
	/* critical section with interrupts off */
	key = irq_lock();
	/*
	 * Because IT8XXX2_KBS_KSOH1 register is shared by keyboard scan
	 * out and GPIO output mode, so we don't drive all KSOH pins
	 * here (this depends on how many keyboard matrix output pin
	 * we are using).
	 */
	inst->KBS_KSOH1 = ((inst->KBS_KSOH1) & ~KSOH_PIN_MASK) |
		      ((mask >> 8) & KSOH_PIN_MASK);
	/* restore interrupts */
	irq_unlock(key);

	return 0;
}

static void cros_kb_raw_ite_ksi_isr(const struct device *dev)
{
	ARG_UNUSED(dev);

	/*
	 * We clear IT8XXX2_IRQ_WKINTC irq status in
	 * ite_intc_irq_handler(), after interrupt was fired.
	 */
	/* W/C wakeup interrupt status for KSI[0-7] */
	ECREG(IT8XXX2_WUC_WUESR3) = 0xFF;

	/* Wake-up keyboard scan task */
	task_wake(TASK_ID_KEYSCAN);
}

static int cros_kb_raw_ite_init(const struct device *dev)
{
	unsigned int key;
	const struct cros_kb_raw_ite_config *config = dev->config;
	struct kscan_it8xxx2_regs *const inst =
				(struct kscan_it8xxx2_regs *) config->base;

	/* Ensure top-level interrupt is disabled */
	cros_kb_raw_ite_enable_interrupt(dev, 0);

	/*
	 * bit2, Setting 1 enables the internal pull-up of the KSO[15:0] pins.
	 * To pull up KSO[17:16], set the GPCR registers of their
	 * corresponding GPIO ports.
	 * bit0, Setting 1 enables the open-drain mode of the KSO[17:0] pins.
	 */
	inst->KBS_KSOCTRL = (IT8XXX2_KBS_KSOPU | IT8XXX2_KBS_KSOOD);
	/* bit2, 1 enables the internal pull-up of the KSI[7:0] pins. */
	inst->KBS_KSICTRL = IT8XXX2_KBS_KSIPU;
#ifdef CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED
	/* KSO[2] output high, others output low. */
	inst->KBS_KSOL = BIT(2);
	/* Enable KSO2's push-pull */
	inst->KBS_KSOLGCTRL |= IT8XXX2_KBS_KSO2GCTRL;
	inst->KBS_KSOLGOEN |= IT8XXX2_KBS_KSO2GOEN;
#else
	/* KSO[7:0] pins output low. */
	inst->KBS_KSOL = 0x00;
#endif
	/* critical section with interrupts off */
	key = irq_lock();
	/*
	 * KSO[COLS_MAX:8] pins low.
	 * NOTE: KSO[15:8] pins can part be enabled for keyboard function and
	 *       rest be configured as GPIO output mode. In this case that we
	 *       disable the ISR in critical section to avoid race condition.
	 */
	inst->KBS_KSOH1 &= ~KSOH_PIN_MASK;
	/* restore interrupts */
	irq_unlock(key);
	/* Select falling-edge triggered of wakeup interrupt for KSI[0-7] */
	ECREG(IT8XXX2_WUC_WUEMR3) = 0xFF;
	/* W/C wakeup interrupt status for KSI[0-7] */
	ECREG(IT8XXX2_WUC_WUESR3) = 0xFF;
	ite_intc_isr_clear(config->irq);
	/* Enable wakeup interrupt for KSI[0-7] */
	ECREG(IT8XXX2_WUC_WUENR3) = 0xFF;

	IRQ_CONNECT(DT_INST_IRQN(0), 0, cros_kb_raw_ite_ksi_isr, NULL, 0);

	return 0;
}

static const struct cros_kb_raw_driver_api cros_kb_raw_ite_driver_api = {
	.init = cros_kb_raw_ite_init,
	.drive_colum = cros_kb_raw_ite_drive_column,
	.read_rows = cros_kb_raw_ite_read_row,
	.enable_interrupt = cros_kb_raw_ite_enable_interrupt,
};

static const struct cros_kb_raw_ite_config cros_kb_raw_cfg = {
	.base = DT_INST_REG_ADDR(0),
	.irq = DT_INST_IRQN(0),
};

DEVICE_DT_INST_DEFINE(0, kb_raw_ite_init, NULL, NULL, &cros_kb_raw_cfg,
		      PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		      &cros_kb_raw_ite_driver_api);
