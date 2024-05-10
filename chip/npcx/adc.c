/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific ADC module for Chrome EC */

#include "adc.h"
#include "atomic.h"
#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

/* Maximum time we allow for an ADC conversion */
#define ADC_TIMEOUT_US SECOND
/*
 * ADC basic clock is from APB1.
 * In npcx5, APB1 clock frequency is (15 MHz / 4).
 * Configure ADC clock divider and speed parameters to set the ADC clock to
 * ~2 MHz.
 * In npcx7 and later chips, APB1 clock frequency is 15 MHz.
 * Configure ADC clock divider and speed parameters to set the ADC clock to
 * 7.5 MHz.
 */
#if defined(CHIP_FAMILY_NPCX5)
#define ADC_CLK 2000000
#define ADC_DLY 0x03
#define ADC_ADCCNF2 0x8B07
#define ADC_GENDLY 0x0100
#define ADC_MEAST 0x0001
#else
#define ADC_CLK 7500000
#define ADC_DLY 0x02
#define ADC_ADCCNF2 0x8901
#define ADC_GENDLY 0x0100
#define ADC_MEAST 0x0405
#endif

/* ADC conversion mode */
enum npcx_adc_conversion_mode {
	ADC_CHN_CONVERSION_MODE = 0,
	ADC_SCAN_CONVERSION_MODE = 1
};

/* Global variables */
static volatile task_id_t task_waiting;

struct mutex adc_lock;

static volatile bool adc_done;

/**
 * Preset ADC operation clock.
 *
 * @param   none
 * @return  none
 * @notes   changed when initial or HOOK_FREQ_CHANGE command
 */
void adc_freq_changed(void)
{
	uint8_t prescaler_divider = 0;

	/* Set clock prescaler divider to ADC module*/
	prescaler_divider = (uint8_t)(clock_get_apb1_freq() / ADC_CLK);
	if (prescaler_divider >= 1)
		prescaler_divider = prescaler_divider - 1;
	if (prescaler_divider > 0x3F)
		prescaler_divider = 0x3F;

	/* Set Core Clock Division Factor in order to obtain the ADC clock */
	SET_FIELD(NPCX_ATCTL, NPCX_ATCTL_SCLKDIV_FIELD, prescaler_divider);
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, adc_freq_changed, HOOK_PRIO_DEFAULT);

/**
 * Flush an ADC sequencer and initiate a read.
 *
 * @param   input_ch    operation channel
 * @param   timeout		preset timeout
 * @return  TRUE/FALSE  success/fail
 * @notes   set SW-triggered interrupt conversion and one-shot mode in npcx chip
 */
static int start_single_and_wait(enum npcx_adc_input_channel input_ch,
				 int timeout)
{
	int event;

	if (IS_ENABLED(CONFIG_KEYBOARD_SCAN_ADC)) {
		if (task_start_called())
			task_waiting = task_get_current();
	} else
		task_waiting = task_get_current();

	/* Stop ADC conversion first */
	SET_BIT(NPCX_ADCCNF, NPCX_ADCCNF_STOP);

	/* Set ADC conversion code to SW conversion mode */
	SET_FIELD(NPCX_ADCCNF, NPCX_ADCCNF_ADCMD_FIELD,
		  ADC_CHN_CONVERSION_MODE);

	/* Set conversion type to one-shot type */
	CLEAR_BIT(NPCX_ADCCNF, NPCX_ADCCNF_ADCRPTC);

	/* Update number of channel to be converted */
	SET_FIELD(NPCX_ASCADD, NPCX_ASCADD_SADDR_FIELD, input_ch);

	/* Clear End-of-Conversion Event status */
	SET_BIT(NPCX_ADCSTS, NPCX_ADCSTS_EOCEV);

	/* Enable ADC End-of-Conversion Interrupt if applicable */
	SET_BIT(NPCX_ADCCNF, NPCX_ADCCNF_INTECEN);

	/* Start conversion */
	SET_BIT(NPCX_ADCCNF, NPCX_ADCCNF_START);

	/*
	 * If tasks have started, we can suspend to the task that called us.
	 * If not, we need to busy poll for adc to finish before proceeding
	 */
	if (IS_ENABLED(CONFIG_KEYBOARD_SCAN_ADC)) {
		if (!task_start_called()) {
			/* Wait for the ADC interrupt to set the flag */
			do {
				crec_usleep(10);
			} while (adc_done == false);

			adc_done = false;

			event = TASK_EVENT_ADC_DONE;
		} else {
			/* Wait for interrupt */
			event = task_wait_event_mask(TASK_EVENT_ADC_DONE,
						     timeout);

			task_waiting = TASK_ID_INVALID;
		}
	} else {
		/* Wait for interrupt */
		event = task_wait_event_mask(TASK_EVENT_ADC_DONE, timeout);

		task_waiting = TASK_ID_INVALID;
	}

	return (event == TASK_EVENT_ADC_DONE);
}

