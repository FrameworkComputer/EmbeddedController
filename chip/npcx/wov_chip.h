/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_WOV_CHIP_H
#define __CROS_EC_WOV_CHIP_H

#include "common.h"

/* FMUL2 clock Frequency. */
enum fmul2_clk_freq {
	FMUL2_48_MHZ = 0, /* Default */
	FMUL2_24_MHZ
};

enum fmul2_clk_divider {
	FMUL2_CLK_NO_DIVIDER = 0x00,
	FMUL2_CLK_DIVIDER_BY_2 = 0x01,
	FMUL2_CLK_DIVIDER_BY_4 = 0x03, /* Default */
	FMUL2_CLK_DIVIDER_BY_8 = 0x07
};

/* Microphone source. */
enum wov_mic_source {
	/* Only data from left mic. */
	WOV_SRC_LEFT = 0,
	/* Only data from right mic. */
	WOV_SRC_RIGHT,
	/* Both channels have the same data (average of left & right */
	WOV_SRC_MONO,
	/* Each channel has its own data. */
	WOV_SRC_STEREO
};

/* Clock source for APM. */
enum wov_clk_src_sel {
	WOV_FMUL2_CLK_SRC = 0,
	WOV_PLL_CLK_SRC = 1
};

/* FMUL clock division factore. */
enum wov_fmul_div {
	WOV_NODIV = 0,
	WOV_DIV_BY_2,
	WOV_DIV_BY_4, /* Default value */
	WOV_DIV_BY_8
};

/* Lock state. */
enum wov_lock_state {
	WOV_UNLOCK = 0,
	WOV_LOCK = 1
};

/* Reference clock source select. */
enum wov_ref_clk_src_sel {
	WOV_FREE_RUN_OSCILLATOR = 0,
	WOV_CRYSTAL_OSCILLATOR = 1
};

/* PLL external divider select. */
enum wov_ext_div_sel {
	WOV_EXT_DIV_BINARY_CNT = 0,
	WOV_EXT_DIV_LFSR_DIV = 1
};

/* FMUL output frequency. */
enum wov_fmul_out_freq {
	WOV_FMUL_OUT_FREQ_48_MHZ = 0,
	WOV_FMUL_OUT_FREQ_49_MHZ = 1
};

/* Digital microphone clock divider select. */
enum wov_dmic_clk_div_sel {
	WOV_DMIC_DIV_DISABLE = 1,
	WOV_DMIC_DIV_BY_2 = 2,
	WOV_DMIC_DIV_BY_4 = 4
};

/* FIFO threshold. */
enum wov_fifo_threshold {
	WOV_FIFO_THRESHOLD_1_DATA_WORD = 0,
	WOV_FIFO_THRESHOLD_2_DATA_WORDS = 1,
	WOV_FIFO_THRESHOLD_4_DATA_WORDS = 2,
	WOV_FIFO_THRESHOLD_8_DATA_WORDS = 4,
	WOV_FIFO_THRESHOLD_16_DATA_WORDS = 8,
	WOV_FIFO_THRESHOLD_32_DATA_WORDS = 16,
	WOV_FIFO_THRESHOLD_40_DATA_WORDS = 20,
	WOV_FIFO_THRESHOLD_64_DATA_WORDS = 32,
	WOV_FIFO_THRESHOLD_80_DATA_WORDS = 40,
	WOV_FIFO_THRESHOLD_96_DATA_WORDS = 48
};

/* FIFO DMA request select. */
enum wov_fifo_dma_req_sel {
	WOV_FIFO_DMA_DFLT_DMA_REQ_CONN = 0,
	WOV_FIFO_DMA_DMA_REQ_CON_FIFO
};

/* FIFO operational state. */
enum wov_fifo_oper_state {
	WOV_FIFO_OPERATIONAL = 0,
	WOV_FIFO_RESET, /* Default */
};

/* WoV interrupt index. */
enum wov_interrupt_index {
	WOV_VAD_INT_INDX,
	WOV_VAD_WAKE_INDX,
	WOV_CFIFO_NOT_EMPTY_INDX,
	WOV_CFIFO_THRESHOLD_INT_INDX,
	WOV_CFIFO_THRESHOLD_WAKE_INDX,
	WOV_CFIFO_OVERRUN_INT_INDX,
	WOV_I2SFIFO_OVERRUN_INT_INDX,
	WOV_I2SFIFO_UNDERRUN_INT_INDX
};

