/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_APM_CHIP_H
#define __CROS_EC_APM_CHIP_H

#include "common.h"

/* MIX indirect registers. */
#define APM_INDIRECT_MIX_2_REG      0x02

/* ADC_AGC indirect registers. */
#define APM_INDIRECT_ADC_AGC_0_REG  0x00
#define APM_INDIRECT_ADC_AGC_1_REG  0x01
#define APM_INDIRECT_ADC_AGC_2_REG  0x02
#define APM_INDIRECT_ADC_AGC_3_REG  0x03
#define APM_INDIRECT_ADC_AGC_4_REG  0x04

/* APM_VAD_REG indirect registers. */
#define APM_INDIRECT_VAD_0_REG       0x00
#define APM_INDIRECT_VAD_1_REG       0x01

/* APM macros. */
#define APM_IS_IRQ_PENDING    IS_BIT_SET(NPCX_APM_SR, NPCX_APM_SR_IRQ_PEND)
#define APM_IS_VOICE_ACTIVITY_DETECTED \
	IS_BIT_SET(NPCX_APM_IFR, NPCX_APM_IFR_VAD_DTC)
#define APM_CLEAR_VAD_INTERRUPT    SET_BIT(NPCX_APM_IFR, NPCX_APM_IFR_VAD_DTC)

/* Indirect registers. */
enum apm_indirect_reg_offset {
	APM_MIX_REG = 0,
	APM_ADC_AGC_REG,
	APM_VAD_REG
};

/* ADC wind noise filter modes. */
enum apm_adc_wind_noise_filter_mode {
	APM_ADC_WIND_NOISE_FILTER_INACTIVE = 0,
	APM_ADC_WIND_NOISE_FILTER_MODE_1_ACTIVE,
	APM_ADC_WIND_NOISE_FILTER_MODE_2_ACTIVE,
	APM_ADC_WIND_NOISE_FILTER_MODE_3_ACTIVE,
};

/* ADC frequency. */
enum apm_adc_frequency {
	APM_ADC_FREQ_8_000_KHZ = 0x00,
	APM_ADC_FREQ_11_025_KHZ,
	APM_ADC_FREQ_12_000_KHZ,
	APM_ADC_FREQ_16_000_KHZ,
	APM_ADC_FREQ_22_050_KHZ,
	APM_ADC_FREQ_24_000_KHZ,
	APM_ADC_FREQ_32_000_KHZ,
	APM_ADC_FREQ_44_100_KHZ,
	APM_ADC_FREQ_48_000_KHZ,
	APM_ADC_FREQ_UNSUPPORTED = 0x0F
};

/* DMIC source. */
enum apm_dmic_src {
	APM_CURRENT_DMIC_CHANNEL = 0x01, /* Current channel, left or rigth. */
	APM_AVERAGE_DMIC_CHANNEL = 0x02  /* Average between left & right.   */
};

/* ADC digital microphone rate. */
enum apm_dmic_rate {
	/* 3.0, 2.4 & 1.0 must be 0, 1 & 2 respectively */
	APM_DMIC_RATE_3_0 = 0,   /* 3.0 -3.25 MHz (default). */
	APM_DMIC_RATE_2_4,       /* 2.4 -2.6 MHz.            */
	APM_DMIC_RATE_1_0,       /* 1.0 -1.08 MHz.           */
	APM_DMIC_RATE_1_2,       /* 1.2 MHz.                        */
	APM_DMIC_RATE_0_75       /* 750 KHz.                 */
};

/* Digitla mixer output. */
enum apm_dig_mix {
	APM_OUT_MIX_NORMAL_INPUT = 0,    /* Default. */
	APM_OUT_MIX_CROSS_INPUT,
	APM_OUT_MIX_MIXED_INPUT,
	APM_OUT_MIX_NO_INPUT
};

/* VAD Input Channel Selection */
enum apm_vad_in_channel_src {
	APM_IN_LEFT = 0,
	APM_IN_RIGHT,
	APM_IN_AVERAGE_LEFT_RIGHT,
	APM_IN_RESERVED
};

/* ADC digital gain coupling. */
enum apm_adc_gain_coupling {
	APM_ADC_CHAN_GAINS_INDEPENDENT = 0,
	APM_ADC_RIGHT_CHAN_GAIN_TRACKS_LEFT
};

