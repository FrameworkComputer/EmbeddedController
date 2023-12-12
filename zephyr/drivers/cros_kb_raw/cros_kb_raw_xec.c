/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT microchip_xec_cros_kb_raw

#include "ec_tasks.h"
#include "keyboard_raw.h"
#include "task.h"

#include <assert.h>

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/interrupt_controller/intc_mchp_xec_ecia.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_kb_raw.h>
#include <soc.h>
#include <soc/microchip_xec/reg_def_cros.h>
LOG_MODULE_REGISTER(cros_kb_raw, LOG_LEVEL_ERR);

#ifdef CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED
#if !DT_NODE_EXISTS(KBD_KSO2_NODE)
#error gpio_kbd_kso2 alias has to point to the keyboard column 2 output pin.
#endif
#endif /* CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED */

#define XEC_MAX_KEY_COLS 18 /* Maximum rows of keyboard matrix */
#define XEC_MAX_KEY_ROWS 8 /* Maximum columns of keyboard matrix */
#define XEC_KB_ROW_MASK (BIT(XEC_MAX_KEY_ROWS) - 1)

/* Device config */
struct cros_kb_raw_xec_config {
	/* keyboard scan controller base address */
	uintptr_t base;
	/* Keyboard scan input (KSI) wake-up irq */
	int irq;
	const struct pinctrl_dev_config *pcfg;
};

static int kb_raw_xec_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	/* Clock default is on */
	return 0;
}

/* Clear keyboard source bits: hw status, block source and NVIC pending */
static void kb_raw_xec_clr_src(const struct device *dev)
{
	struct cros_kb_raw_xec_config const *cfg = dev->config;
	struct kscan_regs *const inst = (struct kscan_regs *)cfg->base;

	inst->KSI_STS = 0xff;
	mchp_soc_ecia_girq_src_clr(MCHP_GIRQ21_ID, MCHP_KEYSCAN_GIRQ_POS);
	mchp_xec_ecia_nvic_clr_pend(MCHP_KEYSCAN_GIRQ_NVIC_DIRECT);
}

/* Cros ec keyboard raw api functions */
static int cros_kb_raw_xec_enable_interrupt(const struct device *dev,
					    int enable)
{
	struct cros_kb_raw_xec_config const *cfg = dev->config;

	if (enable) {
		kb_raw_xec_clr_src(dev);
		/* Enable Kscan NVIC interrupt */
		irq_enable(cfg->irq);
	} else {
		/* Disable Kscan NVIC interrupt */
		irq_disable(cfg->irq);
	}

	return 0;
}

static int cros_kb_raw_xec_read_row(const struct device *dev)
{
	struct cros_kb_raw_xec_config const *cfg = dev->config;
	struct kscan_regs *const inst = (struct kscan_regs *)cfg->base;
	int val;

	val = inst->KSI_IN;
	LOG_DBG("rows raw %02x", val);

	/* 1 means key pressed, otherwise means key released. */
	return (~val & 0xFF);
}

static int cros_kb_raw_xec_drive_column(const struct device *dev, int col)
{
	struct cros_kb_raw_xec_config const *cfg = dev->config;
	struct kscan_regs *const inst = (struct kscan_regs *)cfg->base;

	/*
	 * Selected lines are driven low, not selected one are set to high
	 * impedance. COL2 is set to logical 1 one selected, the actual value
	 * depends on how the corresponding GPIO is defined.
	 */
	if (col == KEYBOARD_COLUMN_NONE) {
		inst->KSO_SEL = MCHP_KSCAN_KSO_EN;
		cros_kb_raw_set_col2(0);
	} else if (col == KEYBOARD_COLUMN_ALL) {
		mchp_soc_ecia_girq_src_dis(MCHP_GIRQ21_ID,
					   MCHP_KEYSCAN_GIRQ_POS);
		inst->KSO_SEL = MCHP_KSCAN_KSO_ALL;
		cros_kb_raw_set_col2(1);

		/*
		 * Fix glitches on KSIs pins as all KSOs are driven low
		 * As keyboard is enabled, either in POR initialization
		 * or after previous key's break scan code sent to host,
		 * EC will drive all KSOs low for ready next key press
		 * detection. KSIs may have glitches based on differnt
		 * hardward design, this source bit checking and clean
		 * can prevent one faulty interrupt occurring, thought
		 * this faulty interrupt is harmless.
		 */
		if (inst->KSI_IN != 0xff) {
			kb_raw_xec_clr_src(dev);
		}
		mchp_soc_ecia_girq_src_en(MCHP_GIRQ21_ID,
					  MCHP_KEYSCAN_GIRQ_POS);
	} else if (IS_ENABLED(CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED)) {
		if (col == 2) {
			inst->KSO_SEL = MCHP_KSCAN_KSO_EN;
			cros_kb_raw_set_col2(1);
		} else {
			inst->KSO_SEL = col + CONFIG_KEYBOARD_KSO_BASE;
			cros_kb_raw_set_col2(0);
		}
	} else {
		inst->KSO_SEL = col + CONFIG_KEYBOARD_KSO_BASE;
	}

	return 0;
}

static void cros_kb_raw_xec_ksi_isr(const struct device *dev)
{
	kb_raw_xec_clr_src(dev);
	/* Wake-up keyboard scan task */
	task_wake(TASK_ID_KEYSCAN);
}

static int cros_kb_raw_xec_init(const struct device *dev)
{
	struct cros_kb_raw_xec_config const *cfg = dev->config;
	struct kscan_regs *const inst = (struct kscan_regs *)cfg->base;

	/* Use zephyr pinctrl to initialize pins */
	int ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);

	if (ret)
		return ret;

	/* Set up Kscan IRQ and ISR */
	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority),
		    cros_kb_raw_xec_ksi_isr, DEVICE_DT_INST_GET(0), 0);

	/* Disable Kscan NVIC and source interrupts */
	irq_disable(cfg->irq);
	mchp_soc_ecia_girq_src_dis(MCHP_GIRQ21_ID, MCHP_KEYSCAN_GIRQ_POS);
	kb_raw_xec_clr_src(dev);
	/* Enable all Kscan KSIs interrupt */
	inst->KSI_IEN = 0xff;
	/* Enable Kscan source interrupt */
	mchp_soc_ecia_girq_src_en(MCHP_GIRQ21_ID, MCHP_KEYSCAN_GIRQ_POS);

	return 0;
}

static const struct cros_kb_raw_driver_api cros_kb_raw_xec_driver_api = {
	.init = cros_kb_raw_xec_init,
	.drive_colum = cros_kb_raw_xec_drive_column,
	.read_rows = cros_kb_raw_xec_read_row,
	.enable_interrupt = cros_kb_raw_xec_enable_interrupt,
};

/* instantiate zephyr pinctrl constant info */
PINCTRL_DT_INST_DEFINE(0);

static const struct cros_kb_raw_xec_config cros_kb_raw_cfg = {
	.base = DT_INST_REG_ADDR(0),
	.irq = DT_INST_IRQN(0),
	.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(0),
};

/* Verify there's exactly one enabled cros,kb-raw-xec node. */
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1);
DEVICE_DT_INST_DEFINE(0, kb_raw_xec_init, NULL, NULL, &cros_kb_raw_cfg,
		      PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		      &cros_kb_raw_xec_driver_api);
