/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT ite_it8xxx2_cros_kb_raw

#include "ec_tasks.h"
#include "keyboard_raw.h"
#include "task.h"

#include <assert.h>

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/interrupt_controller/wuc_ite_it8xxx2.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/dt-bindings/interrupt-controller/it8xxx2-wuc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_kb_raw.h>
#include <soc.h>
#include <soc/ite_it8xxx2/reg_def_cros.h>
#include <soc_dt.h>
LOG_MODULE_REGISTER(cros_kb_raw, LOG_LEVEL_ERR);

#if !defined(CONFIG_SOC_IT8XXX2_REG_SET_V1) && \
	defined(CONFIG_PLATFORM_EC_KEYBOARD_COL2_INVERTED)
#if !DT_NODE_EXISTS(KBD_KSO2_NODE)
#error gpio_kbd_kso2 alias has to point to the keyboard column 2 output pin.
#endif
#endif

#define KEYBOARD_KSI_PIN_COUNT IT8XXX2_DT_INST_WUCCTRL_LEN(0)
#define KSOH_PIN_MASK (((1 << (KEYBOARD_COLS_MAX - 8)) - 1) & 0xff)
#define KSOH2_PIN_MASK GENMASK(1, 0)

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 44

/* Device config */
struct cros_kb_raw_wuc_map_cfg {
	/* WUC control device structure */
	const struct device *wucs;
	/* WUC pin mask */
	uint8_t mask;
};

struct cros_kb_raw_ite_config {
	/* keyboard scan controller base address */
	struct kscan_it8xxx2_regs *base;
	/* Keyboard scan input (KSI) wake-up irq */
	int irq;
	/* KSI[7:0] wake-up input source configuration list */
	const struct cros_kb_raw_wuc_map_cfg *wuc_map_list;
	/* KSI/KSO keyboard scan alternate configuration */
	const struct pinctrl_dev_config *pcfg;
};

struct cros_kb_raw_ite_data {
	/* KSI[7:0] wake-up interrupt status mask */
	uint8_t ksi_pin_mask;
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
	struct cros_kb_raw_ite_data *data = dev->data;

	if (enable) {
		/*
		 * W/C wakeup interrupt status of KSI[7:0] pins
		 *
		 * NOTE: We want to clear the status as soon as possible,
		 *       so clear KSI[7:0] pins at a time.
		 */
		it8xxx2_wuc_clear_status(config->wuc_map_list[0].wucs,
					 data->ksi_pin_mask);
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
	struct kscan_it8xxx2_regs *const inst = config->base;

	/* Bits are active-low, so invert returned levels */
	return ((inst->KBS_KSI) ^ 0xff);
}

#ifdef CONFIG_SOC_IT8XXX2_REG_SET_V1
static void kb_raw_ite_drive_column_reg_set_v1(const struct device *dev)
{
	const struct cros_kb_raw_ite_config *config = dev->config;
	struct kscan_it8xxx2_regs *const inst = config->base;
	unsigned int key;

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
	/* KSO[17:16] pins output low */
	inst->KBS_KSOH2 &= ~KSOH2_PIN_MASK;
}
#endif

static int cros_kb_raw_ite_drive_column(const struct device *dev, int col)
{
	int mask;
	unsigned int key;
	const struct cros_kb_raw_ite_config *config = dev->config;
	struct kscan_it8xxx2_regs *const inst = config->base;

	/* Tri-state all outputs */
	if (col == KEYBOARD_COLUMN_NONE)
		mask = 0x3ffff;
	/* Assert all outputs */
	else if (col == KEYBOARD_COLUMN_ALL)
		mask = 0;
	/* Assert a single output */
	else
		mask = 0x3ffff ^ BIT(col);
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
	/* Set KSO[17:16] output data */
	inst->KBS_KSOH2 = ((inst->KBS_KSOH2) & ~KSOH2_PIN_MASK) |
			  ((mask >> 16) & KSOH2_PIN_MASK);

	return 0;
}

#ifdef CONFIG_PLATFORM_EC_KEYBOARD_FACTORY_TEST
static int cros_kb_raw_ite_config_alt(const struct device *dev, bool enable)
{
	const struct cros_kb_raw_ite_config *config = dev->config;
	int status = 0;

	if (enable) {
		/* Set KSI/KSO pins of cros_kb_raw node to kbs mode */
		status = pinctrl_apply_state(config->pcfg,
					     PINCTRL_STATE_DEFAULT);
		if (status < 0) {
			LOG_ERR("Failed to enable KSI and KSO kbs mode");
			return status;
		}
	} else {
		/* Set KSI/KSO pins of cros_kb_raw node to gpio mode */
		status = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_SLEEP);
		if (status < 0) {
			LOG_ERR("Failed to enable KSI and KSO gpio mode");
			return status;
		}
	}

	return 0;
}
#endif

