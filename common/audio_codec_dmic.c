/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "audio_codec.h"
#include "console.h"
#include "host_command.h"

#define CPRINTS(format, args...) cprints(CC_AUDIO_CODEC, format, ## args)

static enum ec_status dmic_get_max_gain(struct host_cmd_handler_args *args)
{
	struct ec_response_ec_codec_dmic_get_max_gain *r = args->response;

	if (audio_codec_dmic_get_max_gain(&r->max_gain) != EC_SUCCESS)
		return EC_RES_ERROR;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}

static enum ec_status dmic_set_gain_idx(struct host_cmd_handler_args *args)
{
	const struct ec_param_ec_codec_dmic *p = args->params;

	if (audio_codec_dmic_set_gain_idx(
			p->set_gain_idx_param.channel,
			p->set_gain_idx_param.gain) != EC_SUCCESS)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}

static enum ec_status dmic_get_gain_idx(struct host_cmd_handler_args *args)
{
	const struct ec_param_ec_codec_dmic *p = args->params;
	struct ec_response_ec_codec_dmic_get_gain_idx *r = args->response;

	if (audio_codec_dmic_get_gain_idx(
			p->get_gain_idx_param.channel, &r->gain) != EC_SUCCESS)
		return EC_RES_ERROR;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}

static enum ec_status (*sub_cmds[])(struct host_cmd_handler_args *) = {
	[EC_CODEC_DMIC_GET_MAX_GAIN] = dmic_get_max_gain,
	[EC_CODEC_DMIC_SET_GAIN_IDX] = dmic_set_gain_idx,
	[EC_CODEC_DMIC_GET_GAIN_IDX] = dmic_get_gain_idx,
};

#ifdef DEBUG_AUDIO_CODEC
static char *strcmd[] = {
	[EC_CODEC_DMIC_GET_MAX_GAIN] = "EC_CODEC_DMIC_GET_MAX_GAIN",
	[EC_CODEC_DMIC_SET_GAIN_IDX] = "EC_CODEC_DMIC_SET_GAIN_IDX",
	[EC_CODEC_DMIC_GET_GAIN_IDX] = "EC_CODEC_DMIC_GET_GAIN_IDX",
};
BUILD_ASSERT(ARRAY_SIZE(sub_cmds) == ARRAY_SIZE(strcmd));
#endif

static enum ec_status dmic_host_command(struct host_cmd_handler_args *args)
{
	const struct ec_param_ec_codec_dmic *p = args->params;

#ifdef DEBUG_AUDIO_CODEC
	CPRINTS("DMIC subcommand: %s", strcmd[p->cmd]);
#endif

	if (p->cmd < EC_CODEC_DMIC_SUBCMD_COUNT)
		return sub_cmds[p->cmd](args);

	return EC_RES_INVALID_PARAM;
}
DECLARE_HOST_COMMAND(EC_CMD_EC_CODEC_DMIC, dmic_host_command, EC_VER_MASK(0));

#ifdef CONFIG_AUDIO_CODEC_DMIC_SOFTWARE_GAIN
static uint8_t channel_gains[EC_CODEC_DMIC_CHANNEL_COUNT];

int audio_codec_dmic_get_max_gain(uint8_t *gain)
{
	*gain = CONFIG_AUDIO_CODEC_DMIC_MAX_SOFTWARE_GAIN;
	return EC_SUCCESS;
}

int audio_codec_dmic_set_gain_idx(uint8_t channel, uint8_t gain)
{
	if (channel >= ARRAY_SIZE(channel_gains))
		return EC_ERROR_INVAL;
	if (gain > CONFIG_AUDIO_CODEC_DMIC_MAX_SOFTWARE_GAIN)
		return EC_ERROR_INVAL;

	channel_gains[channel] = gain;
	return EC_SUCCESS;
}

int audio_codec_dmic_get_gain_idx(uint8_t channel, uint8_t *gain)
{
	if (channel >= ARRAY_SIZE(channel_gains))
		return EC_ERROR_INVAL;

	*gain = channel_gains[channel];
	return EC_SUCCESS;
}
#endif
