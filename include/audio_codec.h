/*
 * Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_AUDIO_CODEC_H
#define __CROS_EC_AUDIO_CODEC_H

#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Common abstract layer
 */

/*
 * Checks capability of audio codec.
 *
 * @cap is an integer from enum ec_codec_cap.  Note that it represents a
 * bit field in a 32-bit integer.  The valid range is [0, 31].
 *
 * Returns:
 *   1 if audio codec capabilities include cap passed as parameter.
 *   0 if not capable.
 */
int audio_codec_capable(uint8_t cap);

/*
 * Registers shared memory (SHM).
 *
 * @shm_id is a SHM identifier from enum ec_codec_shm_id.
 * @cap is an integer from enum ec_codec_cap.
 * @addr is the address pointer to the SHM.
 * @len is the maximum length of the SHM.
 * @type is an integer from enum ec_codec_shm_type.
 *
 * Returns:
 *   EC_SUCCESS if success.
 *   EC_ERROR_UNKNOWN if internal errors.
 *   EC_ERROR_INVAL if invalid shm_id.
 *   EC_ERROR_INVAL if invalid cap.
 *   EC_ERROR_BUSY if the shm_id has been registered.
 */
int audio_codec_register_shm(uint8_t shm_id, uint8_t cap, uintptr_t *addr,
			     uint32_t len, uint8_t type);

/*
 * Translates the physical address from AP to EC's memory space.  Required if
 * wants to use AP SHM.
 *
 * @ap_addr is physical address from AP.
 * @ec_addr is the translation destination.
 *
 * Returns:
 *   EC_SUCCESS if success.
 *   EC_ERROR_UNKNOWN if internal errors.
 *   EC_ERROR_UNIMPLEMENTED if no concrete implementation.
 */
int audio_codec_memmap_ap_to_ec(uintptr_t ap_addr, uintptr_t *ec_addr);

/*
 * Scales a S16_LE sample by multiplying scalar.
 */
int16_t audio_codec_s16_scale_and_clip(int16_t orig, uint8_t scalar);

/*
 * DMIC abstract layer
 */

/*
 * Gets the maximum possible gain value.  All channels share the same maximum
 * gain value [0, max].
 *
 * The gain has no unit and should fit in a scale to represent relative dB.
 *
 * For example, suppose maximum possible gain value is 4, one could define a
 * mapping:
 * - 0 => -10 dB
 * - 1 => -5 dB
 * - 2 => 0 dB
 * - 3 => 5 dB
 * - 4 => 10 dB
 *
 * @max_gain is the destination address to put the gain value.
 *
 * Returns:
 *   EC_SUCCESS if success.
 *   EC_ERROR_UNKNOWN if internal error.
 */
int audio_codec_dmic_get_max_gain(uint8_t *max_gain);

/*
 * Sets the microphone gain for the specified channel.
 *
 * @channel is an integer from enum ec_codec_dmic_channel.  The valid range
 * is [0, 7].
 * @gain is the target gain for the specified channel.  The valid range
 * is [0, max_gain].  See also audio_codec_dmic_get_max_gain.
 *
 * Returns:
 *   EC_SUCCESS if success.
 *   EC_ERROR_UNKNOWN if internal error.
 *   EC_ERROR_INVAL if channel does not look good.
 *   EC_ERROR_INVAL if gain does not look good.
 */
int audio_codec_dmic_set_gain_idx(uint8_t channel, uint8_t gain);

/*
 * Gets the microphone gain of the specified channel.
 *
 * @channel is an integer from enum ec_codec_dmic_channel.  The valid range
 * is [0, 7].
 * @gain is the destination address to put the gain value of the channel.
 *
 * Returns:
 *   EC_SUCCESS if success.
 *   EC_ERROR_UNKNOWN if internal error.
 *   EC_ERROR_INVAL if channel does not look good.
 */
int audio_codec_dmic_get_gain_idx(uint8_t channel, uint8_t *gain);

