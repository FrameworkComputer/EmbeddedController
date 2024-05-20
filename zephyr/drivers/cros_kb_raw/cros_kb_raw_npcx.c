/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#define DT_DRV_COMPAT nuvoton_npcx_cros_kb_raw

#include "ec_tasks.h"
#include "keyboard_raw.h"
#include "soc_miwu.h"
#include "task.h"

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 24

#include <assert.h>

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/dt-bindings/clock/npcx_clock.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_kb_raw.h>
#include <soc.h>
#include <soc/nuvoton_npcx/reg_def_cros.h>
LOG_MODULE_REGISTER(cros_kb_raw, LOG_LEVEL_ERR);

#ifdef CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED
#if !DT_NODE_EXISTS(KBD_KSO2_NODE)
#error gpio_kbd_kso2 alias has to point to the keyboard column 2 output pin.
#endif
#endif /* CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED */

#define NPCX_MAX_KEY_COLS 18 /* Maximum rows of keyboard matrix */
#define NPCX_MAX_KEY_ROWS 8 /* Maximum columns of keyboard matrix */
#define NPCX_KB_ROW_MASK (BIT(NPCX_MAX_KEY_ROWS) - 1)

/* Device config */
struct cros_kb_raw_npcx_config {
	/* keyboard scan controller base address */
	uintptr_t base;
	/* clock configuration */
	struct npcx_clk_cfg clk_cfg;
	/* Pin control configuration */
	const struct pinctrl_dev_config *pcfg;
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
static struct miwu_callback ksi_callback[NPCX_MAX_KEY_ROWS];

static void kb_raw_npcx_init_ksi_wui_callback(
	const struct device *dev, struct miwu_callback *callback,
	const struct npcx_wui *wui, miwu_dev_callback_handler_t handler)
{
	/* KSI signal which has no wake-up input source */
	if (wui->table == NPCX_MIWU_TABLE_NONE)
		return;

	/* Install callback function */
	npcx_miwu_init_dev_callback(callback, wui, handler, dev);
	npcx_miwu_manage_callback(callback, 1);

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

	/* Nuvoton 'Keyboard Scan' module supports 18x8 matrix. */
	uint32_t mask, col_out;

	/* Add support for CONFIG_KEYBOARD_KSO_BASE shifting */
	col_out = col + CONFIG_KEYBOARD_KSO_BASE;

	/*
	 * Selected lines are set to 0 (i.e. drive low), not selected one are
	 * set to 1 (high impedance). COL2 is set to logical 1 one selected,
	 * the actual value depends on how the corresponding GPIO is defined.
	 */
	if (col == KEYBOARD_COLUMN_NONE) {
		mask = ~0;
		cros_kb_raw_set_col2(0);
	} else if (col == KEYBOARD_COLUMN_ALL) {
		mask = ~(BIT(keyboard_cols) - 1);
		cros_kb_raw_set_col2(1);
	} else {
		if (col == 2) {
			cros_kb_raw_set_col2(1);
		} else {
			cros_kb_raw_set_col2(0);
		}
		mask = ~BIT(col_out);
	}

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
	int ret;

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

	/* Configure pin control for kscan device */
	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("KB Raw pinctrl setup failed (%d)", ret);
		return ret;
	}

	/* Drive all column lines to low for detection any key press */
	cros_kb_raw_npcx_drive_column(dev, KEYBOARD_COLUMN_ALL);

	/* Configure wake-up input and callback for keyboard input signal */
	for (int i = 0; i < ARRAY_SIZE(ksi_callback); i++)
		kb_raw_npcx_init_ksi_wui_callback(dev, &ksi_callback[i],
						  &config->wui_maps[i],
						  cros_kb_raw_npcx_ksi_isr);

	return 0;
}

#ifdef CONFIG_PLATFORM_EC_KEYBOARD_FACTORY_TEST
static int cros_kb_raw_npcx_config_alt(const struct device *dev, bool enable)
{
	const struct cros_kb_raw_npcx_config *const config = DRV_CONFIG(dev);
	uint8_t id = enable ? PINCTRL_STATE_DEFAULT : PINCTRL_STATE_SLEEP;

	return pinctrl_apply_state(config->pcfg, id);
}
#endif

static const struct cros_kb_raw_driver_api cros_kb_raw_npcx_driver_api = {
	.init = cros_kb_raw_npcx_init,
	.drive_colum = cros_kb_raw_npcx_drive_column,
	.read_rows = cros_kb_raw_npcx_read_row,
	.enable_interrupt = cros_kb_raw_npcx_enable_interrupt,
#ifdef CONFIG_PLATFORM_EC_KEYBOARD_FACTORY_TEST
	.config_alt = cros_kb_raw_npcx_config_alt,
#endif
};

PINCTRL_DT_INST_DEFINE(0);

static const struct cros_kb_raw_npcx_config cros_kb_raw_cfg = {
	.base = DT_INST_REG_ADDR(0),
	.clk_cfg = NPCX_DT_CLK_CFG_ITEM(0),
	.irq = DT_INST_IRQN(0),
	.wui_size = NPCX_DT_WUI_ITEMS_LEN(0),
	.wui_maps = NPCX_DT_WUI_ITEMS_LIST(0),
	.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(0),
};

/* Verify there's exactly 1 enabled cros,kb-raw-npcx node. */
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1);
DEVICE_DT_INST_DEFINE(0, kb_raw_npcx_init, NULL, NULL, &cros_kb_raw_cfg,
		      PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		      &cros_kb_raw_npcx_driver_api);

BUILD_ASSERT(
	!IS_ENABLED(CONFIG_INPUT_NPCX_KBD),
	"cros_kb_raw_npcx can't be enabled at the same time as input_npcx_kbd");

/* KBS register structure check */
NPCX_REG_SIZE_CHECK(kbs_reg, 0x010);
NPCX_REG_OFFSET_CHECK(kbs_reg, KBSIN, 0x004);
NPCX_REG_OFFSET_CHECK(kbs_reg, KBSOUT0, 0x006);
NPCX_REG_OFFSET_CHECK(kbs_reg, KBS_BUF_INDX, 0x00a);
