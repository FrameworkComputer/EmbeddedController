/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT microchip_xec_cros_kb_raw

#include <assert.h>
#include <drivers/cros_kb_raw.h>
#include <drivers/clock_control.h>
#include <drivers/gpio.h>
#include <drivers/pinctrl.h>
#include <kernel.h>
#include <soc.h>
#include <soc/microchip_xec/reg_def_cros.h>

#include "ec_tasks.h"
#include "keyboard_raw.h"
#include "task.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(cros_kb_raw, LOG_LEVEL_ERR);

#ifdef CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED
#if !DT_NODE_EXISTS(KBD_KS02_NODE)
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

/* Driver convenience defines */
#define KB_RAW_XEC_CONFIG(dev)						\
	((struct cros_kb_raw_xec_config const *)(dev)->config)

static int kb_raw_xec_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	/* Clock default is on */
	return 0;
}

/* Cros ec keyboard raw api functions */
static int cros_kb_raw_xec_enable_interrupt(const struct device *dev,
					    int enable)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(enable);

	/* TODO(b/216111514): need to implement code per kscan hardware */

	return 0;
}

static int cros_kb_raw_xec_read_row(const struct device *dev)
{
	struct kscan_regs *const inst =		\
		(struct kscan_regs *)(KB_RAW_XEC_CONFIG(dev)->base);
	int val;

	val = inst->KSI_IN;
	LOG_DBG("rows raw %02x", val);

	/* 1 means key pressed, otherwise means key released. */
	return ~(val & 0xFF);
}

static int cros_kb_raw_xec_drive_column(const struct device *dev, int col)
{
	struct kscan_regs *const inst =		\
		(struct kscan_regs *)(KB_RAW_XEC_CONFIG(dev)->base);

	/* Drive all lines to high. i.e. Key detection is disabled. */
	if (col == KEYBOARD_COLUMN_NONE) {
		inst->KSO_SEL = MCHP_KSCAN_KSO_EN;
		cros_kb_raw_set_col2(0);
	}
	/* Drive all lines to low for detection any key press */
	else if (col == KEYBOARD_COLUMN_ALL) {
		inst->KSO_SEL = MCHP_KSCAN_KSO_ALL;
		cros_kb_raw_set_col2(1);
	}
	/* Drive one line to low for determining which key's state changed. */
	else if (IS_ENABLED(CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED)) {
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
	ARG_UNUSED(dev);

	/* TODO(b/216111514): need to implement code per kscan hardware */
}

/* TODO(b/216111514): need to implement code per kscan hardware */
static int cros_kb_raw_xec_init(const struct device *dev)
{
	struct cros_kb_raw_xec_config const *cfg = KB_RAW_XEC_CONFIG(dev);

	/* Use zephyr pinctrl to initialize pins */
	int ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);

	if (ret)
		return ret;

	IRQ_CONNECT(DT_INST_IRQN(0), 0, cros_kb_raw_xec_ksi_isr, NULL, 0);

	return 0;
}

static const struct cros_kb_raw_driver_api cros_kb_raw_xec_driver_api = {
	.init = cros_kb_raw_xec_init,
	.drive_colum = cros_kb_raw_xec_drive_column,
	.read_rows = cros_kb_raw_xec_read_row,
	.enable_interrupt = cros_kb_raw_xec_enable_interrupt,
};

/* instantiate zephyr pinctrl constant info */
PINCTRL_DT_INST_DEFINE(0)

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
