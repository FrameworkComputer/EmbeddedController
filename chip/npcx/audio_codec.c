/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Audio codec interface for NPCX */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "wov_chip.h"

#define EC_WOV_I2S_SAMPLE_RATE 48000

typedef enum ec_status (*codec_i2s_func)(struct host_cmd_handler_args *args);

static enum ec_status codec_set_sample_depth(struct host_cmd_handler_args *args)
{
	struct ec_param_codec_i2s *param =
		(struct ec_param_codec_i2s *)args->params;
	/*
	 * The number of bits comes from the enum ec_sample_depth_value where
	 * the 1st value is for 16 bits and the 2nd value is for 24 bits.
	 */
	int bits_num = param->depth == EC_CODEC_SAMPLE_DEPTH_24 ? 24 : 16;

	args->response_size = 0;
	/* Sample depth can only be changed when mode is WOV_MODE_OFF */
	wov_set_mode(WOV_MODE_OFF);

	if (wov_set_sample_depth(bits_num))
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}

static enum ec_status codec_set_gain(struct host_cmd_handler_args *args)
{
	struct ec_param_codec_i2s *param =
		(struct ec_param_codec_i2s *)args->params;

	args->response_size = 0;
	wov_set_gain(param->gain.left, param->gain.right);

	return EC_RES_SUCCESS;
}

static enum ec_status codec_get_gain(struct host_cmd_handler_args *args)
{
	struct ec_codec_i2s_gain *resp =
		(struct ec_codec_i2s_gain *)args->response;
	int left_gain;
	int right_gain;

	args->response_size = sizeof(struct ec_codec_i2s_gain);
	/* Get the gains set in the wov driver */
	wov_get_gain(&left_gain, &right_gain);
	resp->left = left_gain;
	resp->right = right_gain;

	return EC_RES_SUCCESS;
}

static enum ec_status codec_i2s_enable(struct host_cmd_handler_args *args)
{
	struct ec_param_codec_i2s *param =
		(struct ec_param_codec_i2s *)args->params;
	int rv;

	args->response_size = 0;

	if (param->i2s_enable) {
		/*
		 * The mic source and sample rate don't need to be set each time
		 * an i2s stream is started, but the audio codec does not
		 * contain a method to select these as they must be the values
		 * set below for proper i2s operation. Since the default values
		 * set in wov.c are different than what's required, they are set
		 * each time an i2s stream is started.
		 */
		wov_set_mic_source(WOV_SRC_STEREO);
		/* Mode must be WOV_MODE_OFF to change sample rate */
		wov_set_mode(WOV_MODE_OFF);
		wov_set_sample_rate(EC_WOV_I2S_SAMPLE_RATE);
		/* Start i2s stream */
		rv = wov_set_mode(WOV_MODE_I2S);
	} else {
		/* Stop i2s stream */
		rv = wov_set_mode(WOV_MODE_OFF);
	}

	return rv ? EC_RES_ERROR : EC_RES_SUCCESS;
}

static enum ec_status codec_i2s_set_fmt(struct host_cmd_handler_args *args)
{
	struct ec_param_codec_i2s *param =
		(struct ec_param_codec_i2s *)args->params;

	args->response_size = 0;

	/* To change mode setting it must be set to WOV_MODE_OFF */
	wov_set_mode(WOV_MODE_OFF);
	wov_set_i2s_fmt(param->i2s_config);

	return EC_RES_SUCCESS;
}

static enum ec_status
codec_i2s_set_tdm_config(struct host_cmd_handler_args *args)
{
	struct ec_param_codec_i2s *param =
		(struct ec_param_codec_i2s *)args->params;
	uint32_t flags;

	args->response_size = 0;

	flags = (!!param->tdm_param.adjacent_to_ch0) |
		((!!param->tdm_param.adjacent_to_ch1) << 1);

	if (wov_set_i2s_tdm_config(param->tdm_param.ch0_delay,
				   param->tdm_param.ch1_delay, flags))
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}

static enum ec_status codec_i2s_set_bclk(struct host_cmd_handler_args *args)
{
	struct ec_param_codec_i2s *param =
		(struct ec_param_codec_i2s *)args->params;

	args->response_size = 0;
	/* To change bclk setting it must be set to WOV_MODE_OFF */
	wov_set_mode(WOV_MODE_OFF);
	wov_set_i2s_bclk(param->bclk);

	return EC_RES_SUCCESS;
}

static enum ec_status codec_i2s(struct host_cmd_handler_args *args)
{
	struct ec_param_codec_i2s *param =
		(struct ec_param_codec_i2s *)args->params;

	codec_i2s_func codec_i2s_func_tbl[] = {
		codec_set_sample_depth,
		codec_set_gain,
		codec_get_gain,
		codec_i2s_enable,
		codec_i2s_set_fmt,
		codec_i2s_set_tdm_config,
		codec_i2s_set_bclk,
	};

	if (param->cmd < EC_CODEC_I2S_SUBCMD_COUNT)
		return codec_i2s_func_tbl[param->cmd](args);
	else
		return EC_RES_INVALID_PARAM;
}

DECLARE_HOST_COMMAND(EC_CMD_CODEC_I2S,
		     codec_i2s,
		     EC_VER_MASK(0));
