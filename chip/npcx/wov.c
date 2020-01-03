/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific WOV module for Chrome EC */

#include "apm_chip.h"
#include "clock.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "wov_chip.h"

#ifndef NPCX_WOV_SUPPORT
#error "Do not enable CONFIG_AUDIO_CODEC_* if npcx ec doesn't support WOV !"
#endif

/* Console output macros */
#ifndef DEBUG_AUDIO_CODEC
#define CPUTS(...)
#define CPRINTS(...)
#else
#define CPUTS(outstr) cputs(CC_AUDIO_CODEC, outstr)
#define CPRINTS(format, args...) cprints(CC_AUDIO_CODEC, format, ## args)
#endif

/* WOV FIFO status. */
#define WOV_STATUS_OFFSET NPCX_WOV_STATUS_CFIFO_OIT
#define WOV_IS_CFIFO_INT_THRESHOLD(sts) \
	IS_BIT_SET(sts, (NPCX_WOV_STATUS_CFIFO_OIT - WOV_STATUS_OFFSET))
#define WOV_IS_CFIFO_WAKE_THRESHOLD(sts) \
	IS_BIT_SET(sts, (NPCX_WOV_STATUS_CFIFO_OWT - WOV_STATUS_OFFSET))
#define WOV_IS_CFIFO_OVERRUN(sts) \
	IS_BIT_SET(sts, (NPCX_WOV_STATUS_CFIFO_OVRN - WOV_STATUS_OFFSET))
#define WOV_IS_I2S_FIFO_OVERRUN(sts) \
	IS_BIT_SET(sts, (NPCX_WOV_STATUS_I2S_FIFO_OVRN - WOV_STATUS_OFFSET))
#define WOV_IS_I2S_FIFO_UNDERRUN(sts) \
	IS_BIT_SET(sts, (NPCX_WOV_STATUS_I2S_FIFO_UNDRN - WOV_STATUS_OFFSET))

/* Core FIFO threshold. */
#define WOV_SET_FIFO_WAKE_THRESHOLD(n) \
	SET_FIELD(NPCX_WOV_FIFO_CNT, NPCX_WOV_FIFO_CNT_FIFO_WTHRSH, n)

#define WOV_SET_FIFO_INT_THRESHOLD(n) \
	SET_FIELD(NPCX_WOV_FIFO_CNT, NPCX_WOV_FIFO_CNT_FIFO_ITHRSH, n)
#define WOV_GET_FIFO_INT_THRESHOLD \
	GET_FIELD(NPCX_WOV_FIFO_CNT, NPCX_WOV_FIFO_CNT_FIFO_ITHRSH)

#define WOV_PLL_IS_NOT_LOCK \
	(!IS_BIT_SET(NPCX_WOV_PLL_CNTL1, NPCX_WOV_PLL_CNTL1_PLL_LOCKI))

/* mask definitions that clear reserved fields for WOV registers.*/
#define WOV_CLK_CTRL_REG_RESERVED_MASK 0x037F7FFF

#define WOV_GET_FIFO_WAKE_THRESHOLD \
	GET_FIELD(NPCX_WOV_FIFO_CNT, NPCX_WOV_FIFO_CNT_FIFO_WTHRSH)

/* Wait time 4ms for FMUL2 enabled and for configuration tuning sequence. */
#define WOV_FMUL2_CLK_TUNING_DELAY_TIME (4 * 1000)

/* The size of RAM buffer to store the voice data */
#define VOICE_BUF_SIZE    16000

/* PLL setting options. */
struct wov_pll_set_options_val {
	uint8_t pll_indv;     /* Input Divider */
	uint16_t pll_fbdv;    /* Feedback Divider */
	uint8_t pll_otdv1;    /* Output devide 1. */
	uint8_t pll_otdv2;    /* Output devide 2. */
	uint32_t pll_ext_div; /* Index for the table pll_ext_div */
};

/* PLL External Divider Load Values. */
struct wov_pll_ext_div_val {
	uint8_t pll_ediv;    /* Required PLL external divider */
	uint8_t pll_ediv_dc; /* Required PLL external divider DC */
};

static const struct wov_pll_ext_div_val pll_ext_div[] = {
	{0x2F, 0x78}, /* 12 */
	{0x57, 0x7C}, /* 13 */
	{0x2B, 0x7C}, /* 14 */
	{0x55, 0x7E}, /* 15 */
	{0x2A, 0x7E}, /* 16 */
	{0x15, 0x7F}, /* 17 */
	{0x4A, 0x7F}, /* 18 */
	{0x65, 0x3F}, /* 19 */
	{0x32, 0x3F}, /* 20 */
	{0x19, 0x5F}, /* 21 */
	{0x4C, 0x5F}, /* 22 */
	{0x66, 0x2F}, /* 23 */
	{0x73, 0x2F}, /* 24 */
	{0x39, 0x57}, /* 25 */
	{0x5C, 0x57}, /* 26 */
	{0x6E, 0x2B}, /* 27 */
	{0x77, 0x2B}, /* 28 */
	{0x3B, 0x55}, /* 29 */
	{0x5D, 0x55}, /* 30 */
	{0x2E, 0x2A}, /* 31 */
	{0x17, 0x2A}, /* 32 */
	{0x4B, 0x15}, /* 33 */
	{0x25, 0x15}, /* 34 */
	{0x52, 0x4A}, /* 35 */
	{0x69, 0x4A}, /* 36 */
	{0x34, 0x65}, /* 37 */
	{0x1A, 0x65}, /* 38 */
	{0x0D, 0x32}, /* 39 */
	{0x46, 0x32}, /* 40 */
	{0x63, 0x19}, /* 41 */
	{0x31, 0x19}, /* 42 */
	{0x58, 0x4C}, /* 43 */
	{0x6C, 0x4C}, /* 44 */
	{0x76, 0x66}, /* 45 */
	{0x7B, 0x66}, /* 46 */
	{0x3D, 0x73}, /* 47 */
	{0x5E, 0x73}, /* 48 */
	{0x6F, 0x39}, /* 49 */
	{0x37, 0x39}, /* 50 */
	{0x5B, 0x5C}, /* 51 */
	{0x2D, 0x5C}, /* 52 */
	{0x56, 0x6E}, /* 53 */
	{0x6B, 0x6E}, /* 54 */
	{0x35, 0x77}, /* 55 */
	{0x5A, 0x77}, /* 56 */
	{0x6D, 0x3B}, /* 57 */
	{0x36, 0x3B}, /* 58 */
	{0x1B, 0x5D}, /* 59 */
	{0x4D, 0x5D}, /* 60 */
	{0x26, 0x2E}, /* 61 */
	{0x13, 0x2E}, /* 62 */
	{0x49, 0x17}, /* 63 */
	{0x24, 0x17}, /* 64 */
	{0x12, 0x4B}, /* 65 */
	{0x09, 0x4B}, /* 66 */
	{0x44, 0x25}  /* 67 */
};

/* WOV interrupts */
static const uint8_t wov_interupts[] = {
	0,  /* VAD_INTEN */
	1,  /* VAD_WKEN */
	8,  /* CFIFO_NE_IE */
	9,  /* CFIFO_OIT_IE */
	10, /* CFIFO_OWT_WE */
	11, /* CFIFO_OVRN_IE */
	12, /* I2S_FIFO_OVRN_IE */
	13  /* I2S_FIFO_UNDRN_IE */
};

struct wov_ppl_divider {
	uint16_t pll_frame_len; /* PLL frame length. */
	uint16_t pll_fbdv;      /* PLL feedback divider. */
	uint8_t pll_indv;       /* PLL Input Divider. */
	uint8_t pll_otdv1;      /* PLL Output Divider 1. */
	uint8_t pll_otdv2;      /* PLL Output Divider 2. */
	uint8_t pll_ediv;       /* PLL External Divide Factor. */
};

struct wov_cfifo_buf {
	uint32_t *buf; /* Pointer to a buffer. */
	int size;      /* Buffer size in words. */
};

struct wov_config wov_conf;

static struct wov_cfifo_buf cfifo_buf;
static wov_call_back_t callback_fun;

#define WOV_RATE_ERROR_THRESH_MSEC 10
#define WOV_RATE_ERROR_THRESH 5

static int irq_underrun_count;
static int irq_overrun_count;
static uint32_t wov_i2s_underrun_tstamp;
static uint32_t wov_i2s_overrun_tstamp;

#define WOV_CALLBACK(event)                  \
	{                                    \
		if (callback_fun != NULL)    \
			callback_fun(event); \
	}

#define CONFIG_WOV_FIFO_THRESH_WORDS WOV_FIFO_THRESHOLD_80_DATA_WORDS

/**
 * Reads data from the core fifo.
 *
 * @param   num_elements - Number of elements (Dword) to read.
 *
 * @return  None
 */
void wov_cfifo_read_handler_l(uint32_t num_elements)
{
	uint32_t index;

	for (index = 0; index < num_elements; index++)
		cfifo_buf.buf[index] = NPCX_WOV_FIFO_OUT;

	cfifo_buf.buf = &cfifo_buf.buf[index];
	cfifo_buf.size -= num_elements;
}

static enum ec_error_list wov_calc_pll_div_s(int32_t d_in,
					int32_t total_div, int32_t vco_freq,
					struct wov_ppl_divider *pll_div)
{
	int32_t	 d_1, d_2, d_e;

	/*
	 * Please see comments in wov_calc_pll_div_l function below.
	 */
	for (d_e = 4; d_e < 75; d_e++) {
		for (d_2 = 1; d_2 < 7; d_2++) {
			for (d_1 = 1; d_1 < 7; d_1++) {
				if ((vco_freq / (d_1 * d_2)) > 900)
					continue;

				if (total_div == (d_in * d_e * d_1 * d_2)) {
					pll_div->pll_indv  = d_in;
					pll_div->pll_otdv1 = d_1;
					pll_div->pll_otdv2 = d_2;
					pll_div->pll_ediv  = d_e;
					return EC_SUCCESS;
				}
			}
		}
	}
	return EC_ERROR_INVAL;
}

/**
 * Gets the PLL divider value accordingly to the i2S clock frequency.
 *
 * @param   i2s_clk_freq - i2S clock frequency
 * @param   sample_rate  - Sample rate in KHz (16KHz or 48KHz)
 * @param   pll_div      - PLL dividers.
 *
 * @return  None
 */