static uint16_t repetitive_enabled;
void npcx_set_adc_repetitive(enum npcx_adc_input_channel input_ch, int enable)
{
	mutex_lock(&adc_lock);

	/* Stop ADC conversion */
	SET_BIT(NPCX_ADCCNF, NPCX_ADCCNF_STOP);

	if (enable) {
		/* Forbid EC enter deep sleep during conversion. */
		disable_sleep(SLEEP_MASK_ADC);
		/* Turn on ADC */
		SET_BIT(NPCX_ADCCNF, NPCX_ADCCNF_ADCEN);
		/* Set ADC conversion code to SW conversion mode */
		SET_FIELD(NPCX_ADCCNF, NPCX_ADCCNF_ADCMD_FIELD,
			  ADC_SCAN_CONVERSION_MODE);
		/* Update number of channel to be converted */
		SET_BIT(NPCX_ADCCS, input_ch);
		/* Set conversion type to repetitive (runs continuously) */
		SET_BIT(NPCX_ADCCNF, NPCX_ADCCNF_ADCRPTC);
		repetitive_enabled |= BIT(input_ch);

		/* Start conversion */
		SET_BIT(NPCX_ADCCNF, NPCX_ADCCNF_START);
	} else {
		CLEAR_BIT(NPCX_ADCCS, input_ch);
		repetitive_enabled &= ~BIT(input_ch);

		if (!repetitive_enabled) {
			/* Turn off ADC */
			CLEAR_BIT(NPCX_ADCCNF, NPCX_ADCCNF_ADCEN);
			/* Set ADC to one-shot mode */
			CLEAR_BIT(NPCX_ADCCNF, NPCX_ADCCNF_ADCRPTC);
			/* Allow ec enter deep sleep */
			enable_sleep(SLEEP_MASK_ADC);
		} else {
			/* Start conversion again */
			SET_BIT(NPCX_ADCCNF, NPCX_ADCCNF_START);
		}
	}

	mutex_unlock(&adc_lock);
}

/**
 * Return the ADC value from CHNDAT register directly.
 *
 * @param   input_ch    channel number
 * @return  ADC data
 */
int adc_read_data(enum npcx_adc_input_channel input_ch)
{
	const struct adc_t *adc = adc_channels + input_ch;
	int value;
	uint16_t chn_data;

	chn_data = NPCX_CHNDAT(adc->input_ch);
	value = GET_FIELD(chn_data, NPCX_CHNDAT_CHDAT_FIELD) * adc->factor_mul /
			adc->factor_div +
		adc->shift;
	return value;
}

/**
 * Start a single conversion and return the result
 *
 * @param   ch    operation channel
 * @return  ADC converted voltage or error message
 */