/*
 * I2S RX abstract layer
 */

/*
 * Enables I2S RX.
 *
 * Returns:
 *   EC_SUCCESS if success.
 *   EC_ERROR_UNKNOWN if internal error.
 *   EC_ERROR_BUSY if has enabled.
 */
int audio_codec_i2s_rx_enable(void);

/*
 * Disables I2S RX.
 *
 * Returns:
 *   EC_SUCCESS if success.
 *   EC_ERROR_UNKNOWN if internal error.
 *   EC_ERROR_BUSY if has not enabled.
 */
int audio_codec_i2s_rx_disable(void);

/*
 * Sets I2S RX sample depth.
 *
 * @depth is an integer from enum ec_codec_i2s_rx_sample_depth.
 *
 * Returns:
 *   EC_SUCCESS if success.
 *   EC_ERROR_UNKNOWN if internal error.
 *   EC_ERROR_INVAL if depth does not look good.
 */
int audio_codec_i2s_rx_set_sample_depth(uint8_t depth);

/*
 * Sets I2S RX DAI format.
 *
 * @daifmt is an integer from enum ec_codec_i2s_rx_daifmt.
 *
 * Returns:
 *   EC_SUCCESS if success.
 *   EC_ERROR_UNKNOWN if internal error.
 *   EC_ERROR_INVAL if daifmt does not look good.
 */
int audio_codec_i2s_rx_set_daifmt(uint8_t daifmt);

/*
 * Sets I2S RX BCLK.
 *
 * @bclk is an integer to represent the bit clock rate.
 *
 * Returns:
 *   EC_SUCCESS if success.
 *   EC_ERROR_UNKNOWN if internal error.
 *   EC_ERROR_INVAL if bclk does not look good.
 */
int audio_codec_i2s_rx_set_bclk(uint32_t bclk);

/*
 * WoV abstract layer
 */

/*
 * Enables WoV.
 *
 * Returns:
 *   EC_SUCCESS if success.
 *   EC_ERROR_UNKNOWN if internal error.
 *   EC_ERROR_BUSY if has enabled.
 */
int audio_codec_wov_enable(void);

/*
 * Disables WoV.
 *
 * Returns:
 *   EC_SUCCESS if success.
 *   EC_ERROR_UNKNOWN if internal error.
 *   EC_ERROR_BUSY if has not enabled.
 */
int audio_codec_wov_disable(void);

/*
 * Reads the WoV audio data from chip.
 *
 * @buf is the target pointer to put the data.
 * @count is the maximum number of bytes to read.
 *
 * Returns:
 *   -1 if any errors.
 *   0 if no data.
 *   >0 if success.  The returned value denotes number of bytes read.
 */
int32_t audio_codec_wov_read(void *buf, uint32_t count);

/*
 * Enables notification if WoV audio data is available.
 *
 * Returns:
 *   EC_SUCCESS if success.
 *   EC_ERROR_UNKNOWN if internal error.
 *   EC_ERROR_BUSY if has enabled.
 *   EC_ERROR_ACCESS_DENIED if the notifiee has not set.
 */
int audio_codec_wov_enable_notifier(void);

/*
 * Disables WoV data notification.
 *
 * Returns:
 *   EC_SUCCESS if success.
 *   EC_ERROR_UNKNOWN if internal error.
 *   EC_ERROR_BUSY if has not enabled.
 *   EC_ERROR_ACCESS_DENIED if the notifiee has not set.
 */
int audio_codec_wov_disable_notifier(void);

/*
 * Audio buffer for 2 seconds S16_LE, 16kHz, mono.
 */
extern uintptr_t audio_codec_wov_audio_buf_addr;

/*
 * Language model buffer for speech-micro.  At least 67KB.
 */
extern uintptr_t audio_codec_wov_lang_buf_addr;

/*
 * Task for running WoV.
 */
void audio_codec_wov_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif
