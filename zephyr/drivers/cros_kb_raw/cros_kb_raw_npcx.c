/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT nuvoton_npcx_cros_kb_raw

#include <assert.h>
#include <dt-bindings/clock/npcx_clock.h>
#include <drivers/cros_kb_raw.h>
#include <drivers/clock_control.h>
#include <drivers/gpio.h>
#include <kernel.h>
#include <soc.h>
#include <soc/nuvoton_npcx/reg_def_cros.h>

#include "ec_tasks.h"
#include "keyboard_raw.h"
#include "soc_miwu.h"
#include "task.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(cros_kb_raw, LOG_LEVEL_ERR);

#define NPCX_MAX_KEY_COLS 18 /* Maximum rows of keyboard matrix */
#define NPCX_MAX_KEY_ROWS 8 /* Maximum columns of keyboard matrix */
#define NPCX_KB_ROW_MASK (BIT(NPCX_MAX_KEY_ROWS) - 1)

/* Device config */
struct cros_kb_raw_npcx_config {
	/* keyboard scan controller base address */
	uintptr_t base;
	/* clock configuration */
	struct npcx_clk_cfg clk_cfg;
	/* pinmux configuration */
	const uint8_t alts_size;
	const struct npcx_alt *alts_list;
	/* Keyboard scan input (KSI) wake-up irq */
	int irq;
	/* Size of keyboard inputs-wui mapping array */
	int wui_size;
	/* Mapping table between keyboard inputs and wui */
	struct npcx_wui wui_maps[];
};

/* Driver convenience defines */
#define DRV_CONFIG(dev) ((const struct cros_kb_raw_npcx_config *)(dev)->config)
#define HAL_INSTANCE(dev) (struct kbs_reg *)(DRV_CONFIG(dev)->base)

/* Keyboard Scan local functions */
static struct miwu_dev_callback ksi_callback[NPCX_MAX_KEY_ROWS];

static void kb_raw_npcx_init_ksi_wui_callback(
	const struct device *dev, struct miwu_dev_callback *callback,
	const struct npcx_wui *wui, miwu_dev_callback_handler_t handler)
{
	/* KSI signal which has no wake-up input source */
	if (wui->table == NPCX_MIWU_TABLE_NONE)
		return;

	/* Install callback function */
	npcx_miwu_init_dev_callback(callback, wui, handler, dev);
	npcx_miwu_manage_dev_callback(callback, 1);

	/* Configure MIWU setting and enable its interrupt */
	npcx_miwu_interrupt_configure(wui, NPCX_MIWU_MODE_EDGE,
				      NPCX_MIWU_TRIG_BOTH);
	npcx_miwu_irq_enable(wui);
}

static int kb_raw_npcx_init(const struct device *dev)
{
	const struct cros_kb_raw_npcx_config *const config = DRV_CONFIG(dev);
	const struct device *clk_dev = DEVICE_DT_GET(NPCX_CLK_CTRL_NODE);
	int ret;

	/* Turn on device clock first and get source clock freq. */
	ret = clock_control_on(clk_dev,
			       (clock_control_subsys_t *)&config->clk_cfg);
	if (ret < 0) {
		LOG_ERR("Turn on KSCAN clock fail %d", ret);
		return ret;
	}

	return 0;
}

/* Cros ec keyboard raw api functions */
static int cros_kb_raw_npcx_enable_interrupt(const struct device *dev,
					     int enable)
{
	const struct cros_kb_raw_npcx_config *const config = DRV_CONFIG(dev);

	if (enable)
		irq_enable(config->irq);
	else
		irq_disable(config->irq);

	return 0;
}

static int cros_kb_raw_npcx_read_row(const struct device *dev)
{
	struct kbs_reg *const inst = HAL_INSTANCE(dev);
	int val;

	val = inst->KBSIN;
	LOG_DBG("rows raw %02x", val);

	/* 1 means key pressed, otherwise means key released. */
	return (~val & NPCX_KB_ROW_MASK);
}