/* FIFO DMA request selection. */
enum wov_dma_req_sel {
	WOV_DFLT_ESPI_DMA_REQ = 0,
	WOV_FROM_FIFO_DMA_REQUEST
};

/* Core FIFO input select. */
enum wov_core_fifo_in_sel {
	WOV_CFIFO_IN_LEFT_CHAN_2_CONS_16_BITS = 0, /* Default */
	WOV_CFIFO_IN_LEFT_RIGHT_CHAN_16_BITS,
	WOV_CFIFO_IN_LEFT_CHAN_24_BITS,
	WOV_CFIFO_IN_LEFT_RIGHT_CHAN_24_BITS
};

/* PLL external divider selector. */
enum wov_pll_ext_div_sel {
	WOV_PLL_EXT_DIV_BIN_CNT = 0,
	WOV_PLL_EXT_DIV_LFSR
};

/* Code for events for call back function. */
enum wov_events {
	WOV_NO_EVENT = 0,
	/*
	 * Data is ready.
	 * need to call to wov_set_buffer to update the buffer * pointer
	 */
	WOV_EVENT_DATA_READY = 1,
	WOV_EVENT_VAD,		  /* Voice activity detected */

	WOV_EVENT_ERROR_FIRST = 128,
	WOV_EVENT_ERROR_CORE_FIFO_OVERRUN = 128,
	WOV_EVENT_ERROR_I2S_FIFO_UNDERRUN = 129,
	WOV_EVENT_ERROR_I2S_FIFO_OVERRUN = 130,
	WOV_EVENT_ERROR_LAST = 255,

};

/* WoV FIFO errors. */
enum wov_fifo_errors {
	WOV_FIFO_NO_ERROR = 0,
	WOV_CORE_FIFO_OVERRUN = 1, /* 2 : I2S FIFO is underrun. */
	WOV_I2S_FIFO_OVERRUN = 2,  /* 3 : I2S FIFO is overrun.  */
	WOV_I2S_FIFO_UNDERRUN = 3  /* 4 : I2S FIFO is underrun. */

};

/* Selects I2S test mode. */
enum wov_test_mode { WOV_NORMAL_MODE = 0, WOV_TEST_MODE };

/* PULL_UP/PULL_DOWN selection. */
enum wov_pull_upd_down_sel { WOV_PULL_DOWN = 0, WOV_PULL_UP };

/* I2S output data floating mode. */
enum wov_floating_mode { WOV_FLOATING_DRIVEN = 0, WOV_FLOATING };

/* Clock inverted mode. */
enum wov_clk_inverted_mode { WOV_CLK_NORMAL = 0, WOV_CLK_INVERTED };

enum wov_i2s_chan_trigger {
	WOV_I2S_SAMPLED_1_AFTER_0 = 0,
	WOV_I2S_SAMPLED_0_AFTER_1 = 1
};

/* APM modes. */
enum wov_modes {
	WOV_MODE_OFF = 1,
	WOV_MODE_VAD,
	WOV_MODE_RAM,
	WOV_MODE_I2S,
	WOV_MODE_RAM_AND_I2S
};

/* DAI format. */
enum wov_dai_format {
	WOV_DAI_FMT_I2S,     /* I2S mode			*/
	WOV_DAI_FMT_RIGHT_J, /* Right Justified mode		*/
	WOV_DAI_FMT_LEFT_J,  /* Left Justified mode		*/
	WOV_DAI_FMT_PCM_A,   /* PCM A Audio			*/
	WOV_DAI_FMT_PCM_B,   /* PCM B Audio			*/
	WOV_DAI_FMT_PCM_TDM  /* Time Division Multiplexing	*/
};

struct wov_config {
	enum wov_modes mode;
	uint32_t sample_per_sec;
	int bit_depth;
	enum wov_mic_source mic_src;
	int left_chan_gain;
	int right_chan_gain;
	uint16_t i2s_start_delay_0;
	uint16_t i2s_start_delay_1;
	uint32_t i2s_clock;
	enum wov_dai_format dai_format;
	int sensitivity_db;
};