/* ADC target output level. */
enum apm_adc_target_out_level {
	APM_ADC_MAX_TARGET_LEVEL = 0,
	APM_ADC_MAX_TARGET_LEVEL_1_5,
	APM_ADC_MAX_TARGET_LEVEL_3_0,
	APM_ADC_MAX_TARGET_LEVEL_4_5,
	APM_ADC_MAX_TARGET_LEVEL_6_0,
	APM_ADC_MAX_TARGET_LEVEL_7_5,
	APM_ADC_MAX_TARGET_LEVEL_9_0,
	APM_ADC_MAX_TARGET_LEVEL_10_5,
	APM_ADC_MAX_TARGET_LEVEL_12_0,
	APM_ADC_MAX_TARGET_LEVEL_13_5,
	APM_ADC_MAX_TARGET_LEVEL_15_0,
	APM_ADC_MAX_TARGET_LEVEL_16_5,
	APM_ADC_MAX_TARGET_LEVEL_18_0,
	APM_ADC_MAX_TARGET_LEVEL_19_5, /* Default. */
	APM_ADC_MAX_TARGET_LEVEL_21_0,
	APM_ADC_MAX_TARGET_LEVEL_22_5
};

/* Noise gate threshold values. */
enum apm_noise_gate_threshold {
	APM_MIN_NOISE_GET_THRESHOLD = 0,
	APM_MIN_NOISE_GET_THRESHOLD_6,
	APM_MIN_NOISE_GET_THRESHOLD_12,
	APM_MIN_NOISE_GET_THRESHOLD_18,
	APM_MIN_NOISE_GET_THRESHOLD_24,
	APM_MIN_NOISE_GET_THRESHOLD_30,
	APM_MIN_NOISE_GET_THRESHOLD_36,
	APM_MIN_NOISE_GET_THRESHOLD_42
};

/* Hold time in msec before starting AGC adjustment to the TARGET value. */
enum apm_agc_adj_hold_time {
	APM_HOLD_TIME_0 = 0,
	APM_HOLD_TIME_2,
	APM_HOLD_TIME_4,
	APM_HOLD_TIME_8,
	APM_HOLD_TIME_16,
	APM_HOLD_TIME_32,
	APM_HOLD_TIME_64,
	APM_HOLD_TIME_128, /* Default. */
	APM_HOLD_TIME_256,
	APM_HOLD_TIME_512,
	APM_HOLD_TIME_1024,
	APM_HOLD_TIME_2048,
	APM_HOLD_TIME_4096,
	APM_HOLD_TIME_8192,
	APM_HOLD_TIME_16384,
	APM_HOLD_TIME_32768
};

/* Attack time in msec - gain ramp down. */
enum apm_gain_ramp_time {
	APM_GAIN_RAMP_TIME_32 = 0,
	APM_GAIN_RAMP_TIME_64,
	APM_GAIN_RAMP_TIME_96,
	APM_GAIN_RAMP_TIME_128,
	APM_GAIN_RAMP_TIME_160, /* Default. */
	APM_GAIN_RAMP_TIME_192,
	APM_GAIN_RAMP_TIME_224,
	APM_GAIN_RAMP_TIME_256,
	APM_GAIN_RAMP_TIME_288,
	APM_GAIN_RAMP_TIME_320,
	APM_GAIN_RAMP_TIME_352,
	APM_GAIN_RAMP_TIME_384,
	APM_GAIN_RAMP_TIME_416,
	APM_GAIN_RAMP_TIME_448,
	APM_GAIN_RAMP_TIME_480,
	APM_GAIN_RAMP_TIME_512
};