static enum ec_error_list wov_calc_pll_div_l(uint32_t i2s_clk_freq,
			uint32_t sample_rate, struct wov_ppl_divider *pll_div)
{
	int32_t d_f;
	int32_t total_div;
	int32_t d_in;
	int32_t n;
	int32_t vco_freq;
	int32_t i2s_clk_freq_khz;

	n = i2s_clk_freq / sample_rate;
	if (i2s_clk_freq != (sample_rate * n))
		return EC_ERROR_INVAL;

	if ((n < 32) || (n >= 257))
		return EC_ERROR_INVAL;

	pll_div->pll_frame_len = n;

	i2s_clk_freq_khz = i2s_clk_freq / 1000;

	/*
	 * The code below implemented the “PLL setting option” table as
	 * describe in the NPCX7m7wb specification document.
	 * - Total_div is VCO frequency in MHz / 12 MHz
	 * - d_f is the Feedback Divider
	 * - d_in is the Input Divider (PLL_INDV)
	 * - d_e is the PLL Ext Divider
	 * - d_2 is the Output Divide 2 (PLL_OTDV2)
	 * - d_1 is the Output Divide 1 (PLL_OTDV1)
	 * It is preferred that d_f will be as smaller as possible, after that
	 * the d_in will be as smaller as possible and so on, this is the
	 * reason that d_f (calculated from total_div) is in the external loop
	 * and d-1 is in the internal loop (as it may contain the bigger value).
	 * The “PLL setting option” code divided to 2 function in order to
	 * fulfil the coding style indentation rule.
	 */

	/* total div is min_vco/12 400/12=33. */
	for (total_div = 33; total_div < 1500; total_div++) {
		d_f = (total_div * 12000) / i2s_clk_freq_khz;
		if ((total_div * 12000) == (d_f * i2s_clk_freq_khz)) {
			for (d_in = 1; d_in < 10; d_in++) {
				if (((i2s_clk_freq / 1000) / d_in) <= 500)
					continue;

				vco_freq = total_div * 12 / d_in;
				if ((vco_freq < 500) || (vco_freq > 1600))
					continue;
				if (wov_calc_pll_div_s(d_in, total_div,
						vco_freq, pll_div) ==
						EC_SUCCESS) {
					pll_div->pll_fbdv  = d_f;
					return EC_SUCCESS;
				}

			}
		}
	}

	return EC_ERROR_INVAL;
}

/**
 * Check if PLL is locked.
 *
 * @param   None
 *
 * @return  EC_SUCCESS if PLL is locked, EC_ERROR_UNKNOWN otherwise .
 */
enum ec_error_list wov_wait_for_pll_lock_l(void)
{
	volatile uint32_t index;