int adc_read_channel(enum adc_channel ch)
{
	const struct adc_t *adc = adc_channels + ch;
	int value;
	uint16_t chn_data;

	mutex_lock(&adc_lock);

	/* Forbid ec enter deep sleep during ADC conversion is proceeding. */
	disable_sleep(SLEEP_MASK_ADC);

	/* Turn on ADC */
	SET_BIT(NPCX_ADCCNF, NPCX_ADCCNF_ADCEN);

	if (start_single_and_wait(adc->input_ch, ADC_TIMEOUT_US)) {
		chn_data = NPCX_CHNDAT(adc->input_ch);
		if ((adc->input_ch ==
		     GET_FIELD(NPCX_ASCADD, NPCX_ASCADD_SADDR_FIELD)) &&
		    (IS_BIT_SET(chn_data, NPCX_CHNDAT_NEW))) {
			value = GET_FIELD(chn_data, NPCX_CHNDAT_CHDAT_FIELD) *
					adc->factor_mul / adc->factor_div +
				adc->shift;
		} else {
			value = ADC_READ_ERROR;
		}
	} else {
		value = ADC_READ_ERROR;
	}

	if (!repetitive_enabled) {
		/* Turn off ADC */
		CLEAR_BIT(NPCX_ADCCNF, NPCX_ADCCNF_ADCEN);
		/* Allow ec enter deep sleep */
		enable_sleep(SLEEP_MASK_ADC);
	} else {
		/* Set ADC conversion code to SW conversion mode */
		SET_FIELD(NPCX_ADCCNF, NPCX_ADCCNF_ADCMD_FIELD,
			  ADC_SCAN_CONVERSION_MODE);
		/* Set conversion type to repetitive (runs continuously) */
		SET_BIT(NPCX_ADCCNF, NPCX_ADCCNF_ADCRPTC);
		/* Start conversion */
		SET_BIT(NPCX_ADCCNF, NPCX_ADCCNF_START);
	}

	mutex_unlock(&adc_lock);

	return value;
}

/* Board should register these callbacks with npcx_adc_cfg_thresh_int(). */
static void (*adc_thresh_irqs[NPCX_ADC_THRESH_CNT])(void);

void npcx_adc_thresh_int_enable(int threshold_idx, int enable)
{
	uint16_t thrcts;

	enable = !!enable;

	if ((threshold_idx < 1) || (threshold_idx > NPCX_ADC_THRESH_CNT)) {
		CPRINTS("Invalid ADC thresh index! (%d)", threshold_idx);
		return;
	}
	threshold_idx--; /* convert to 0-based */

	/* avoid clearing other threshold status */
	thrcts = NPCX_THRCTS & ~GENMASK(NPCX_ADC_THRESH_CNT - 1, 0);

	if (enable) {
		/* clear threshold status */
		SET_BIT(thrcts, threshold_idx);
		/* set enable threshold status */
		SET_BIT(thrcts, NPCX_THRCTS_THR1_IEN + threshold_idx);
	} else {
		CLEAR_BIT(thrcts, NPCX_THRCTS_THR1_IEN + threshold_idx);
	}
	NPCX_THRCTS = thrcts;
}

void npcx_adc_register_thresh_irq(int threshold_idx,
				  const struct npcx_adc_thresh_t *thresh_cfg)
{
	int npcx_adc_ch;
	int raw_val;
	int mul;
	int div;
	int shift;

	if ((threshold_idx < 1) || (threshold_idx > NPCX_ADC_THRESH_CNT)) {
		CPRINTS("Invalid ADC thresh index! (%d)", threshold_idx);
		return;
	}
	npcx_adc_ch = adc_channels[thresh_cfg->adc_ch].input_ch;

	if (!thresh_cfg->adc_thresh_cb) {
		CPRINTS("No callback for ADC Threshold %d!", threshold_idx);
		return;
	}

	/* Fill in the table */
	adc_thresh_irqs[threshold_idx - 1] = thresh_cfg->adc_thresh_cb;

	/* Select the channel */
	SET_FIELD(NPCX_THRCTL(threshold_idx), NPCX_THRCTL_CHNSEL, npcx_adc_ch);

	if (thresh_cfg->lower_or_higher)
		SET_BIT(NPCX_THRCTL(threshold_idx), NPCX_THRCTL_L_H);
	else
		CLEAR_BIT(NPCX_THRCTL(threshold_idx), NPCX_THRCTL_L_H);

	/* Set the threshold value. */
	mul = adc_channels[thresh_cfg->adc_ch].factor_mul;
	div = adc_channels[thresh_cfg->adc_ch].factor_div;
	shift = adc_channels[thresh_cfg->adc_ch].shift;

	raw_val = (thresh_cfg->thresh_assert - shift) * div / mul;
	CPRINTS("ADC THR%d: Setting THRVAL = %d, L_H: %d", threshold_idx,
		raw_val, thresh_cfg->lower_or_higher);
	SET_FIELD(NPCX_THRCTL(threshold_idx), NPCX_THRCTL_THRVAL, raw_val);

#if NPCX_FAMILY_VERSION <= NPCX_FAMILY_NPCX7
	/* Disable deassertion threshold function */
	CLEAR_BIT(NPCX_THR_DCTL(threshold_idx), NPCX_THR_DCTL_THRD_EN);
#endif

	/* Enable threshold detection */
	SET_BIT(NPCX_THRCTL(threshold_idx), NPCX_THRCTL_THEN);
}