/* Minimum and Maximum gain values. */
enum apm_gain_values {
	APM_GAIN_VALUE_0_0 = 0,
	APM_GAIN_VALUE_1_5,
	APM_GAIN_VALUE_3_0,
	APM_GAIN_VALUE_4_5,
	APM_GAIN_VALUE_6_0,
	APM_GAIN_VALUE_7_5,
	APM_GAIN_VALUE_9_0,
	APM_GAIN_VALUE_10_5,
	APM_GAIN_VALUE_12_0,
	APM_GAIN_VALUE_13_5,
	APM_GAIN_VALUE_15_0,
	APM_GAIN_VALUE_16_5,
	APM_GAIN_VALUE_18_0,
	APM_GAIN_VALUE_19_5,
	APM_GAIN_VALUE_21_0,
	APM_GAIN_VALUE_22_5,
	APM_GAIN_VALUE_23_0_1ST,
	APM_GAIN_VALUE_23_0_2ND,
	APM_GAIN_VALUE_23_0_3RD,
	APM_GAIN_VALUE_24_5,
	APM_GAIN_VALUE_26_0,
	APM_GAIN_VALUE_27_5,
	APM_GAIN_VALUE_29_0,
	APM_GAIN_VALUE_30_5,
	APM_GAIN_VALUE_32_0,
	APM_GAIN_VALUE_33_5,
	APM_GAIN_VALUE_35_0,
	APM_GAIN_VALUE_36_5,
	APM_GAIN_VALUE_38_0,
	APM_GAIN_VALUE_39_5,
	APM_GAIN_VALUE_41_0,
	APM_GAIN_VALUE_42_5
};

/* ADC Audio Data Word Length */
enum apm_adc_data_length {
	APM_ADC_DATA_LEN_16_BITS = 0x00,
	APM_ADC_DATA_LEN_18_BITS,
	APM_ADC_DATA_LEN_20_BITS,
	APM_ADC_DATA_LEN_24_BITS
};

struct apm_config {
	enum apm_dmic_rate vad_dmic_rate;
	enum apm_dmic_rate adc_ram_dmic_rate;
	enum apm_dmic_rate adc_i2s_dmic_rate;
	enum apm_adc_gain_coupling gain_coupling;
	uint8_t left_chan_gain;
	uint8_t right_chan_gain;
};
struct apm_auto_gain_config {
	int stereo_enable;
	enum apm_adc_target_out_level agc_target;
	int nois_gate_en;
	enum apm_noise_gate_threshold nois_gate_thold;
	enum apm_agc_adj_hold_time hold_time;
	enum apm_gain_ramp_time attack_time;
	enum apm_gain_ramp_time decay_time;
	enum apm_gain_values gain_max;
	enum apm_gain_values gain_min;
};

/*****************************************************************************/
/* IC specific low-level driver */
enum wov_modes;
/**
 * Sets the RAM ADC DMIC rate.
 *
 * @param   rate      - ADC digital microphone rate
 * @return  None
 */
void apm_set_adc_ram_dmic_config(enum apm_dmic_rate rate);

/**
 * Gets the RAM ADC DMIC rate.
 *
 * @param   None
 * @return  ADC digital microphone rate code.
 */
enum apm_dmic_rate apm_get_adc_ram_dmic_rate(void);

/**
 * Sets the ADC I2S DMIC rate.
 *
 * @param   rate      - ADC digital microphone rate
 * @return  None
 */
void apm_set_adc_i2s_dmic_config(enum apm_dmic_rate rate);

/**
 * Gets the ADC I2S DMIC rate.
 *
 * @param   None
 * @return  ADC digital microphone rate code.
 */
enum apm_dmic_rate apm_get_adc_i2s_dmic_rate(void);

/**
 * Sets VAD DMIC rate.
 *
 * @param   rate    - VAD DMIC rate
 *
 * @return  None
 */
void apm_set_vad_dmic_rate(enum apm_dmic_rate rate);

/**
 * Gets VAD DMIC rate.
 *
 * @param   None
 *
 * @return  ADC digital microphone rate code.
 */
enum apm_dmic_rate apm_get_vad_dmic_rate(void);

/**
 * Gets the ADC DMIC rate.
 *
 * @param   None
 * @return  ADC digital microphone rate code.
 */
enum apm_dmic_rate apm_get_adc_dmic_rate(void);

/**
 * Initiate APM module local parameters..
 *
 * @param   enable - enabled flag, 1 means enable
 * @return  None
 */
void apm_init(void);

/**
 * Enables/Disables APM  module.
 *
 * @param   enable - enabled flag, true means enable
 * @return  None
 */
void apm_enable(int enable);