	for (index = 0; WOV_PLL_IS_NOT_LOCK; index++) {
		/* PLL doesn't reach to lock state. */
		if (index > 0xFFFF)
			return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

/**
 * Configure I2S bus. (Parameters determined via common config functions.)
 *
 * @param   None.
 *
 * @return  none.
 */
static enum ec_error_list wov_set_i2s_config_l(void)
{
	struct wov_ppl_divider pll_div;
	enum ec_error_list ret_code;
	enum wov_i2s_chan_trigger trigger_0, trigger_1;
	int32_t start_delay_0, start_delay_1;

	ret_code = wov_calc_pll_div_l(wov_conf.i2s_clock,
				wov_conf.sample_per_sec, &pll_div);
	if (ret_code == EC_SUCCESS) {
		/* Configure the PLL. */
		ret_code = wov_pll_clk_div_config(
			pll_div.pll_otdv1, pll_div.pll_otdv2, pll_div.pll_fbdv,
			pll_div.pll_indv);
		if (ret_code != EC_SUCCESS)
			return ret_code;

		ret_code = wov_pll_clk_ext_div_config(
			(enum wov_pll_ext_div_sel)(pll_div.pll_ediv > 15),
			pll_div.pll_ediv);
		if (ret_code != EC_SUCCESS)
			return ret_code;

		wov_i2s_global_config(
			(enum wov_floating_mode)(wov_conf.dai_format ==
						 WOV_DAI_FMT_PCM_TDM),
			WOV_FLOATING_DRIVEN, WOV_CLK_NORMAL, 0, WOV_PULL_DOWN,
			0, WOV_PULL_DOWN, WOV_NORMAL_MODE);

		/* Configure DAI format. */
		switch (wov_conf.dai_format) {
		case WOV_DAI_FMT_I2S:
			trigger_0 = WOV_I2S_SAMPLED_0_AFTER_1;
			trigger_1 = WOV_I2S_SAMPLED_1_AFTER_0;
			start_delay_0 = 1;
			start_delay_1 = 1;
			break;

		case WOV_DAI_FMT_RIGHT_J:
			trigger_0 = WOV_I2S_SAMPLED_1_AFTER_0;
			trigger_1 = WOV_I2S_SAMPLED_0_AFTER_1;
			start_delay_0 = (pll_div.pll_frame_len / 2) -
					wov_conf.bit_depth;
			start_delay_1 = (pll_div.pll_frame_len / 2) -
					wov_conf.bit_depth;
			break;

		case WOV_DAI_FMT_LEFT_J:
			trigger_0 = WOV_I2S_SAMPLED_1_AFTER_0;
			trigger_1 = WOV_I2S_SAMPLED_0_AFTER_1;
			start_delay_0 = 0;
			start_delay_1 = 0;
			break;

		case WOV_DAI_FMT_PCM_A:
			trigger_0 = WOV_I2S_SAMPLED_1_AFTER_0;
			trigger_1 = WOV_I2S_SAMPLED_1_AFTER_0;
			start_delay_0 = 1;
			start_delay_1 = wov_conf.bit_depth + 1;
			break;

		case WOV_DAI_FMT_PCM_B:
			trigger_0 = WOV_I2S_SAMPLED_1_AFTER_0;
			trigger_1 = WOV_I2S_SAMPLED_1_AFTER_0;
			start_delay_0 = 0;
			start_delay_1 = wov_conf.bit_depth;
			break;

		case WOV_DAI_FMT_PCM_TDM:
			trigger_0 = WOV_I2S_SAMPLED_1_AFTER_0;
			trigger_1 = WOV_I2S_SAMPLED_1_AFTER_0;
			start_delay_0 = wov_conf.i2s_start_delay_0;
			start_delay_1 = wov_conf.i2s_start_delay_1;
			break;

		default:
			return EC_ERROR_INVALID_CONFIG;
		}

		udelay(100);

		ret_code = wov_i2s_channel_config(0, wov_conf.bit_depth,
					trigger_0, start_delay_0);

		ret_code = wov_i2s_channel_config(1, wov_conf.bit_depth,
					trigger_1, start_delay_1);
	}

	return EC_SUCCESS;
}

/**
 * wov_i2s_channel1_disable
 *
 * @param  disable - disabled flag, 1 means disable
 *
 * @return  None
 */
static void wov_i2s_channel1_disable(int disable)
{
	if (disable)
		SET_BIT(NPCX_WOV_I2S_CNTL(1), NPCX_WOV_I2S_CNTL1_I2S_CHN1_DIS);
	else
		CLEAR_BIT(NPCX_WOV_I2S_CNTL(1),
			  NPCX_WOV_I2S_CNTL1_I2S_CHN1_DIS);
}

/**
 * Sets microphone source.
 *
 *		    |	Left	|  Right	|  Mono		|  Stereo
 *------------------|-----------|---------------|---------------|--------------
 *FIFO_CNT.	    |0x0 or 0x2	| 0x0 or 0x2	| 0x1 or 0x3	|0x1 or 0x3
 *CFIFI_ISEL	    |	(left)	|(left)		|(left & Right)	|(left & right)
 *------------------|-----------|---------------|---------------|--------------
 *CR_DMIC.	    |   0x1	|     0x1	|      0x2	|     0x1
 *ADC_DMIC_SEL_LEFT |  (left)	|    (left)	|   (average)	|   (left)
 *------------------|-----------|---------------|---------------|--------------
 *CR_DMIC.	    |   0x1	|     0x1	|      0x2	|     0x1
 *ADC_DMIC_SEL_RIGHT|  (right)	|    (right)	|   (average)	|   (right)
 *------------------|-----------|---------------|---------------|--------------
 *MIX_2.	    |   0x0	|     0x1	|      0x0	|     0x0
 *AIADCL_SEL	    |  (normal)	|(cross inputs)	|    (normal)	|   (normal)
 *------------------|-----------|---------------|---------------|--------------
 *MIX_2.	    |   0x3	|     0x3	|      0x0	|     0x0
 *AIADCR_SEL	    |(no input)	|  (no input)	|    (normal)	|   (normal)
 *------------------|-----------|---------------|---------------|--------------
 *VAD_0.	    |   0x0	|     0x1	|      0x2	|    Not
 *VAD_INSEL	    |  (left)	|    (right)	|    (average)	|  applicable
 *
 * @param   None.
 * @return  return EC_SUCCESS if mic source valid othewise return error code.
 */
static enum ec_error_list wov_set_mic_source_l(void)
{
	switch (wov_conf.mic_src) {
	case WOV_SRC_LEFT:
		if (wov_conf.bit_depth == 16)
			SET_FIELD(NPCX_WOV_FIFO_CNT,
				NPCX_WOV_FIFO_CNT_CFIFO_ISEL, 0x00);
		else
			SET_FIELD(NPCX_WOV_FIFO_CNT,
				NPCX_WOV_FIFO_CNT_CFIFO_ISEL, 0x02);
		SET_FIELD(NPCX_APM_CR_DMIC, NPCX_APM_CR_DMIC_ADC_DMIC_SEL_LEFT,
				0x01);
		SET_FIELD(NPCX_APM_CR_DMIC, NPCX_APM_CR_DMIC_ADC_DMIC_SEL_RIGHT,
				0x01);
		apm_digital_mixer_config(APM_OUT_MIX_NORMAL_INPUT,
					 APM_OUT_MIX_NO_INPUT);
		apm_set_vad_input_channel(APM_IN_LEFT);
		wov_i2s_channel1_disable(1);
		break;

	case WOV_SRC_RIGHT:
		if (wov_conf.bit_depth == 16)
			SET_FIELD(NPCX_WOV_FIFO_CNT,
				  NPCX_WOV_FIFO_CNT_CFIFO_ISEL, 0x00);
		else
			SET_FIELD(NPCX_WOV_FIFO_CNT,
				  NPCX_WOV_FIFO_CNT_CFIFO_ISEL, 0x02);
		SET_FIELD(NPCX_APM_CR_DMIC, NPCX_APM_CR_DMIC_ADC_DMIC_SEL_LEFT,
				0x01);
		SET_FIELD(NPCX_APM_CR_DMIC, NPCX_APM_CR_DMIC_ADC_DMIC_SEL_RIGHT,
				0x01);
		apm_digital_mixer_config(APM_OUT_MIX_CROSS_INPUT,
					APM_OUT_MIX_NO_INPUT);
		apm_set_vad_input_channel(APM_IN_RIGHT);
		wov_i2s_channel1_disable(1);
		break;

	case WOV_SRC_MONO:
		if (wov_conf.bit_depth == 16)
			SET_FIELD(NPCX_WOV_FIFO_CNT,
				NPCX_WOV_FIFO_CNT_CFIFO_ISEL, 0x01);
		else
			SET_FIELD(NPCX_WOV_FIFO_CNT,
				NPCX_WOV_FIFO_CNT_CFIFO_ISEL, 0x03);
		SET_FIELD(NPCX_APM_CR_DMIC, NPCX_APM_CR_DMIC_ADC_DMIC_SEL_LEFT,
				0x02);
		SET_FIELD(NPCX_APM_CR_DMIC, NPCX_APM_CR_DMIC_ADC_DMIC_SEL_RIGHT,
				0x02);
		apm_digital_mixer_config(APM_OUT_MIX_NORMAL_INPUT,
					APM_OUT_MIX_NORMAL_INPUT);
		apm_set_vad_input_channel(APM_IN_AVERAGE_LEFT_RIGHT);
		wov_i2s_channel1_disable(0);
		break;

	case WOV_SRC_STEREO:
		if (wov_conf.bit_depth == 16)
			SET_FIELD(NPCX_WOV_FIFO_CNT,
				NPCX_WOV_FIFO_CNT_CFIFO_ISEL, 0x01);
		else
			SET_FIELD(NPCX_WOV_FIFO_CNT,
				NPCX_WOV_FIFO_CNT_CFIFO_ISEL, 0x03);
		SET_FIELD(NPCX_APM_CR_DMIC, NPCX_APM_CR_DMIC_ADC_DMIC_SEL_LEFT,
				0x01);
		SET_FIELD(NPCX_APM_CR_DMIC, NPCX_APM_CR_DMIC_ADC_DMIC_SEL_RIGHT,
				0x01);
		apm_digital_mixer_config(APM_OUT_MIX_NORMAL_INPUT,
					 APM_OUT_MIX_NORMAL_INPUT);
		wov_i2s_channel1_disable(0);
		break;

	default:
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

static void wov_over_under_deferred(void)
{
	CPRINTS("wov: Under/Over run error: under = %d, over = %d",
		irq_underrun_count, irq_overrun_count);
}
DECLARE_DEFERRED(wov_over_under_deferred);

static void wov_under_over_error_handler(int *count, uint32_t *last_time)
{
	uint32_t time_delta_msec;
	uint32_t current_time = get_time().le.lo;

	if (!(*count)) {
		*last_time = current_time;
		(*count)++;
	} else {
		time_delta_msec = (current_time - *last_time) / MSEC;
		*last_time = current_time;
		if (time_delta_msec < WOV_RATE_ERROR_THRESH_MSEC)
			(*count)++;
		else
			*count = 0;

		if (*count >= WOV_RATE_ERROR_THRESH) {
			wov_stop_i2s_capture();
			hook_call_deferred(&wov_over_under_deferred_data, 0);
		}
	}
}

/**
 * WoV interrupt handler.
 *
 * @param   None
 *
 * @return  None
 */
void wov_interrupt_handler(void)
{
	uint32_t wov_status;
	uint32_t wov_inten;

	wov_inten = GET_FIELD(NPCX_WOV_WOV_INTEN, NPCX_WOV_STATUS_BITS);
	wov_status = wov_inten &
			GET_FIELD(NPCX_WOV_STATUS, NPCX_WOV_STATUS_BITS);

	/*
	 * Voice activity detected.
	 */
	if (APM_IS_VOICE_ACTIVITY_DETECTED) {
		apm_enable_vad_interrupt(0);
		APM_CLEAR_VAD_INTERRUPT;
		WOV_CALLBACK(WOV_EVENT_VAD);
	}

	/* Core FIFO is overrun. Reset the Core FIFO and inform the FW */
	if (WOV_IS_CFIFO_OVERRUN(wov_status)) {
		WOV_CALLBACK(WOV_EVENT_ERROR_CORE_FIFO_OVERRUN);
		wov_core_fifo_reset();
	} else if (WOV_IS_CFIFO_INT_THRESHOLD(wov_status) &&
				(cfifo_buf.buf != NULL)) {
		/*
		 * Core FIFO threshold or FIFO not empty event occurred.
		 * - Read data from core FIFO to the buffer.
		 * - In case data ready or no space for data, inform the FW.
		 */

		/* Copy data from CFIFO to RAM. */
		wov_cfifo_read_handler_l((WOV_GET_CORE_FIFO_THRESHOLD * 2));

		if (cfifo_buf.size < (WOV_GET_CORE_FIFO_THRESHOLD * 2)) {
			cfifo_buf.buf = NULL;
			cfifo_buf.size = 0;
			WOV_CALLBACK(WOV_EVENT_DATA_READY);
		}
	}

	/* I2S FIFO is overrun. Reset the I2S FIFO and inform the FW. */
	if (WOV_IS_I2S_FIFO_OVERRUN(wov_status)) {
		WOV_CALLBACK(WOV_EVENT_ERROR_I2S_FIFO_OVERRUN);
		wov_under_over_error_handler(&irq_overrun_count,
					     &wov_i2s_overrun_tstamp);
		wov_i2s_fifo_reset();
	}

	/* I2S FIFO is underrun. Reset the I2S FIFO and inform the FW. */
	if (WOV_IS_I2S_FIFO_UNDERRUN(wov_status)) {
		WOV_CALLBACK(WOV_EVENT_ERROR_I2S_FIFO_UNDERRUN);
		wov_under_over_error_handler(&irq_underrun_count,
					     &wov_i2s_underrun_tstamp);
		wov_i2s_fifo_reset();
	}


	/* Clear the WoV status register. */
	SET_FIELD(NPCX_WOV_STATUS, NPCX_WOV_STATUS_BITS, wov_status);
}

DECLARE_IRQ(NPCX_IRQ_WOV, wov_interrupt_handler, 4);

/**
 * Enable FMUL2.
 *
 * @param   enable - enabled flag, true for enable
 * @return  None
 */
static void wov_fmul2_enable(int enable)
{
	if (enable) {

		/* If clock disabled, then enable it. */
		if (IS_BIT_SET(NPCX_FMUL2_FM2CTRL,
					NPCX_FMUL2_FM2CTRL_FMUL2_DIS)) {
			/* Enable clock tuning. */
			CLEAR_BIT(NPCX_FMUL2_FM2CTRL,
					NPCX_FMUL2_FM2CTRL_TUNE_DIS);
			/* Enable clock. */
			CLEAR_BIT(NPCX_FMUL2_FM2CTRL,
					NPCX_FMUL2_FM2CTRL_FMUL2_DIS);

			udelay(WOV_FMUL2_CLK_TUNING_DELAY_TIME);

		}
	} else
		SET_BIT(NPCX_FMUL2_FM2CTRL, NPCX_FMUL2_FM2CTRL_FMUL2_DIS);
}

#define WOV_FMUL2_MAX_RETRIES 0x000FFFFF

/* FMUL2 clock multipliers values. */
struct wov_fmul2_multiplier_setting_val {
	uint8_t fm2mh;
	uint8_t fm2ml;
	uint8_t fm2n;
};

/**
 * Configure FMUL2 clock tunning.
 *
 * @param   None
 * @return  None
 */
void wov_fmul2_conf_tuning(void)
{
	/* Check if FMUL2 is enabled, then do nothing. */
	if (IS_BIT_SET(NPCX_FMUL2_FM2CTRL, NPCX_FMUL2_FM2CTRL_FMUL2_DIS) ==
			0x00)
		return;

	/* Enable clock tuning. */
	CLEAR_BIT(NPCX_FMUL2_FM2CTRL, NPCX_FMUL2_FM2CTRL_TUNE_DIS);

	udelay(WOV_FMUL2_CLK_TUNING_DELAY_TIME);

	/* Disable clock tuning. */
	SET_BIT(NPCX_FMUL2_FM2CTRL, NPCX_FMUL2_FM2CTRL_TUNE_DIS);
}

static int wov_get_cfifo_threshold_l(void)
{
	int fifo_threshold;

	fifo_threshold = WOV_GET_FIFO_INT_THRESHOLD;

	if (fifo_threshold == 0)
		return 1;
	else
		return (fifo_threshold * 2);
}

/**
 * Gets clock source FMUL2 or PLL.
 *
 * @param   None.
 *
 *  NOTE:
 *
 * @return  The clock source FMUL2 (WOV_FMUL2_CLK_SRC) and
 *                    PLL (WOV_PLL_CLK_SRC)
 */
static enum wov_clk_src_sel wov_get_clk_selection(void)
{
	if (IS_BIT_SET(NPCX_WOV_CLOCK_CNTL, NPCX_WOV_CLOCK_CNT_CLK_SEL))
		return WOV_PLL_CLK_SRC;
	else
		return WOV_FMUL2_CLK_SRC;
}

/***************************************************************************
 *
 * Exported function.
 *
 **************************************************************************/

/**
 * Set FMUL2 clock divider.
 *
 * @param   None
 * @return  None
 */
void wov_fmul2_set_clk_divider(enum fmul2_clk_divider clk_div)
{
	SET_FIELD(NPCX_FMUL2_FM2P, NPCX_FMUL2_FM2P_WFPRED, clk_div);
}

/**
 * Configure DMIC clock.
 *
 * @param   enable          - DMIC enabled , 1 means enable
 * @param   clk_div         - DMIC clock division factor (disable, divide by 2
 *			      divide by 4)
 * @return  None
 */
void wov_dmic_clk_config(int enable, enum wov_dmic_clk_div_sel clk_div)
{
	/* If DMIC enabled then configured its clock.*/
	if (enable) {
		if (clk_div != WOV_DMIC_DIV_DISABLE) {
			SET_BIT(NPCX_WOV_CLOCK_CNTL,
				NPCX_WOV_CLOCK_CNT_DMIC_CKDIV_EN);
			if (clk_div == WOV_DMIC_DIV_BY_2)
				CLEAR_BIT(NPCX_WOV_CLOCK_CNTL,
					  NPCX_WOV_CLOCK_CNT_DMIC_CKDIV_SEL);
			else
				SET_BIT(NPCX_WOV_CLOCK_CNTL,
					NPCX_WOV_CLOCK_CNT_DMIC_CKDIV_SEL);
		} else
			CLEAR_BIT(NPCX_WOV_CLOCK_CNTL,
				  NPCX_WOV_CLOCK_CNT_DMIC_CKDIV_EN);

		SET_BIT(NPCX_WOV_CLOCK_CNTL, NPCX_WOV_CLOCK_CNT_DMIC_EN);
	} else
		CLEAR_BIT(NPCX_WOV_CLOCK_CNTL, NPCX_WOV_CLOCK_CNT_DMIC_EN);
}

/**
 * Sets WoV mode
 *
 * @param   wov_mode - WoV mode
 * @return  EC_ERROR_INVAL or EC_SUCCESS
 */
enum ec_error_list wov_set_mode(enum wov_modes wov_mode)
{
	enum ec_error_list ret_code;
	enum wov_clk_src_sel prev_clock;

	/* If mode is OFF, then power down and exit. */
	if (wov_mode == WOV_MODE_OFF) {
		wov_stop_i2s_capture();
		wov_stop_ram_capture();
		wov_set_clk_selection(WOV_FMUL2_CLK_SRC);
		wov_dmic_clk_config(0, WOV_DMIC_DIV_DISABLE);
		wov_mute(1);
		apm_set_mode(WOV_MODE_OFF);
		wov_fmul2_enable(0);
		wov_conf.mode = WOV_MODE_OFF;
		return EC_SUCCESS;
	}

	switch (wov_mode) {
	case WOV_MODE_VAD:
		if (apm_get_vad_dmic_rate() == APM_DMIC_RATE_0_75)
			wov_dmic_clk_config(1, WOV_DMIC_DIV_BY_4);
		else if (apm_get_vad_dmic_rate() == APM_DMIC_RATE_1_2)
			wov_dmic_clk_config(1, WOV_DMIC_DIV_BY_2);
		else
			wov_dmic_clk_config(1, WOV_DMIC_DIV_DISABLE);
		wov_stop_i2s_capture();
		wov_stop_ram_capture();
		wov_set_clk_selection(WOV_FMUL2_CLK_SRC);
		apm_set_mode(wov_mode);
		ret_code = wov_set_mic_source_l();
		if (ret_code != EC_SUCCESS)
			return ret_code;
		break;
	case WOV_MODE_RAM:
		if ((wov_conf.bit_depth != 16) && (wov_conf.bit_depth != 24))
			return EC_ERROR_INVAL;

		if (apm_get_adc_ram_dmic_rate() == APM_DMIC_RATE_0_75)
			wov_dmic_clk_config(1, WOV_DMIC_DIV_BY_4);
		else if (apm_get_adc_ram_dmic_rate() == APM_DMIC_RATE_1_2)
			wov_dmic_clk_config(1, WOV_DMIC_DIV_BY_2);
		else
			wov_dmic_clk_config(1, WOV_DMIC_DIV_DISABLE);
		wov_stop_i2s_capture();
		wov_set_clk_selection(WOV_FMUL2_CLK_SRC);
		apm_set_mode(wov_mode);
		ret_code = wov_set_mic_source_l();
		if (ret_code != EC_SUCCESS)
			return ret_code;
		wov_start_ram_capture();
		break;
	case WOV_MODE_RAM_AND_I2S:
		if ((wov_conf.bit_depth != 16) && (wov_conf.bit_depth != 24))
			return EC_ERROR_INVAL;
	case WOV_MODE_I2S:
		if (apm_get_adc_i2s_dmic_rate() == APM_DMIC_RATE_0_75)
			wov_dmic_clk_config(1, WOV_DMIC_DIV_BY_4);
		else if (apm_get_adc_i2s_dmic_rate() == APM_DMIC_RATE_1_2)
			wov_dmic_clk_config(1, WOV_DMIC_DIV_BY_2);
		else
			wov_dmic_clk_config(1, WOV_DMIC_DIV_DISABLE);
		prev_clock = wov_get_clk_selection();
		if (prev_clock != WOV_PLL_CLK_SRC) {
			wov_set_i2s_config_l();
			wov_set_clk_selection(WOV_PLL_CLK_SRC);
		}
		apm_set_mode(wov_mode);
		ret_code = wov_set_mic_source_l();
		if (ret_code != EC_SUCCESS)
			return ret_code;
		wov_start_i2s_capture();
		if (wov_mode == WOV_MODE_RAM_AND_I2S)
			wov_start_ram_capture();
		else
			wov_stop_ram_capture();
		break;
	default:
		wov_dmic_clk_config(0, WOV_DMIC_DIV_DISABLE);
		wov_fmul2_enable(0);
		wov_mute(1);
		return EC_ERROR_INVAL;
	}

	wov_mute(0);

	wov_conf.mode = wov_mode;

	return EC_SUCCESS;
}

/**
 * Gets WoV mode
 *
 * @param   None
 * @return  WoV mode
 */
enum wov_modes wov_get_mode(void)
{
	return wov_conf.mode;
}

/**
 * Initiates WoV.
 *
 * @param	callback	 - Pointer to callback function.
 *
 * @return	None
 */
void wov_init(void)
{
	apm_init();

	wov_apm_active(1);
	wov_mute(1);

	wov_conf.mode = WOV_MODE_OFF;
	wov_conf.sample_per_sec = 16000;
	wov_conf.bit_depth = 16;
	wov_conf.mic_src = WOV_SRC_LEFT;
	wov_conf.left_chan_gain = 0;
	wov_conf.right_chan_gain = 0;
	wov_conf.i2s_start_delay_0 = 0;
	wov_conf.i2s_start_delay_1 = 0;
	wov_conf.i2s_clock = 0;
	wov_conf.dai_format = WOV_DAI_FMT_I2S;
	wov_conf.sensitivity_db = 5;

	/* Set DMIC clock signal output to use fast transitions. */
	SET_BIT(NPCX_DEVALT(0xE), NPCX_DEVALTE_DMCLK_FAST);

	callback_fun = wov_handle_event;

	wov_cfifo_config(WOV_CFIFO_IN_LEFT_CHAN_2_CONS_16_BITS,
			 WOV_FIFO_THRESHOLD_80_DATA_WORDS);

	apm_set_vad_dmic_rate(APM_DMIC_RATE_0_75);
	apm_set_adc_ram_dmic_config(APM_DMIC_RATE_0_75);
	apm_set_adc_i2s_dmic_config(APM_DMIC_RATE_3_0);
}

/**
 * Select clock source FMUL2 or PLL.
 *
 * @param   clk_src - select between FMUL2 (WOV_FMUL2_CLK_SRC) and
 *                    PLL (WOV_PLL_CLK_SRC)
 *
 *  NOTE: THIS FUNCTION RESETS THE APM and RETURN ITS REGISSTERS TO THEIR
 *        DEFAULT VALUES !!!!!!!
 *
 * @return  None
 */
void wov_set_clk_selection(enum wov_clk_src_sel clk_src)
{
	int is_apm_disable;

	/*
	 * Be sure that both clocks are active, as both of them need to
	 * be active when modify the CLK_SEL bit.
	 */
	if (IS_BIT_SET(NPCX_WOV_PLL_CNTL1, NPCX_WOV_PLL_CNTL1_PLL_PWDEN))
		wov_pll_enable(1);

	if (IS_BIT_SET(NPCX_FMUL2_FM2CTRL, NPCX_FMUL2_FM2CTRL_FMUL2_DIS))
		wov_fmul2_enable(1);

	is_apm_disable = IS_BIT_SET(NPCX_APM_CR_APM, NPCX_APM_CR_APM_PD);

	apm_enable(0);

	if (clk_src == WOV_FMUL2_CLK_SRC)
		CLEAR_BIT(NPCX_WOV_CLOCK_CNTL, NPCX_WOV_CLOCK_CNT_CLK_SEL);
	else if (wov_wait_for_pll_lock_l() == EC_SUCCESS)
		SET_BIT(NPCX_WOV_CLOCK_CNTL, NPCX_WOV_CLOCK_CNT_CLK_SEL);

	udelay(100);

	if (!is_apm_disable)
		apm_enable(1);

	/* Disable the unneeded clock. */
	if (clk_src == WOV_PLL_CLK_SRC)
		wov_fmul2_enable(0);
	else
		wov_pll_enable(0);

}

/**
 * Configure PLL clock.
 *
 * @param   ext_div_sel         - PLL external divider selector.
 * @param   div_factor          - When ext_div_sel is WOV_PLL_EXT_DIV_BIN_CNT
 *                                then it is the 4 LSBits of this field,
 *                                otherwise this field is an index to
 *                                PLL External Divider Load Values table.
 * @return  EC_ERROR_INVAL or EC_SUCCESS
 */
enum ec_error_list wov_pll_clk_ext_div_config(
	enum wov_pll_ext_div_sel ext_div_sel,
	uint32_t div_factor)
{
	/* Sets the clock division factor for the PLL external divider.
	 * The divide factor should be in the range of 2 to 67.
	 * When ext_div_sel is WOV_PLL_EXT_DIV_BIN_CNT, then the 4 least
	 * significant bits of div_factor  are used to set the divide
	 * ratio.
	 * In this case the divide ration legal values are from 2 to 15
	 * For WOV_PLL_EXT_DIV_LFSR, this parameter is used as index for
	 * pll_ext_div table.
	 */
	if (ext_div_sel == WOV_PLL_EXT_DIV_BIN_CNT)
		CLEAR_BIT(NPCX_WOV_CLOCK_CNTL, NPCX_WOV_CLOCK_CNT_PLL_EDIV_SEL);
	else
		SET_BIT(NPCX_WOV_CLOCK_CNTL, NPCX_WOV_CLOCK_CNT_PLL_EDIV_SEL);

	if (ext_div_sel == WOV_PLL_EXT_DIV_BIN_CNT) {
		if ((div_factor > 15) || (div_factor < 2))
			return EC_ERROR_INVAL;

		SET_FIELD(NPCX_WOV_CLOCK_CNTL, NPCX_WOV_CLOCK_CNT_PLL_EDIV,
			  (div_factor));
	} else {
		if ((div_factor > 67) || (div_factor < 12))
			return EC_ERROR_INVAL;

		SET_FIELD(NPCX_WOV_CLOCK_CNTL, NPCX_WOV_CLOCK_CNT_PLL_EDIV,
			  pll_ext_div[div_factor - 12].pll_ediv);

		SET_FIELD(NPCX_WOV_CLOCK_CNTL, NPCX_WOV_CLOCK_CNT_PLL_EDIV_DC,
			  pll_ext_div[div_factor - 12].pll_ediv_dc);
	}

	return EC_SUCCESS;
}

/**
 * PLL power down.
 *
 * @param   enable - 1 enable the PLL or 0 PLL disable
 * @return  None
 */
void wov_pll_enable(int enable)
{
	if (enable)
		CLEAR_BIT(NPCX_WOV_PLL_CNTL1, NPCX_WOV_PLL_CNTL1_PLL_PWDEN);
	else
		SET_BIT(NPCX_WOV_PLL_CNTL1, NPCX_WOV_PLL_CNTL1_PLL_PWDEN);

	udelay(100);
}

/**
 * Configures PLL clock dividers..
 *
 * @param   out_div_1    - PLL output divider #1, valid values 1-7
 * @param   out_div_2    - PLL output divider #2, valid values 1-7
 * @param   feedback_div - PLL feadback divider (Default is 375 decimal)
 * @param   in_div       - PLL input divider
 * @return  EC_ERROR_INVAL or EC_SUCCESS
 */
enum ec_error_list wov_pll_clk_div_config(uint32_t out_div_1,
					uint32_t out_div_2,
					uint32_t feedback_div,
					uint32_t in_div)
{
	/* Parameter check. */
	if ((out_div_1 < 1) || (out_div_1 > 7) ||
		(out_div_2 < 1) || (out_div_2 > 7))
		return EC_ERROR_INVAL;

	/*
	 * PLL configuration sequence:
	 * 1. Set PLL_PWDEN bit to 1.
	 * 2. Set PLL divider values.
	 * 3. Wait 1usec.
	 * 4. Clear PLL_PWDEN bit to 0 while not changing other PLL parameters.
	 */
	SET_BIT(NPCX_WOV_PLL_CNTL1, NPCX_WOV_PLL_CNTL1_PLL_PWDEN);

	SET_FIELD(NPCX_WOV_PLL_CNTL1, NPCX_WOV_PLL_CNTL1_PLL_OTDV1, out_div_1);
	SET_FIELD(NPCX_WOV_PLL_CNTL1, NPCX_WOV_PLL_CNTL1_PLL_OTDV2, out_div_2);

	SET_FIELD(NPCX_WOV_PLL_CNTL2, NPCX_WOV_PLL_CNTL2_PLL_FBDV,
		  feedback_div);

	SET_FIELD(NPCX_WOV_PLL_CNTL2, NPCX_WOV_PLL_CNTL2_PLL_INDV, in_div);

	udelay(100);

	CLEAR_BIT(NPCX_WOV_PLL_CNTL1, NPCX_WOV_PLL_CNTL1_PLL_PWDEN);

	udelay(100);

	return EC_SUCCESS;
}

/**
 * Enables/Disables WoV interrupt.
 *
 * @param   int_index - Interrupt ID.
 * @param   enable    - enabled flag, 1 means enable
 *
 * @return  None.
 */
void wov_interrupt_enable(enum wov_interrupt_index int_index, int enable)
{
	if (enable)
		SET_BIT(NPCX_WOV_WOV_INTEN, wov_interupts[int_index]);
	else
		CLEAR_BIT(NPCX_WOV_WOV_INTEN, wov_interupts[int_index]);
}

/**
 * Sets core FIFO threshold.
 *
 * @param   in_sel    - Core FIFO input select
 * @param   threshold - Core FIFO threshold
 *
 * @return  None
 */
void wov_cfifo_config(enum wov_core_fifo_in_sel in_sel,
		      enum wov_fifo_threshold threshold)
{
	/* Set core FIFO input selection. */
	SET_FIELD(NPCX_WOV_FIFO_CNT, NPCX_WOV_FIFO_CNT_CFIFO_ISEL, in_sel);

	/* Set wake & interrupt core FIFO threshold. */
	WOV_SET_FIFO_WAKE_THRESHOLD(threshold);
	WOV_SET_FIFO_INT_THRESHOLD(threshold);
}

/**
 * Start the actual capturing of the Voice data to the RAM.
 * Note that the pointer to the RAM buffer must be precisely
 * set by calling wov_set_buffer();
 *
 * @param   None
 *
 * @return  None
 */
void wov_start_ram_capture(void)
{
	/* Clear the CFIFO status bits in WoV status register. */
	SET_FIELD(NPCX_WOV_STATUS, NPCX_WOV_STATUS_BITS, 0x27);

	CLEAR_BIT(NPCX_WOV_FIFO_CNT, NPCX_WOV_FIFO_CNT_CORE_FFRST);

	wov_interrupt_enable(WOV_CFIFO_OVERRUN_INT_INDX, 1);
	wov_interrupt_enable(WOV_CFIFO_THRESHOLD_INT_INDX, 1);
	wov_interrupt_enable(WOV_CFIFO_THRESHOLD_WAKE_INDX, 1);
}

/**
 * Stop the capturing of the Voice data to the RAM.
 *
 * @param   none
 *
 * @return  None
 */
void wov_stop_ram_capture(void)
{
	SET_BIT(NPCX_WOV_FIFO_CNT, NPCX_WOV_FIFO_CNT_CORE_FFRST);

	wov_interrupt_enable(WOV_CFIFO_OVERRUN_INT_INDX, 0);
	wov_interrupt_enable(WOV_CFIFO_THRESHOLD_INT_INDX, 0);
	wov_interrupt_enable(WOV_CFIFO_THRESHOLD_WAKE_INDX, 0);

	udelay(100);
}

/**
 * Rests the Core FIFO.
 *
 * @param   None
 *
 * @return  None
 */
void wov_core_fifo_reset(void)
{
	SET_BIT(NPCX_WOV_FIFO_CNT, NPCX_WOV_FIFO_CNT_CORE_FFRST);

	udelay(1000);

	/* Clear the CFIFO status bits in WoV status register. */
	SET_FIELD(NPCX_WOV_STATUS, NPCX_WOV_STATUS_BITS, 0x27);

	CLEAR_BIT(NPCX_WOV_FIFO_CNT, NPCX_WOV_FIFO_CNT_CORE_FFRST);
}

/**
 * Rests the I2S FIFO.
 *
 * @param   None
 *
 * @return  None
 */
void wov_i2s_fifo_reset(void)
{
	int disable;

	disable = IS_BIT_SET(NPCX_WOV_FIFO_CNT, NPCX_WOV_FIFO_CNT_I2S_FFRST);

	SET_BIT(NPCX_WOV_FIFO_CNT, NPCX_WOV_FIFO_CNT_I2S_FFRST);

	udelay(1000);

	/* Clear the I2S status bits in WoV status register. */
	SET_FIELD(NPCX_WOV_STATUS, NPCX_WOV_STATUS_BITS, 0x18);

	if (!disable)
		CLEAR_BIT(NPCX_WOV_FIFO_CNT, NPCX_WOV_FIFO_CNT_I2S_FFRST);
}

/**
 * Start the capturing of the Voice data via I2S.
 *
 * @param   None
 *
 * @return  None
 */
void wov_start_i2s_capture(void)
{
	/* Clear counters used to track for underrun/overrun errors */
	irq_underrun_count = 0;
	irq_overrun_count = 0;

	/* Clear the I2S status bits in WoV status register. */
	SET_FIELD(NPCX_WOV_STATUS, NPCX_WOV_STATUS_BITS, 0x18);

	CLEAR_BIT(NPCX_WOV_FIFO_CNT, NPCX_WOV_FIFO_CNT_I2S_FFRST);

	wov_interrupt_enable(WOV_I2SFIFO_OVERRUN_INT_INDX, 1);
	wov_interrupt_enable(WOV_I2SFIFO_UNDERRUN_INT_INDX, 1);
}

/**
 * Stop the capturing of the Voice data via I2S.
 *
 * @param   none
 *
 * @return  None
 */
void wov_stop_i2s_capture(void)
{
	SET_BIT(NPCX_WOV_FIFO_CNT, NPCX_WOV_FIFO_CNT_I2S_FFRST);

	wov_interrupt_enable(WOV_I2SFIFO_OVERRUN_INT_INDX, 0);
	wov_interrupt_enable(WOV_I2SFIFO_UNDERRUN_INT_INDX, 0);

	udelay(100);
}

/**
 * Sets data buffer for reading from core FIFO
 *
 * @param  buff          - Pointer to the read buffer, buffer must be 32 bits
 *                         aligned.
 * @param  size_in_words - Size must be a multiple of CONFIG_WOV_THRESHOLD_WORDS
 *                         (defaulte = 80 words)
 *
 * @return  None
 *
 *  Note - When the data buffer will be full the FW will be notifyed
 *         about it, and the FW will need to recall to this function.
 */
int wov_set_buffer(uint32_t *buf, int size_in_words)
{
	int cfifo_threshold;

	cfifo_threshold = wov_get_cfifo_threshold_l();
	if (size_in_words !=
		((size_in_words / cfifo_threshold) * cfifo_threshold))
		return EC_ERROR_INVAL;

	cfifo_buf.buf = buf;
	cfifo_buf.size = size_in_words;

	return EC_SUCCESS;
}

/**
 * Resets the APM.
 *
 * @param   enable - enabled flag, 1 means enable
 * @return  None
 */
void wov_apm_active(int enable)
{
	/* For APM it is negativ logic. */
	if (enable)
		CLEAR_BIT(NPCX_WOV_APM_CTRL, NPCX_WOV_APM_CTRL_APM_RST);
	else
		SET_BIT(NPCX_WOV_APM_CTRL, NPCX_WOV_APM_CTRL_APM_RST);
}

/**
 * I2S golobal configuration
 *
 * @param   i2s_hiz_data  - Defines when the I2S data output is floating.
 * @param   i2s_hiz       - Defines if the I2S data output is always floating.
 * @param   clk_invert    - Defines the I2S bit clock edge sensitivity
 * @param   out_pull_en   - Enable a pull-up or a pull-down resistor on
 *                          I2S output
 * @param   out_pull_mode - Select a pull-up or a pull-down resistor on
 *                          I2S output
 * @param   in_pull_en    - Enable a pull-up or a pull-down resistor on
 *                          I2S input
 * @param   in_pull_mode  - Select a pull-up or a pull-down resistor on
 *                          I2S intput
 * @param   test_mode     - Selects I2S test mode
 *
 * @return  EC_ERROR_INVAL or EC_SUCCESS
 */
enum ec_error_list wov_i2s_global_config(
	enum wov_floating_mode i2s_hiz_data,
	enum wov_floating_mode i2s_hiz,
	enum wov_clk_inverted_mode clk_invert,
	int out_pull_en,
	enum wov_pull_upd_down_sel out_pull_mode,
	int in_pull_en,
	enum wov_pull_upd_down_sel in_pull_mode,
	enum wov_test_mode test_mode)
{
	/* Check the parameters correctness. */
	if ((i2s_hiz_data == WOV_FLOATING) &&
		((GET_FIELD(NPCX_WOV_I2S_CNTL(0),
				NPCX_WOV_I2S_CNTL_I2S_ST_DEL) == 0) ||
		(GET_FIELD(NPCX_WOV_I2S_CNTL(1),
				NPCX_WOV_I2S_CNTL_I2S_ST_DEL) == 0)))
		return EC_ERROR_INVAL;

	/* Set the parameters. */
	if (i2s_hiz_data == WOV_FLOATING_DRIVEN)
		CLEAR_BIT(NPCX_WOV_I2S_CNTL(0), NPCX_WOV_I2S_CNTL0_I2S_HIZD);
	else
		SET_BIT(NPCX_WOV_I2S_CNTL(0), NPCX_WOV_I2S_CNTL0_I2S_HIZD);

	if (i2s_hiz == WOV_FLOATING_DRIVEN)
		CLEAR_BIT(NPCX_WOV_I2S_CNTL(0), NPCX_WOV_I2S_CNTL0_I2S_HIZ);
	else
		SET_BIT(NPCX_WOV_I2S_CNTL(0), NPCX_WOV_I2S_CNTL0_I2S_HIZ);

	if (clk_invert == WOV_CLK_NORMAL)
		CLEAR_BIT(NPCX_WOV_I2S_CNTL(0),
			  NPCX_WOV_I2S_CNTL0_I2S_SCLK_INV);
	else
		SET_BIT(NPCX_WOV_I2S_CNTL(0), NPCX_WOV_I2S_CNTL0_I2S_SCLK_INV);

	if (out_pull_en)
		SET_BIT(NPCX_WOV_I2S_CNTL(0), NPCX_WOV_I2S_CNTL0_I2S_OPE);
	else
		CLEAR_BIT(NPCX_WOV_I2S_CNTL(0), NPCX_WOV_I2S_CNTL0_I2S_OPE);

	if (out_pull_mode == WOV_PULL_DOWN)
		CLEAR_BIT(NPCX_WOV_I2S_CNTL(0), NPCX_WOV_I2S_CNTL0_I2S_OPS);
	else
		SET_BIT(NPCX_WOV_I2S_CNTL(0), NPCX_WOV_I2S_CNTL0_I2S_OPS);

	if (in_pull_en)
		SET_BIT(NPCX_WOV_I2S_CNTL(0), NPCX_WOV_I2S_CNTL0_I2S_IPE);
	else
		CLEAR_BIT(NPCX_WOV_I2S_CNTL(0), NPCX_WOV_I2S_CNTL0_I2S_IPE);

	if (in_pull_mode == WOV_PULL_DOWN)
		CLEAR_BIT(NPCX_WOV_I2S_CNTL(0), NPCX_WOV_I2S_CNTL0_I2S_IPS);
	else
		SET_BIT(NPCX_WOV_I2S_CNTL(0), NPCX_WOV_I2S_CNTL0_I2S_IPS);

	if (test_mode == WOV_NORMAL_MODE)
		CLEAR_BIT(NPCX_WOV_I2S_CNTL(0), NPCX_WOV_I2S_CNTL0_I2S_TST);
	else
		SET_BIT(NPCX_WOV_I2S_CNTL(0), NPCX_WOV_I2S_CNTL0_I2S_TST);

	/* I2S should be reset in order I2S interface to function correctly. */
	wov_i2s_fifo_reset();

	return EC_SUCCESS;
}

/**
 * I2S channel configuration
 *
 * @param  channel_num     - I2S channel number, 0 or 1.
 * @param  bit_count       - I2S channel bit count.
 * @param  trigger         - Define the I2S chanel trigger 1->0 or 0->1
 * @param  start_delay     - Defines the delay from the trigger defined for
 *                           the channel till the first bit (MSB) of the data.
 *
 * @return  EC_ERROR_INVAL or EC_SUCCESS
 */
enum ec_error_list wov_i2s_channel_config(uint32_t channel_num,
					uint32_t bit_count,
					enum wov_i2s_chan_trigger trigger,
					int32_t start_delay)
{
	/* Check the parameters correctnes. */
	if ((channel_num != 0) && (channel_num != 1))
		return EC_ERROR_INVAL;

	if ((start_delay < 0) || (start_delay > 496))
		return EC_ERROR_INVAL;

	if ((bit_count != 16) && (bit_count != 18) && (bit_count != 20) &&
	    (bit_count != 24))
		return EC_ERROR_INVAL;

	/* Set the parameters. */
	SET_FIELD(NPCX_WOV_I2S_CNTL(channel_num), NPCX_WOV_I2S_CNTL_I2S_BCNT,
			(bit_count - 1));

	if (trigger == WOV_I2S_SAMPLED_1_AFTER_0)
		CLEAR_BIT(NPCX_WOV_I2S_CNTL(channel_num),
			  NPCX_WOV_I2S_CNTL_I2S_TRIG);
	else
		SET_BIT(NPCX_WOV_I2S_CNTL(channel_num),
			NPCX_WOV_I2S_CNTL_I2S_TRIG);

	SET_FIELD(NPCX_WOV_I2S_CNTL(channel_num), NPCX_WOV_I2S_CNTL_I2S_ST_DEL,
		  start_delay);

	/* I2S should be reset in order I2S interface to function correctly. */
	wov_i2s_fifo_reset();

	return EC_SUCCESS;
}

/**
 * Sets sampling rate.
 *
 * @param   samples_per_second - Valid sample rate.
 * @return  In case sample rate is valid return EC_SUCCESS othewise return
 *          error code.
 */
int wov_set_sample_rate(uint32_t samples_per_second)
{
	if (wov_conf.mode != WOV_MODE_OFF)
		return EC_ERROR_INVALID_CONFIG;

	switch (samples_per_second) {
	case 8000:
	case 12000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
		wov_conf.sample_per_sec = samples_per_second;
		return EC_SUCCESS;
	default:
		return EC_ERROR_INVAL;
	}
}

/**
 * Gets sampling rate.
 *
 * @param   None
 * @return  the current sampling rate.
 */
uint32_t wov_get_sample_rate(void)
{
	return wov_conf.sample_per_sec;
}

/**
 * Sets sampling depth.
 *
 * @param   bits_num - Valid sample depth in bits.
 * @return  In case sample depth is valid return EC_SUCCESS othewise return
 *          error code.
 */
int wov_set_sample_depth(int bits_num)
{
	if (wov_conf.mode != WOV_MODE_OFF)
		return EC_ERROR_INVALID_CONFIG;

	if ((bits_num != 16) && (bits_num != 18) &&
		(bits_num != 20) && (bits_num != 24))
		return EC_ERROR_INVAL;

	wov_conf.bit_depth = bits_num;

	return EC_SUCCESS;
}

/**
 * Gets sampling depth.
 *
 * @param   None.
 * @return  sample depth in bits.
 */
int wov_get_sample_depth(void)
{
	return wov_conf.bit_depth;
}

/**
 * Sets microphone source.
 *
 * @param   mic_src - Valid microphone source
 * @return  return EC_SUCCESS if mic source valid othewise return error code.
 */
int wov_set_mic_source(enum wov_mic_source mic_src)
{
	wov_conf.mic_src = mic_src;

	return wov_set_mic_source_l();
}

/**
 * Gets microphone source.
 *
 * @param   None.
 * @return  sample depth in bits.
 */
enum wov_mic_source wov_get_mic_source(void)
{
	return wov_conf.mic_src;
}

/**
 * Mutes the WoV.
 *
 * @param   enable  - enabled flag, 1 means enable
 * @return  None
 */
void wov_mute(int enable)
{
	if (enable)
		SET_BIT(NPCX_APM_CR_ADC, NPCX_APM_CR_ADC_ADC_SOFT_MUTE);
	else
		CLEAR_BIT(NPCX_APM_CR_ADC, NPCX_APM_CR_ADC_ADC_SOFT_MUTE);
}

/**
 * Sets gain
 *
 * @param   left_chan_gain  - Left channel gain.
 * @param   right_chan_gain - Right channel gain
 * @return  None
 */
void wov_set_gain(int left_chan_gain, int right_chan_gain)
{
	wov_conf.left_chan_gain = left_chan_gain;
	wov_conf.right_chan_gain = right_chan_gain;

	(void) apm_adc_gain_config(APM_ADC_CHAN_GAINS_INDEPENDENT,
				left_chan_gain, right_chan_gain);
}

/**
 * Gets gain values
 *
 * @param   left_chan_gain  - address of left channel gain response.
 * @param   right_chan_gain - address of right channel gain response.
 * @return  None
 */
void wov_get_gain(int *left_chan_gain, int *right_chan_gain)
{
	*left_chan_gain = wov_conf.left_chan_gain;
	*right_chan_gain = wov_conf.right_chan_gain;
}

/**
 * Enables/Disables ADC.
 *
 * @param   enable - enabled flag, 1 means enable
 * @return  None
 */
void wov_enable_agc(int enable)
{
	apm_auto_gain_cntrl_enable(enable);
}

/**
 * Enables/Disables the automatic gain.
 *
 * @param stereo                - Stereo enabled flag, 1 means enable.
 * @param target                - Target output level of the ADC.
 * @param noise_gate_threshold  - Noise Gate system select. 1 means enable.
 * @param hold_time             - Hold time before starting AGC adjustment to
 *                                the TARGET value.
 * @param attack_time           - Attack time - gain ramp down.
 * @param decay_time            - Decay time - gain ramp up.
 * @param max_applied_gain      - Maximum Gain Value to apply to the ADC path.
 * @param min_applied_gain      - Minimum Gain Value to apply to the ADC path.
 * @return  EC_ERROR_INVAL or EC_SUCCESS
 */
enum ec_error_list wov_set_agc_config(int stereo, float target,
				int noise_gate_threshold, uint8_t hold_time,
				uint16_t attack_time, uint16_t decay_time,
				float max_applied_gain, float min_applied_gain)
{
	int target_code;
	int ngth_code;
	int attack_time_code;
	int decay_time_code;
	int max_applied_gain_code;
	int min_applied_gain_code;
	enum ec_error_list ret_code;
	struct apm_auto_gain_config gain_cfg;

	for (target_code = 0; target_code < 16; target_code++) {
		if (((float)target_code * (-1.5)) == target)
			break;
	}
	if (target_code == 16)
		return EC_ERROR_INVAL;

	if (noise_gate_threshold == 0)
		ngth_code = 0;
	else {
		for (ngth_code = 0; ngth_code <= 0x07; ngth_code++) {
			if ((-68 + ngth_code * 6) == noise_gate_threshold)
				break;
		}
		if (ngth_code * 6 > 42)
			return EC_ERROR_INVAL;
	}

	if (hold_time > 15)
		return EC_ERROR_INVAL;

	for (attack_time_code = 0; attack_time_code <= 0x0F;
			attack_time_code++) {
		if (((attack_time_code + 1) * 32) == attack_time)
			break;
	}
	if (attack_time_code > 0x0F)
		return EC_ERROR_INVAL;

	for (decay_time_code = 0; decay_time_code <= 0x0F; decay_time_code++) {
		if (((decay_time_code + 1) * 32) == decay_time)
			break;
	}
	if (decay_time_code > 0x0F)
		return EC_ERROR_INVAL;

	for (max_applied_gain_code = 0; max_applied_gain_code < 16;
			max_applied_gain_code++) {
		if ((max_applied_gain_code * 1.5) == max_applied_gain)
			break;
	}
	if (max_applied_gain_code == 16) {
		for (max_applied_gain_code = 18; max_applied_gain_code < 32;
				max_applied_gain_code++) {
			if (((max_applied_gain_code * 1.5) - 4) ==
					max_applied_gain)
				break;
		}
	}
	if (max_applied_gain_code >= 32)
		return EC_ERROR_INVAL;

	for (min_applied_gain_code = 0; min_applied_gain_code < 16;
			min_applied_gain_code++) {
		if ((min_applied_gain_code * 1.5) == min_applied_gain)
			break;
	}
	if (min_applied_gain_code == 16) {
		for (min_applied_gain_code = 18; min_applied_gain_code < 32;
				min_applied_gain_code++) {
			if (((min_applied_gain_code * 1.5) - 4) ==
					min_applied_gain)
				break;
		}
	}
	if (min_applied_gain_code > 32)
		return EC_ERROR_INVAL;

	gain_cfg.stereo_enable = stereo,
	gain_cfg.agc_target = (enum apm_adc_target_out_level) target_code;
	gain_cfg.nois_gate_en = (noise_gate_threshold != 0);
	gain_cfg.nois_gate_thold = (enum apm_noise_gate_threshold) ngth_code;
	gain_cfg.hold_time = (enum apm_agc_adj_hold_time) hold_time;
	gain_cfg.attack_time = (enum apm_gain_ramp_time) attack_time_code;
	gain_cfg.decay_time = (enum apm_gain_ramp_time) decay_time_code;
	gain_cfg.gain_max = (enum apm_gain_values) max_applied_gain_code;
	gain_cfg.gain_min = (enum apm_gain_values) min_applied_gain_code;

	ret_code = apm_adc_auto_gain_config(&gain_cfg);

	return ret_code;
}

/**
 * Sets VAD sensitivity.
 *
 * @param   sensitivity_db - VAD sensitivity in db.
 * @return  None
 */
int wov_set_vad_sensitivity(int sensitivity_db)
{

	if ((sensitivity_db < 0) || (sensitivity_db > 31))
		return EC_ERROR_INVAL;

	wov_conf.sensitivity_db = sensitivity_db;

	apm_set_vad_sensitivity(sensitivity_db);

	return EC_SUCCESS;
}

/**
 * Gets VAD sensitivity.
 *
 * @param   None.
 * @return  VAD sensitivity in db
 */
int wov_get_vad_sensitivity(void)
{
	return wov_conf.sensitivity_db;
}

/**
 * Configure I2S bus format. (Sample rate and size are determined via common
 * config functions.)
 *
 * @param   format    - one of the following: I2S mode, Right Justified mode,
 *                      Left Justified mode, PCM A Audio, PCM B Audio and
 *                      Time Division Multiplexing
 * @return  EC error code.
 */
void wov_set_i2s_fmt(enum wov_dai_format format)
{
	if (wov_conf.mode != WOV_MODE_OFF)
		return;

	wov_conf.dai_format = format;
}

/**
 * Configure I2S bus clock. (Sample rate and size are determined via common
 * config functions.)
 *
 * @param   i2s_clock - I2S clock frequency in Hz (needed in order to
 *                      configure the internal PLL for 12MHz)
 * @return  EC error code.
 */
void wov_set_i2s_bclk(uint32_t i2s_clock)
{
	if (wov_conf.mode != WOV_MODE_OFF)
		return;

	wov_conf.i2s_clock = i2s_clock;
}

/**
 * Configure I2S bus. (Sample rate and size are determined via common
 * config functions.)
 *
 * @param   ch0_delay - 0 to 496.  Defines the delay from the SYNC till the
 *                      first bit (MSB) of channel 0 (left channel)
 * @param   ch1_delay - -1 to 496.  Defines the delay from the SYNC till the
 *                      first bit (MSB) of channel 1 (right channel).
 *                      If channel 1 is not used set this field to -1.
 *
 * @param   flags     -  WOV_TDM_ADJACENT_TO_CH0 = BIT(0).  There is a
 *                       channel adjacent to channel 0, so float SDAT when
 *                       driving the last bit (LSB) of the channel during the
 *                       second half of the clock cycle to avoid bus contention.
 *
 *                       WOV_TDM_ADJACENT_TO_CH1 = BIT(1). There is a channel
 *                       adjacent to channel 1.
 *
 * @return  EC error code.
 */
enum ec_error_list wov_set_i2s_tdm_config(int ch0_delay, int ch1_delay,
				uint32_t flags)
{
	if (wov_conf.mode != WOV_MODE_OFF)
		return EC_ERROR_INVALID_CONFIG;

	if ((ch0_delay < 0) || (ch0_delay > 496) ||
		(ch1_delay < -1) || (ch1_delay > 496))
		return EC_ERROR_INVAL;

	wov_conf.i2s_start_delay_0 = ch0_delay;
	wov_conf.i2s_start_delay_1 = ch1_delay;

	SET_FIELD(NPCX_WOV_I2S_CNTL(0), NPCX_WOV_I2S_CNTL_I2S_ST_DEL,
			ch0_delay);

	if (ch1_delay == -1)
		wov_i2s_channel1_disable(1);
	else {
		wov_i2s_channel1_disable(0);
		SET_FIELD(NPCX_WOV_I2S_CNTL(1), NPCX_WOV_I2S_CNTL_I2S_ST_DEL,
			ch1_delay);
	}

	if (flags & 0x0001)
		SET_BIT(NPCX_WOV_I2S_CNTL(0), NPCX_WOV_I2S_CNTL_I2S_LBHIZ);
	else
		CLEAR_BIT(NPCX_WOV_I2S_CNTL(0), NPCX_WOV_I2S_CNTL_I2S_LBHIZ);

	if (flags & 0x0002)
		SET_BIT(NPCX_WOV_I2S_CNTL(1), NPCX_WOV_I2S_CNTL_I2S_LBHIZ);
	else
		CLEAR_BIT(NPCX_WOV_I2S_CNTL(1), NPCX_WOV_I2S_CNTL_I2S_LBHIZ);

	/* I2S should be reset in order I2S interface to function correctly. */
	wov_i2s_fifo_reset();

	return EC_SUCCESS;
}

static void wov_system_init(void)
{
	/* Set WoV module to be operational. */
	clock_enable_peripheral(CGC_OFFSET_WOV, CGC_WOV_MASK,
				CGC_MODE_RUN | CGC_MODE_SLEEP);
	/* Configure pins from GPIOs to WOV */
	gpio_config_module(MODULE_WOV, 1);
	wov_init();

	task_enable_irq(NPCX_IRQ_WOV);

	CPRINTS("WoV init done");
}
DECLARE_HOOK(HOOK_INIT, wov_system_init, HOOK_PRIO_DEFAULT);

void wov_handle_event(enum wov_events event)
{
	if (event == WOV_EVENT_DATA_READY) {
		CPRINTS("ram data ready and stop ram capture");
		/* just capture one times on RAM*/
		wov_stop_ram_capture();
	}
	if (event == WOV_EVENT_VAD)
		CPRINTS("got vad");
	if (event == WOV_EVENT_ERROR_CORE_FIFO_OVERRUN)
		CPRINTS("error: cfifo overrun");
}

#ifdef DEBUG_AUDIO_CODEC
static uint32_t voice_buffer[VOICE_BUF_SIZE] = {0};

/* voice data 16Khz 2ch 16bit 1s */
static int command_wov(int argc, char **argv)
{
	static int bit_clk;
	static enum wov_dai_format i2s_fmt;

	if (argc == 2) {
		if (strcasecmp(argv[1], "init") == 0) {
			wov_system_init();
			return EC_SUCCESS;
		}
		if (strcasecmp(argv[1], "cfgget") == 0) {
			CPRINTS("mode:%d", wov_get_mode());
			CPRINTS("sample rate:%d", wov_get_sample_rate());
			CPRINTS("sample bits:%d", wov_get_sample_depth());
			CPRINTS("mic source:%d", wov_get_mic_source());
			CPRINTS("vad sensitivity :%d",
				wov_get_vad_sensitivity());
			return EC_SUCCESS;
		}
		/* Start to capature voice data and store in RAM buffer */
		if (strcasecmp(argv[1], "capram") == 0) {
			if (wov_set_buffer((uint32_t *)voice_buffer,
				sizeof(voice_buffer) / sizeof(uint32_t))
					== EC_SUCCESS) {
				CPRINTS("Start RAM Catpure...");
				wov_start_ram_capture();
				return EC_SUCCESS;
			}
			CPRINTS("Init fail: voice buffer size");
			return EC_ERROR_INVAL;
		}
	} else if (argc == 3) {
		if (strcasecmp(argv[1], "cfgsrc") == 0) {
			if (strcasecmp(argv[2], "mono") == 0)
				wov_set_mic_source(WOV_SRC_MONO);
			else if (strcasecmp(argv[2], "stereo") == 0)
				wov_set_mic_source(WOV_SRC_STEREO);
			else if (strcasecmp(argv[2], "left") == 0)
				wov_set_mic_source(WOV_SRC_LEFT);
			else if (strcasecmp(argv[2], "right") == 0)
				wov_set_mic_source(WOV_SRC_RIGHT);
			else
				return EC_ERROR_INVAL;

			wov_i2s_fifo_reset();
			return EC_SUCCESS;
		}
		if (strcasecmp(argv[1], "cfgbit") == 0) {
			int bits;

			bits = atoi(argv[2]);
			if ((bits == 16) || (bits == 18) || (bits == 20) ||
			    (bits == 24)) {
				return wov_set_sample_depth(bits);
			}
		}
		if (strcasecmp(argv[1], "cfgsfs") == 0) {
			int fs;

			fs = atoi(argv[2]);
			return wov_set_sample_rate(fs);
		}
		if (strcasecmp(argv[1], "cfgbck") == 0) {
			int fs;

			fs = wov_get_sample_rate();
			if (strcasecmp(argv[2], "32fs") == 0)
				bit_clk = fs * 32;
			else if (strcasecmp(argv[2], "48fs") == 0)
				bit_clk = fs * 48;
			else if (strcasecmp(argv[2], "64fs") == 0)
				bit_clk = fs * 64;
			else if (strcasecmp(argv[2], "128fs") == 0)
				bit_clk = fs * 128;
			else if (strcasecmp(argv[2], "256fs") == 0)
				bit_clk = fs * 256;
			else
				return EC_ERROR_INVAL;

			wov_set_i2s_fmt(i2s_fmt);
			wov_set_i2s_bclk(bit_clk);
			return EC_SUCCESS;
		}
		if (strcasecmp(argv[1], "cfgfmt") == 0) {
			if (strcasecmp(argv[2], "i2s") == 0)
				i2s_fmt = WOV_DAI_FMT_I2S;
			else if (strcasecmp(argv[2], "right") == 0)
				i2s_fmt = WOV_DAI_FMT_RIGHT_J;
			else if (strcasecmp(argv[2], "left") == 0)
				i2s_fmt = WOV_DAI_FMT_LEFT_J;
			else if (strcasecmp(argv[2], "pcma") == 0)
				i2s_fmt = WOV_DAI_FMT_PCM_A;
			else if (strcasecmp(argv[2], "pcmb") == 0)
				i2s_fmt = WOV_DAI_FMT_PCM_B;
			else if (strcasecmp(argv[2], "tdm") == 0)
				i2s_fmt = WOV_DAI_FMT_PCM_TDM;
			else
				return EC_ERROR_INVAL;

			wov_set_i2s_fmt(i2s_fmt);
			wov_set_i2s_bclk(bit_clk);
			return EC_SUCCESS;
		}
		if (strcasecmp(argv[1], "cfgdckV") == 0) {
			if (strcasecmp(argv[2], "1.0") == 0)
				apm_set_vad_dmic_rate(APM_DMIC_RATE_1_0);
			else if (strcasecmp(argv[2], "1.2") == 0)
				apm_set_vad_dmic_rate(APM_DMIC_RATE_1_2);
			else if (strcasecmp(argv[2], "2.4") == 0)
				apm_set_vad_dmic_rate(APM_DMIC_RATE_2_4);
			else if (strcasecmp(argv[2], "3.0") == 0)
				apm_set_vad_dmic_rate(APM_DMIC_RATE_3_0);
			else if (strcasecmp(argv[2], "0.75") == 0)
				apm_set_vad_dmic_rate(APM_DMIC_RATE_0_75);
			else
				return EC_ERROR_INVAL;
			return EC_SUCCESS;
		}
		if (strcasecmp(argv[1], "cfgdckR") == 0) {
			if (strcasecmp(argv[2], "1.0") == 0)
				apm_set_adc_ram_dmic_config(APM_DMIC_RATE_1_0);
			else if (strcasecmp(argv[2], "1.2") == 0)
				apm_set_adc_ram_dmic_config(APM_DMIC_RATE_1_2);
			else if (strcasecmp(argv[2], "2.4") == 0)
				apm_set_adc_ram_dmic_config(APM_DMIC_RATE_2_4);
			else if (strcasecmp(argv[2], "3.0") == 0)
				apm_set_adc_ram_dmic_config(APM_DMIC_RATE_3_0);
			else if (strcasecmp(argv[2], "0.75") == 0)
				apm_set_adc_ram_dmic_config(APM_DMIC_RATE_0_75);
			else
				return EC_ERROR_INVAL;
			return EC_SUCCESS;
		}
		if (strcasecmp(argv[1], "cfgdckI") == 0) {
			if (strcasecmp(argv[2], "1.0") == 0)
				apm_set_adc_i2s_dmic_config(APM_DMIC_RATE_1_0);
			else if (strcasecmp(argv[2], "1.2") == 0)
				apm_set_adc_i2s_dmic_config(APM_DMIC_RATE_1_2);
			else if (strcasecmp(argv[2], "2.4") == 0)
				apm_set_adc_i2s_dmic_config(APM_DMIC_RATE_2_4);
			else if (strcasecmp(argv[2], "3.0") == 0)
				apm_set_adc_i2s_dmic_config(APM_DMIC_RATE_3_0);
			else if (strcasecmp(argv[2], "0.75") == 0)
				apm_set_adc_i2s_dmic_config(APM_DMIC_RATE_0_75);
			else
				return EC_ERROR_INVAL;
			return EC_SUCCESS;
		}

		if (strcasecmp(argv[1], "cfgmod") == 0) {
			if (strcasecmp(argv[2], "off") == 0) {
				wov_set_mode(WOV_MODE_OFF);
				wov_stop_ram_capture();
			} else if (strcasecmp(argv[2], "vad") == 0) {
				wov_set_mode(WOV_MODE_VAD);
			} else if (strcasecmp(argv[2], "ram") == 0) {
				if (wov_set_buffer((uint32_t *)voice_buffer,
					sizeof(voice_buffer) / sizeof(uint32_t))
						== EC_SUCCESS)
					wov_set_mode(WOV_MODE_RAM);
				else
					return EC_ERROR_INVAL;
			} else if (strcasecmp(argv[2], "i2s") == 0) {
				wov_set_mode(WOV_MODE_I2S);
			} else if (strcasecmp(argv[2], "rami2s") == 0) {
				if (wov_set_buffer((uint32_t *)voice_buffer,
					sizeof(voice_buffer) / sizeof(uint32_t))
						== EC_SUCCESS)
					wov_set_mode(WOV_MODE_RAM_AND_I2S);
				else
					return EC_ERROR_INVAL;
			} else {
				return EC_ERROR_INVAL;
			}
			wov_i2s_fifo_reset();
			return EC_SUCCESS;
		}
		if (strcasecmp(argv[1], "mute") == 0) {
			if (strcasecmp(argv[2], "enable") == 0) {
				wov_mute(1);
				return EC_SUCCESS;
			}
			if (strcasecmp(argv[2], "disable") == 0) {
				wov_mute(0);
				return EC_SUCCESS;
			}
		}
		if (strcasecmp(argv[1], "fmul2") == 0) {
			if (strcasecmp(argv[2], "enable") == 0) {
				CLEAR_BIT(NPCX_FMUL2_FM2CTRL,
					NPCX_FMUL2_FM2CTRL_TUNE_DIS);
				return EC_SUCCESS;
			}
			if (strcasecmp(argv[2], "disable") == 0) {
				SET_BIT(NPCX_FMUL2_FM2CTRL,
					NPCX_FMUL2_FM2CTRL_TUNE_DIS);
				return EC_SUCCESS;
			}
		}
		if (strcasecmp(argv[1], "vadsens") == 0)
			return wov_set_vad_sensitivity(atoi(argv[2]));

		if (strcasecmp(argv[1], "gain") == 0) {
			wov_set_gain(atoi(argv[2]), atoi(argv[2]));
			return EC_SUCCESS;
		}
	} else if (argc == 5) {
		if (strcasecmp(argv[1], "cfgtdm") == 0) {
			int delay0, delay1;
			uint32_t flags;

			delay0 = atoi(argv[2]);
			delay1 = atoi(argv[3]);
			flags = atoi(argv[4]);
			if ((delay0 > 496) || (delay1 > 496) || (flags > 3) ||
			    (delay0 < 0) || (delay1 < 0)) {
				return EC_ERROR_INVAL;
			}
			wov_set_i2s_tdm_config(delay0, delay1, flags);
			return EC_SUCCESS;
		}
	}

	return EC_ERROR_INVAL;
}

DECLARE_CONSOLE_COMMAND(wov, command_wov,
		"init\n"
		"mute <enable|disable>\n"
		"capram\n"
		"cfgsrc <mono|stereo|left|right>\n"
		"cfgbit <16|18|20|24>\n"
		"cfgsfs <8000|12000|16000|24000|32000|48000>\n"
		"cfgbck <32fs|48fs|64fs|128fs|256fs>\n"
		"cfgfmt <i2s|right|left|pcma|pcmb|tdm>\n"
		"cfgmod <off|vad|ram|i2s|rami2s>\n"
		"cfgtdm [0~496 0~496 0~3]>\n"
		"cfgdckV <0.75|1.0|1.2|2.4|3.0>\n"
		"cfgdckR <0.75|1.0|1.2|2.4|3.0>\n"
		"cfgdckI <0.75|1.0|1.2|2.4|3.0>\n"
		"cfgget\n"
		"fmul2 <enable|disable>\n"
		"vadsens <0~31>\n"
		"gain <0~31>",
		"wov configuration");
#endif
