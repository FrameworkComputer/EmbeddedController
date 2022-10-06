/*
 * Copyright 2021 Google LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nuvoton_npcx_cros_mtc

#include <assert.h>
#include <drivers/cros_rtc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <soc.h>
#include <soc/nuvoton_npcx/reg_def_cros.h>

#include "ec_tasks.h"
#include "soc_miwu.h"
#include "task.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(cros_rtc, LOG_LEVEL_ERR);

#define NPCX_MTC_TTC_LOAD_DELAY_US 250 /* Delay after writing TTC */
#define NPCX_MTC_ALARM_MASK GENMASK(24, 0) /* Valid field of alarm in WTC */

/* Driver config */
struct cros_rtc_npcx_config {
	/* Monotonic counter base address */
	uintptr_t base;
	/* Monotonic counter wake-up input source configuration */
	const struct npcx_wui mtc_alarm;
};

/* Driver data */
struct cros_rtc_npcx_data {
	/* Monotonic counter wake-up callback object */
	struct miwu_dev_callback miwu_mtc_cb;
	cros_rtc_alarm_callback_t alarm_callback;
};

/* Driver convenience defines */
#define DRV_CONFIG(dev) ((const struct cros_rtc_npcx_config *)(dev)->config)

#define DRV_DATA(dev) ((struct cros_rtc_npcx_data *)(dev)->data)

#define HAL_INSTANCE(dev) (struct mtc_reg *)(DRV_CONFIG(dev)->base)

/* Counter internal local functions */
static uint32_t counter_npcx_get_val(const struct device *dev)
{
	struct mtc_reg *const inst = HAL_INSTANCE(dev);

	/*
	 * Get value of monotonic counter which keeps counting when VCC1 power
	 * domain exists (Unit:sec)
	 */
	return inst->TTC;
}

static void counter_npcx_set_val(const struct device *dev, uint32_t val)
{
	struct mtc_reg *const inst = HAL_INSTANCE(dev);

	/*
	 * Set monotonic counter. Write it twice to ensure the value latch to
	 * TTC register. A delay (~250 us) is also needed before writing again.
	 */
	inst->TTC = val;
	k_busy_wait(NPCX_MTC_TTC_LOAD_DELAY_US);

	inst->TTC = val;
	k_busy_wait(NPCX_MTC_TTC_LOAD_DELAY_US);
}

static uint32_t counter_npcx_get_alarm_val(const struct device *dev)
{
	struct mtc_reg *const inst = HAL_INSTANCE(dev);

	/*
	 * If alarm is not set or it is set and has already gone off, return
	 * zero directly.
	 */
	if (!IS_BIT_SET(inst->WTC, NPCX_WTC_WIE) ||
	    IS_BIT_SET(inst->WTC, NPCX_WTC_PTO)) {
		return 0;
	}

	/* Return 25-bit alarm value */
	return inst->WTC & NPCX_MTC_ALARM_MASK;
}

static void counter_npcx_set_alarm_val(const struct device *dev, uint32_t val)
{
	struct mtc_reg *const inst = HAL_INSTANCE(dev);

	/* Disable alarm interrupt */
	inst->WTC &= ~BIT(NPCX_WTC_WIE);

	/* Set new alarm value */
	inst->WTC = val & NPCX_MTC_ALARM_MASK;

	/* Enable alarm interrupt */
	inst->WTC |= BIT(NPCX_WTC_WIE);
}

static void counter_npcx_reset_alarm(const struct device *dev)
{
	struct mtc_reg *const inst = HAL_INSTANCE(dev);

	/* Disable alarm interrupt first */
	if (IS_BIT_SET(inst->WTC, NPCX_WTC_WIE)) {
		inst->WTC &= ~BIT(NPCX_WTC_WIE);
	}

	/* Set alarm to maximum value and clear its pending bit */
	if (IS_BIT_SET(inst->WTC, NPCX_WTC_PTO)) {
		inst->WTC = NPCX_MTC_ALARM_MASK;
		inst->WTC |= BIT(NPCX_WTC_PTO);
	}
}