/**
 * Enables/Disables voice activity detected interrupt.
 *
 * @param   enable - enabled flag, true means enable
 * @return  APM interrupt mode.
 */
void apm_enable_vad_interrupt(int enable);

/**
 * Enables/Disables ADC.
 *
 * @param   enable - enabled flag, true means enable
 * @return  None
 */
void apm_adc_enable(int enable);

/**
 * sets the ADC frequency.
 *
 * @param   adc_freq    - ADC frequency.
 * @return  None
 */
void apm_adc_set_freq(enum apm_adc_frequency adc_freq);

/**
 * Configures the ADC.
 *
 * @param   hpf_enable  - High pass filter enabled flag, true means enable
 * @param   filter_mode - ADC wind noise filter mode.
 * @param   adc_freq    - ADC frequency.
 * @return  None
 */
void apm_adc_config(int hpf_enable,
		enum apm_adc_wind_noise_filter_mode filter_mode,
		enum apm_adc_frequency adc_freq);

/**
 * Enables/Disables Digital Microphone.
 *
 * @param   enable - enabled flag, true means enable
 * @return  None
 */
void apm_dmic_enable(int enable);

/**
 * Configures Digital Microphone.
 *
 * @param   mix_left  - Mixer left channel output selection on ADC path.
 * @param   mix_right - Mixer right channel output selection on ADC path.
 * @return  None
 */
void apm_digital_mixer_config(enum apm_dig_mix mix_left,
		enum apm_dig_mix mix_right);

/**
 * Enables/Disables the VAD functionality.
 *
 * @param   enable - enabled flag, true means enable
 * @return  None
 */
void apm_vad_enable(int enable);

/**
 * Enables/Disables VAD ADC wakeup
 *
 * @param   enable - true enable, false disable.
 *
 * @return  None
 */
void apm_vad_adc_wakeup_enable(int enable);

/**
 * Sets VAD Input chanel.
 *
 * @param   chan_src	- Processed digital microphone channel
 *			  selection.
 * @return  None
 */
void apm_set_vad_input_channel(enum apm_vad_in_channel_src chan_src);

/**
 * Sets VAD sensitivity.
 *
 * @param   sensitivity_db - VAD sensitivity in db.
 * @return  None
 */
void apm_set_vad_sensitivity(uint8_t sensitivity_db);

/**
 * Gets VAD sensitivity.
 *
 * @param   None.
 * @return  VAD sensitivity in db
 */
uint8_t apm_get_vad_sensitivity(void);

/**
 * Restarts VAD functionality.
 *
 * @param   None
 * @return  None
 */
void apm_vad_restart(void);

/**
 * Restarts VAD functionality.
 *
 * @param   gain_coupling   - ADC digital gain coupling (independent or
 *                            rigth tracks left).
 * @param   left_chan_gain  - Left channel ADC digital gain programming value.
 * @param   right_chan_gain - Right channel ADC digital gain programming value.
 * @return  EC_ERROR_INVAL or EC_SUCCESS
 */
enum ec_error_list apm_adc_gain_config(enum apm_adc_gain_coupling gain_coupling,
			uint8_t left_chan_gain, uint8_t right_chan_gain);

/**
 * Enables/Disables the automatic gain.
 *
 * @param   enable - enabled flag, true means enable
 * @return  None
 */
void apm_auto_gain_cntrl_enable(int enable);

/**
 * Enables/Disables the automatic gain.
 *
 * @param   gain_cfg - struct of apm auto gain config
 * @return  EC_ERROR_INVAL or EC_SUCCESS
 */
enum ec_error_list apm_adc_auto_gain_config(
				struct apm_auto_gain_config *gain_cfg);

/**
 * Sets APM mode (enables & disables APN sub modules accordingly
 * to the APM mode).
 *
 * @param apm_mode - APM mode, DEFAULT, DETECTION, RECORD or INDEPENDENT modes.
 * @return  None
 */
void apm_set_mode(enum wov_modes wov_mode);

/**
 * Clears VAD detected bit in IFR register.
 *
 * @param   None
 * @return  None.
 */
void apm_clear_vad_detected_bit(void);

#endif /* __CROS_EC_APM_CHIP_H */