/**
 * ADC interrupt handler
 *
 * @param   none
 * @return  none
 * @notes   Only handle SW-triggered conversion in npcx chip
 */
static void adc_interrupt(void)
{
	int i;
	uint16_t thrcts;

	if (IS_BIT_SET(NPCX_ADCCNF, NPCX_ADCCNF_INTECEN) &&
	    IS_BIT_SET(NPCX_ADCSTS, NPCX_ADCSTS_EOCEV)) {
		/* Disable End-of-Conversion Interrupt */
		CLEAR_BIT(NPCX_ADCCNF, NPCX_ADCCNF_INTECEN);

		/* Stop conversion for single-shot mode */
		if (!repetitive_enabled)
			SET_BIT(NPCX_ADCCNF, NPCX_ADCCNF_STOP);

		/* Clear End-of-Conversion Event status */
		SET_BIT(NPCX_ADCSTS, NPCX_ADCSTS_EOCEV);

		/* Wake up the task which was waiting for the interrupt */
		if (task_waiting != TASK_ID_INVALID)
			task_set_event(task_waiting, TASK_EVENT_ADC_DONE);

		if (IS_ENABLED(CONFIG_KEYBOARD_SCAN_ADC)) {
			if (!task_start_called())
				adc_done = true;
		}
	}

	for (i = NPCX_THRCTS_THR1_STS; i < NPCX_ADC_THRESH_CNT; i++) {
		if (IS_BIT_SET(NPCX_THRCTS, NPCX_THRCTS_THR1_IEN + i) &&
		    IS_BIT_SET(NPCX_THRCTS, i)) {
			/* avoid clearing other threshold status */
			thrcts = NPCX_THRCTS &
				 ~GENMASK(NPCX_ADC_THRESH_CNT - 1, 0);
			/* Clear threshold status */
			SET_BIT(thrcts, i);
			NPCX_THRCTS = thrcts;
			if (adc_thresh_irqs[i])
				adc_thresh_irqs[i]();
		}
	}
}
DECLARE_IRQ(NPCX_IRQ_ADC, adc_interrupt, 4);

/*
 * For Antighost keyboard, we need to initialize adc from
 * main before keyboard_scan_init is called in order to
 * detect boot keys
 */

/**
 * ADC initial.
 *
 * @param none
 * @return none
 */
#ifndef CONFIG_KEYBOARD_SCAN_ADC
static void adc_init(void)
#else
void adc_init(void)
#endif
{
	/* Configure pins from GPIOs to ADCs */
	gpio_config_module(MODULE_ADC, 1);

	/* Enable ADC clock (bit4 mask = 0x10) */
	clock_enable_peripheral(CGC_OFFSET_ADC, CGC_ADC_MASK,
				CGC_MODE_RUN | CGC_MODE_SLEEP);

	/* Set Core Clock Division Factor in order to obtain the ADC clock */
	adc_freq_changed();

	/* Set regular speed */
	SET_FIELD(NPCX_ATCTL, NPCX_ATCTL_DLY_FIELD, ADC_DLY);

	/* Set the other ADC settings */
	NPCX_ADCCNF2 = ADC_ADCCNF2;
	NPCX_GENDLY = ADC_GENDLY;
	NPCX_MEAST = ADC_MEAST;

	task_waiting = TASK_ID_INVALID;

	/* Enable IRQs */
	task_enable_irq(NPCX_IRQ_ADC);
}
#ifndef CONFIG_KEYBOARD_SCAN_ADC
DECLARE_HOOK(HOOK_INIT, adc_init, HOOK_PRIO_INIT_ADC);
#endif