static void cros_kb_raw_ite_ksi_isr(const struct device *dev)
{
	const struct cros_kb_raw_ite_config *config = dev->config;
	struct cros_kb_raw_ite_data *data = dev->data;

	/*
	 * We clear IT8XXX2_IRQ_WKINTC irq status in
	 * ite_intc_irq_handler(), after interrupt was fired.
	 */
	/*
	 * W/C wakeup interrupt status of KSI[7:0] pins
	 *
	 * NOTE: We want to clear the status as soon as possible,
	 *       so clear KSI[7:0] pins at a time.
	 */
	it8xxx2_wuc_clear_status(config->wuc_map_list[0].wucs,
				 data->ksi_pin_mask);

	/* Wake-up keyboard scan task */
	task_wake(TASK_ID_KEYSCAN);
}

static int cros_kb_raw_ite_init(const struct device *dev)
{
	const struct cros_kb_raw_ite_config *config = dev->config;
	struct cros_kb_raw_ite_data *data = dev->data;
	int status;

	/* Ensure top-level interrupt is disabled */
	cros_kb_raw_ite_enable_interrupt(dev, 0);

	/*
	 * Enable the internal pull-up and kbs mode of the KSI[7:0] pins.
	 * Enable the internal pull-up and kbs mode of the KSO[15:0] pins.
	 * Enable the open-drain mode of the KSO[17:0] pins.
	 */
	status = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (status < 0) {
		LOG_ERR("Failed to configure KSI[7:0] and KSO[15:0] pins");
		return status;
	}

#ifdef CONFIG_SOC_IT8XXX2_REG_SET_V1
	kb_raw_ite_drive_column_reg_set_v1(dev);
#else
	cros_kb_raw_ite_drive_column(dev, KEYBOARD_COLUMN_ALL);
#endif

	for (int i = 0; i < KEYBOARD_KSI_PIN_COUNT; i++) {
		/* Select wakeup interrupt falling-edge triggered of KSI[7:0] */
		it8xxx2_wuc_set_polarity(config->wuc_map_list[i].wucs,
					 config->wuc_map_list[i].mask,
					 WUC_TYPE_EDGE_FALLING);
		/* W/C wakeup interrupt status of KSI[7:0] pins */
		it8xxx2_wuc_clear_status(config->wuc_map_list[i].wucs,
					 config->wuc_map_list[i].mask);
		/* Enable wakeup interrupt of KSI[7:0] pins */
		it8xxx2_wuc_enable(config->wuc_map_list[i].wucs,
				   config->wuc_map_list[i].mask);

		/*
		 * We want to clear KSI[7:0] pins status at a time when wakeup
		 * interrupt fire, so gather the KSI[7:0] pin mask value here.
		 */
		if (IS_ENABLED(CONFIG_LOG)) {
			if (config->wuc_map_list[i].wucs !=
			    config->wuc_map_list[0].wucs) {
				LOG_ERR("KSI%d isn't in the same wuc node!", i);
			}
		}
		data->ksi_pin_mask |= config->wuc_map_list[i].mask;
	}

	/* W/C interrupt status of KSI[7:0] pins */
	ite_intc_isr_clear(config->irq);

	irq_connect_dynamic(config->irq, 0,
			    (void (*)(const void *))cros_kb_raw_ite_ksi_isr,
			    (const void *)dev, 0);

	return 0;
}

static const struct cros_kb_raw_driver_api cros_kb_raw_ite_driver_api = {
	.init = cros_kb_raw_ite_init,
	.drive_colum = cros_kb_raw_ite_drive_column,
	.read_rows = cros_kb_raw_ite_read_row,
	.enable_interrupt = cros_kb_raw_ite_enable_interrupt,
#ifdef CONFIG_PLATFORM_EC_KEYBOARD_FACTORY_TEST
	.config_alt = cros_kb_raw_ite_config_alt,
#endif
};
static const struct cros_kb_raw_wuc_map_cfg
	cros_kb_raw_wuc_0[IT8XXX2_DT_INST_WUCCTRL_LEN(0)] =
		IT8XXX2_DT_WUC_ITEMS_LIST(0);

PINCTRL_DT_INST_DEFINE(0);

static const struct cros_kb_raw_ite_config cros_kb_raw_cfg = {
	.base = (struct kscan_it8xxx2_regs *)DT_INST_REG_ADDR(0),
	.irq = DT_INST_IRQN(0),
	.wuc_map_list = cros_kb_raw_wuc_0,
	.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(0),
};

static struct cros_kb_raw_ite_data cros_kb_raw_data;

DEVICE_DT_INST_DEFINE(0, kb_raw_ite_init, NULL, &cros_kb_raw_data,
		      &cros_kb_raw_cfg, PRE_KERNEL_1,
		      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		      &cros_kb_raw_ite_driver_api);