extern struct wov_config wov_conf;

/**
 * Set FMUL2 clock divider.
 *
 * @param   None
 * @return  None
 */
void wov_fmul2_set_clk_divider(enum fmul2_clk_divider clk_div);

/**
 * WoV Call back function decleration.
 *
 * @param   event - the event that cause the call to the callback
 *		    function.
 *
 * @return  None
 */
typedef void (*wov_call_back_t)(enum wov_events);

/*
 * WoV macros.
 */

/* MACROs that set fields of the Clock Control Register structure. */
#define WOV_APM_CLK_SRC_FMUL2(reg_val) reg_val.clk_sel = 0
#define WOV_APM_CLK_SRC_PLL(reg_val) reg_val.clk_sel = 1
#define WOV_APM_GET_CLK_SRC(reg_val) (reg_val.clk_sel)

/* Core FIFO threshold. */
#define WOV_GET_CORE_FIFO_THRESHOLD WOV_GET_FIFO_INT_THRESHOLD

/******************************************************************************
 *
 * WoV APIs
 *
 ******************************************************************************/

/**
 * Initiates WoV.
 *
 * @param	callback	 - Pointer to callback function.
 *
 * @return	None
 */
void wov_init(void);

/**
 * Sets WoV stage
 *
 * @param	wov_mode - WoV stage (Table 38)
 * @return  EC_ERROR_INVAL or EC_SUCCESS
 */
enum ec_error_list wov_set_mode(enum wov_modes wov_mode);

/**
 * Gets WoV mode
 *
 * @param	None
 * @return	WoV mode
 */
enum wov_modes wov_get_mode(void);

/**
 * Configure WoV.
 *
 * @param	samples_per_second - Valid sample rate.
 * @return	In case sample rate is valid return EC_SUCCESS othewise return
 * error code.
 */
int wov_set_sample_rate(uint32_t samples_per_second);

/**
 * Gets sampling rate.
 *
 * @param	None
 * @return	the current sampling rate.
 */
uint32_t wov_get_sample_rate(void);

/**
 * Sets sampling depth.
 *
 * @param	bits_num - Valid sample depth in bits.
 * @return	In case sample depth is valid return EC_SUCCESS othewise return
 * error code.
 */
int wov_set_sample_depth(int bits_num);

/**
 * Gets sampling depth.
 *
 * @param	None.
 * @return	sample depth in bits.
 */
int wov_get_sample_depth(void);

/**
 * Sets microphone source.
 *
 * @param	mic_src - Valid microphone source
 * @return	return EC_SUCCESS if mic source valid othewise
 * return error code.
 */
int wov_set_mic_source(enum wov_mic_source mic_src);

/**
 * Gets microphone source.
 *
 * @param	None.
 * @return	sample depth in bits.
 */
enum wov_mic_source wov_get_mic_source(void);

/**
 * Mutes the WoV.
 *
 * @param	enable	- enabled flag, true means enable
 * @return	None
 */
void wov_mute(int enable);

/**
 * Gets gain values
 *
 * @param   left_chan_gain  - address of left channel gain response.
 * @param   right_chan_gain - address of right channel gain response.
 * @return  None
 */
void wov_get_gain(int *left_chan_gain, int *right_chan_gain);

/**
 * Sets gain
 *
 * @param		left_chan_gain	- Left channel gain.
 * @param		right_chan_gain - Right channel gain
 * @return	None
 */
void wov_set_gain(int left_chan_gain, int right_chan_gain);

/**
 * Enables/Disables ADC.
 *
 * @param	enable - enabled flag, true means enable
 * @return	None
 */
void wov_enable_agc(int enable);

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
		float max_applied_gain, float min_applied_gain);

/**
 * Sets VAD sensitivity.
 *
 * @param	sensitivity_db - VAD sensitivity in db.
 * @return	None
 */
int wov_set_vad_sensitivity(int sensitivity_db);

/**
 * Gets VAD sensitivity.
 *
 * @param	None.
 * @return	VAD sensitivity in db
 */
int wov_get_vad_sensitivity(void);