/* Counter local functions */
static void counter_npcx_isr(const struct device *dev, struct npcx_wui *wui)
{
	struct cros_rtc_npcx_data *data = DRV_DATA(dev);

	LOG_DBG("%s", __func__);

	/* Alarm is one-shot, so reset alarm to default */
	counter_npcx_reset_alarm(dev);

	/* Call callback function */
	if (data->alarm_callback) {
		data->alarm_callback(dev);
	}
}

/* cros ec RTC api functions */
static int cros_rtc_npcx_configure(const struct device *dev,
				   cros_rtc_alarm_callback_t callback)
{
	struct cros_rtc_npcx_data *data = DRV_DATA(dev);

	if (callback == NULL) {
		return -EINVAL;
	}

	data->alarm_callback = callback;
	return 0;
}

static int cros_rtc_npcx_get_value(const struct device *dev, uint32_t *value)
{
	*value = counter_npcx_get_val(dev);

	return 0;
}

static int cros_rtc_npcx_set_value(const struct device *dev, uint32_t value)
{
	counter_npcx_set_val(dev, value);

	return 0;
}
static int cros_rtc_npcx_get_alarm(const struct device *dev, uint32_t *seconds,
				   uint32_t *microseconds)
{
	*seconds = counter_npcx_get_alarm_val(dev);
	*microseconds = 0;

	return 0;
}
static int cros_rtc_npcx_set_alarm(const struct device *dev, uint32_t seconds,
				   uint32_t microseconds)
{
	const struct cros_rtc_npcx_config *config = DRV_CONFIG(dev);
	ARG_UNUSED(microseconds);

	/* Enable interrupt of the MTC alarm wake-up input source */
	npcx_miwu_irq_enable(&config->mtc_alarm);

	/* Make sure alarm restore to default state */
	counter_npcx_reset_alarm(dev);
	counter_npcx_set_alarm_val(dev, seconds);

	return 0;
}

static int cros_rtc_npcx_reset_alarm(const struct device *dev)
{
	const struct cros_rtc_npcx_config *config = DRV_CONFIG(dev);

	/* Disable interrupt of the MTC alarm wake-up input source */
	npcx_miwu_irq_disable(&config->mtc_alarm);

	counter_npcx_reset_alarm(dev);

	return 0;
}

/* cros ec RTC driver registration */
static const struct cros_rtc_driver_api cros_rtc_npcx_driver_api = {
	.configure = cros_rtc_npcx_configure,
	.get_value = cros_rtc_npcx_get_value,
	.set_value = cros_rtc_npcx_set_value,
	.get_alarm = cros_rtc_npcx_get_alarm,
	.set_alarm = cros_rtc_npcx_set_alarm,
	.reset_alarm = cros_rtc_npcx_reset_alarm,
};

static int cros_rtc_npcx_init(const struct device *dev)
{
	const struct cros_rtc_npcx_config *config = DRV_CONFIG(dev);
	struct cros_rtc_npcx_data *data = DRV_DATA(dev);

	/* Initialize the miwu input and its callback for monotonic counter */
	npcx_miwu_init_dev_callback(&data->miwu_mtc_cb, &config->mtc_alarm,
				    counter_npcx_isr, dev);
	npcx_miwu_manage_dev_callback(&data->miwu_mtc_cb, true);

	/*
	 * Configure the monotonic counter wake-up event triggered from a rising
	 * edge on its signal.
	 */
	npcx_miwu_interrupt_configure(&config->mtc_alarm, NPCX_MIWU_MODE_EDGE,
				      NPCX_MIWU_TRIG_HIGH);

	return 0;
}

static const struct cros_rtc_npcx_config cros_rtc_npcx_cfg_0 = {
	.base = DT_INST_REG_ADDR(0),
	.mtc_alarm = NPCX_DT_WUI_ITEM_BY_NAME(0, mtc_alarm)
};

static struct cros_rtc_npcx_data cros_rtc_npcx_data_0;

DEVICE_DT_INST_DEFINE(0, cros_rtc_npcx_init, /* pm_control_fn= */ NULL,
		      &cros_rtc_npcx_data_0, &cros_rtc_npcx_cfg_0, POST_KERNEL,
		      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		      &cros_rtc_npcx_driver_api);