static int cros_kb_raw_npcx_drive_column(const struct device *dev, int col)
{
	struct kbs_reg *const inst = HAL_INSTANCE(dev);

	/*
	 * Nuvoton 'Keyboard Scan' module supports 18x8 matrix
	 * It also support automatic scan functionality.
	 */
	uint32_t mask, col_out;

	/* Add support for CONFIG_KEYBOARD_KSO_BASE shifting */
	col_out = col + CONFIG_KEYBOARD_KSO_BASE;

	/* Drive all lines to high. ie. Key detection is disabled. */
	if (col == KEYBOARD_COLUMN_NONE) {
		mask = ~0;
		if (IS_ENABLED(CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED)) {
			gpio_set_level(GPIO_KBD_KSO2, 0);
		}
	}
	/* Drive all lines to low for detection any key press */
	else if (col == KEYBOARD_COLUMN_ALL) {
		mask = ~(BIT(keyboard_cols) - 1);
		if (IS_ENABLED(CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED)) {
			gpio_set_level(GPIO_KBD_KSO2, 1);
		}
	}
	/* Drive one line to low for determining which key's state changed. */
	else {
		if (IS_ENABLED(CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED)) {
			if (col == 2)
				gpio_set_level(GPIO_KBD_KSO2, 1);
			else
				gpio_set_level(GPIO_KBD_KSO2, 0);
		}
		mask = ~BIT(col_out);
	}

	/* Set KBSOUT */
	inst->KBSOUT0 = (mask & 0xFFFF);
	inst->KBSOUT1 = ((mask >> 16) & 0x03);

	return 0;
}

static void cros_kb_raw_npcx_ksi_isr(const struct device *dev,
				     struct npcx_wui *wui)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(wui);

	LOG_DBG("%s: KSI%d is changed", __func__, wui->bit);
	/* Wake-up keyboard scan task */
	task_wake(TASK_ID_KEYSCAN);
}

static int cros_kb_raw_npcx_init(const struct device *dev)
{
	const struct cros_kb_raw_npcx_config *const config = DRV_CONFIG(dev);
	struct kbs_reg *const inst = HAL_INSTANCE(dev);

	/* Pull-up KBSIN0-7 internally */
	inst->KBSINPU = 0xFF;

	/*
	 * Keyboard Scan Control Register
	 *
	 * [6:7] - KBHDRV KBSOUTn signals output buffers are open-drain.
	 * [3] - KBSINC   Auto-increment of Buffer Data register is disabled
	 * [2] - KBSIEN   Interrupt of Auto-Scan is disabled
	 * [1] - KBSMODE  Key detection mechanism is implemented by firmware
	 * [0] - START    Write 0 to this field is not affected
	 */
	inst->KBSCTL = 0x00;

	/*
	 * Select quasi-bidirectional buffers for KSO pins. It reduces the
	 * low-to-high transition time. This feature only supports in npcx7.
	 */
	if (IS_ENABLED(CONFIG_CROS_KB_RAW_NPCX_KSO_HIGH_DRIVE)) {
		SET_FIELD(inst->KBSCTL, NPCX_KBSCTL_KBHDRV_FIELD, 0x01);
	}

	/* Configure pin-mux for kscan device */
	npcx_pinctrl_mux_configure(config->alts_list, config->alts_size, 1);

	/* Drive all column lines to low for detection any key press */
	cros_kb_raw_npcx_drive_column(dev, KEYBOARD_COLUMN_ALL);

	/* Configure wake-up input and callback for keyboard input signal */
	for (int i = 0; i < ARRAY_SIZE(ksi_callback); i++)
		kb_raw_npcx_init_ksi_wui_callback(dev, &ksi_callback[i],
						  &config->wui_maps[i],
						  cros_kb_raw_npcx_ksi_isr);

	return 0;
}

static const struct cros_kb_raw_driver_api cros_kb_raw_npcx_driver_api = {
	.init = cros_kb_raw_npcx_init,
	.drive_colum = cros_kb_raw_npcx_drive_column,
	.read_rows = cros_kb_raw_npcx_read_row,
	.enable_interrupt = cros_kb_raw_npcx_enable_interrupt,
};

static const struct npcx_alt cros_kb_raw_alts[] = NPCX_DT_ALT_ITEMS_LIST(0);

static const struct cros_kb_raw_npcx_config cros_kb_raw_cfg = {
	.base = DT_INST_REG_ADDR(0),
	.alts_size = ARRAY_SIZE(cros_kb_raw_alts),
	.alts_list = cros_kb_raw_alts,
	.clk_cfg = NPCX_DT_CLK_CFG_ITEM(0),
	.irq = DT_INST_IRQN(0),
	.wui_size = NPCX_DT_WUI_ITEMS_LEN(0),
	.wui_maps = NPCX_DT_WUI_ITEMS_LIST(0),
};

/* Verify there's exactly 1 enabled cros,kb-raw-npcx node. */
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1);
DEVICE_DT_INST_DEFINE(0, kb_raw_npcx_init, NULL, NULL, &cros_kb_raw_cfg,
		      PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		      &cros_kb_raw_npcx_driver_api);

/* KBS register structure check */
NPCX_REG_SIZE_CHECK(kbs_reg, 0x010);
NPCX_REG_OFFSET_CHECK(kbs_reg, KBSIN, 0x004);
NPCX_REG_OFFSET_CHECK(kbs_reg, KBSOUT0, 0x006);
NPCX_REG_OFFSET_CHECK(kbs_reg, KBS_BUF_INDX, 0x00a);