/**
 * Configure I2S bus format. (Sample rate and size are determined via common
 * config functions.)
 *
 * @param   format    - one of the following: I2S mode, Right Justified mode,
 *                      Left Justified mode, PCM A Audio, PCM B Audio and
 *                      Time Division Multiplexing
 * @return  EC error code.
 */
void wov_set_i2s_fmt(enum wov_dai_format format);

/**
 * Configure I2S bus clock. (Sample rate and size are determined via common
 * config functions.)
 *
 * @param   i2s_clock - I2S clock frequency in Hz (needed in order to
 *                      configure the internal PLL for 12MHz)
 * @return  EC error code.
 */
void wov_set_i2s_bclk(uint32_t i2s_clock);

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
				uint32_t flags);

/**
 * Configure FMUL2 clock tunning.
 *
 * @param   None
 * @return  None
 */
void wov_fmul2_conf_tuning(void);

/**
 * Configure DMIC clock.
 *
 * @param   enable          - DMIC enabled , true means enable
 * @param   clk_div         - DMIC clock division factor (disable, divide by 2
 *			      divide by 4)
 * @return  None
 */
void wov_dmic_clk_config(int enable, enum wov_dmic_clk_div_sel clk_div);

/**
 * FMUL2 clock control configuration.
 *
 * @param clk_src - select between FMUL2 (WOV_FMUL2_CLK_SRC) and
 *		    PLL (WOV_PLL_CLK_SRC)
 * @return	None
 */
extern void wov_set_clk_selection(enum wov_clk_src_sel clk_src);

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
	enum wov_pll_ext_div_sel ext_div_sel, uint32_t div_factor);

/**
 * PLL power down.
 *
 * @param enable - true power down the PLL or false PLL operating
 * @return	None
 */
void wov_pll_enable(int enable);

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
			uint32_t in_div);

/**
 * Enables/Disables WoV interrupt.
 *
 * @param   int_index - Interrupt ID.
 * @param   enable    - enabled flag, 1 means enable
 *
 * @return  None.
 */
void wov_interrupt_enable(enum wov_interrupt_index int_index, int enable);

/**
 * Sets core FIFO threshold.
 *
 * @param   in_sel    - Core FIFO input select
 * @param   threshold - Core FIFO threshold
 *
 * @return  None
 */
void wov_cfifo_config(enum wov_core_fifo_in_sel in_sel,
			enum wov_fifo_threshold threshold);

/**
 * Start the actual capturing of the Voice data to the RAM.
 * Note that the pointer to the RAM buffer must be precisely
 * set by calling wov_set_buffer();
 *
 * @param	None
 *
 * @return	None
 */
void wov_start_ram_capture(void);

/**
 * Stop the capturing of the Voice data to the RAM.
 *
 * @param	none
 *
 * @return  None
 */
void wov_stop_ram_capture(void);

/**
 * Rests the Core FIFO.
 *
 * @param	None
 *
 * @return	None
 */
void wov_core_fifo_reset(void);

/**
 * Rests the I2S FIFO.
 *
 * @param	None
 *
 * @return	None
 */
void wov_i2s_fifo_reset(void);

/**
 * Start the capturing of the Voice data via I2S.
 *
 * @param	None
 *
 * @return	None
 */
void wov_start_i2s_capture(void);

/**
 * Stop the capturing of the Voice data via I2S.
 *
 * @param	none
 *
 * @return  None
 */
void wov_stop_i2s_capture(void);

/**
 * Reads data from the core fifo.
 *
 * @param	num_elements - Number of elements (Dword) to read.
 *
 * @return	None
 */
void wov_cfifo_read_handler(uint32_t num_elements);

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
int wov_set_buffer(uint32_t *buff, int size_in_words);

/**
 * Resets the APM.
 *
 * @param enable - enabled flag, true or false
 * @return	None
 */
void wov_apm_active(int enable);

void wov_handle_event(enum wov_events event);

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
	int out_pull_en, enum wov_pull_upd_down_sel out_pull_mode,
	int in_pull_en,
	enum wov_pull_upd_down_sel in_pull_mode,
	enum wov_test_mode test_mode);

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
			uint32_t bit_count, enum wov_i2s_chan_trigger trigger,
			int32_t start_delay);

#endif /* __CROS_EC_WOV_CHIP_H */
