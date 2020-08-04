/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "audio_codec.h"
#include "console.h"
#include "host_command.h"

#define CPRINTS(format, args...) cprints(CC_AUDIO_CODEC, format, ## args)

static uint8_t i2s_rx_enabled;

static enum ec_status i2s_rx_enable(struct host_cmd_handler_args *args)
{
	if (i2s_rx_enabled)
		return EC_RES_BUSY;

	if (audio_codec_i2s_rx_enable() != EC_SUCCESS)
		return EC_RES_ERROR;

	i2s_rx_enabled = 1;

	return EC_RES_SUCCESS;
}

static enum ec_status i2s_rx_disable(struct host_cmd_handler_args *args)
{
	if (!i2s_rx_enabled)
		return EC_RES_BUSY;

	if (audio_codec_i2s_rx_disable() != EC_SUCCESS)
		return EC_RES_ERROR;

	i2s_rx_enabled = 0;

	return EC_RES_SUCCESS;
}

static enum ec_status
i2s_rx_set_sample_depth(struct host_cmd_handler_args *args)
{
	const struct ec_param_ec_codec_i2s_rx *p = args->params;
	const uint8_t depth = p->set_sample_depth_param.depth;

	if (i2s_rx_enabled)
		return EC_RES_BUSY;
	if (depth >= EC_CODEC_I2S_RX_SAMPLE_DEPTH_COUNT)
		return EC_RES_INVALID_PARAM;

	if (audio_codec_i2s_rx_set_sample_depth(depth) != EC_SUCCESS)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}

static enum ec_status i2s_rx_set_daifmt(struct host_cmd_handler_args *args)
{
	const struct ec_param_ec_codec_i2s_rx *p = args->params;
	const uint8_t daifmt = p->set_daifmt_param.daifmt;

	if (i2s_rx_enabled)
		return EC_RES_BUSY;
	if (daifmt >= EC_CODEC_I2S_RX_DAIFMT_COUNT)
		return EC_RES_INVALID_PARAM;

	if (audio_codec_i2s_rx_set_daifmt(daifmt) != EC_SUCCESS)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}

static enum ec_status i2s_rx_set_bclk(struct host_cmd_handler_args *args)
{
	const struct ec_param_ec_codec_i2s_rx *p = args->params;

	if (i2s_rx_enabled)
		return EC_RES_BUSY;

	if (audio_codec_i2s_rx_set_bclk(p->set_bclk_param.bclk) != EC_SUCCESS)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}

static enum ec_status i2s_rx_reset(struct host_cmd_handler_args *args)
{
	if (audio_codec_i2s_rx_disable() != EC_SUCCESS)
		return EC_RES_ERROR;

	i2s_rx_enabled = 0;

	return EC_RES_SUCCESS;
}

static enum ec_status (*sub_cmds[])(struct host_cmd_handler_args *) = {
	[EC_CODEC_I2S_RX_ENABLE] = i2s_rx_enable,
	[EC_CODEC_I2S_RX_DISABLE] = i2s_rx_disable,
	[EC_CODEC_I2S_RX_SET_SAMPLE_DEPTH] = i2s_rx_set_sample_depth,
	[EC_CODEC_I2S_RX_SET_DAIFMT] = i2s_rx_set_daifmt,
	[EC_CODEC_I2S_RX_SET_BCLK] = i2s_rx_set_bclk,
	[EC_CODEC_I2S_RX_RESET] = i2s_rx_reset,
};

#ifdef DEBUG_AUDIO_CODEC
static char *strcmd[] = {
	[EC_CODEC_I2S_RX_ENABLE] = "EC_CODEC_I2S_RX_ENABLE",
	[EC_CODEC_I2S_RX_DISABLE] = "EC_CODEC_I2S_RX_DISABLE",
	[EC_CODEC_I2S_RX_SET_SAMPLE_DEPTH] = "EC_CODEC_I2S_RX_SET_SAMPLE_DEPTH",
	[EC_CODEC_I2S_RX_SET_DAIFMT] = "EC_CODEC_I2S_RX_SET_DAIFMT",
	[EC_CODEC_I2S_RX_SET_BCLK] = "EC_CODEC_I2S_RX_SET_BCLK",
	[EC_CODEC_I2S_RX_RESET] = "EC_CODEC_I2S_RESET",
};
BUILD_ASSERT(ARRAY_SIZE(sub_cmds) == ARRAY_SIZE(strcmd));
#endif

static enum ec_status i2s_rx_host_command(struct host_cmd_handler_args *args)
{
	const struct ec_param_ec_codec_i2s_rx *p = args->params;

#ifdef DEBUG_AUDIO_CODEC
	CPRINTS("I2S RX subcommand: %s", strcmd[p->cmd]);
#endif

	if (p->cmd < EC_CODEC_I2S_RX_SUBCMD_COUNT)
		return sub_cmds[p->cmd](args);

	return EC_RES_INVALID_PARAM;
}
DECLARE_HOST_COMMAND(EC_CMD_EC_CODEC_I2S_RX,
	i2s_rx_host_command, EC_VER_MASK(0));
