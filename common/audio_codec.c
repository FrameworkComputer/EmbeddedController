/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "audio_codec.h"
#include "console.h"
#include "host_command.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_AUDIO_CODEC, format, ## args)

static const uint32_t capabilities =
	0
#ifdef CONFIG_AUDIO_CODEC_CAP_WOV_AUDIO_SHM
	| BIT(EC_CODEC_CAP_WOV_AUDIO_SHM)
#endif
#ifdef CONFIG_AUDIO_CODEC_CAP_WOV_LANG_SHM
	| BIT(EC_CODEC_CAP_WOV_LANG_SHM)
#endif
	;

static struct {
	uint8_t cap;
	uint8_t type;
	uintptr_t *addr;
	uint32_t len;
} shms[EC_CODEC_SHM_ID_LAST];

static enum ec_status get_capabilities(struct host_cmd_handler_args *args)
{
	struct ec_response_ec_codec_get_capabilities *r = args->response;

	r->capabilities = capabilities;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}

static enum ec_status get_shm_addr(struct host_cmd_handler_args *args)
{
	const struct ec_param_ec_codec *p = args->params;
	struct ec_response_ec_codec_get_shm_addr *r = args->response;
	const uint8_t shm_id = p->get_shm_addr_param.shm_id;

	if (shm_id >= EC_CODEC_SHM_ID_LAST)
		return EC_RES_INVALID_PARAM;
	if (!shms[shm_id].addr || !audio_codec_capable(shms[shm_id].cap))
		return EC_RES_INVALID_PARAM;
	if (!*shms[shm_id].addr &&
	    shms[shm_id].type == EC_CODEC_SHM_TYPE_EC_RAM)
		return EC_RES_ERROR;

	r->len = shms[shm_id].len;
	r->type = shms[shm_id].type;
	r->phys_addr = *shms[shm_id].addr;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}

static enum ec_status set_shm_addr(struct host_cmd_handler_args *args)
{
	const struct ec_param_ec_codec *p = args->params;
	const uint8_t shm_id = p->set_shm_addr_param.shm_id;
	uintptr_t ap_addr, ec_addr;

	if (shm_id >= EC_CODEC_SHM_ID_LAST)
		return EC_RES_INVALID_PARAM;
	if (!shms[shm_id].addr || !audio_codec_capable(shms[shm_id].cap))
		return EC_RES_INVALID_PARAM;
	if (p->set_shm_addr_param.len < shms[shm_id].len)
		return EC_RES_INVALID_PARAM;
	if (*shms[shm_id].addr)
		return EC_RES_BUSY;

	ap_addr = (uintptr_t)p->set_shm_addr_param.phys_addr;
	if (audio_codec_memmap_ap_to_ec(ap_addr, &ec_addr) != EC_SUCCESS)
		return EC_RES_ERROR;
	*shms[shm_id].addr = ec_addr;

	args->response_size = 0;
	return EC_RES_SUCCESS;
}

static enum ec_status (*sub_cmds[])(struct host_cmd_handler_args *) = {
	[EC_CODEC_GET_CAPABILITIES] = get_capabilities,
	[EC_CODEC_GET_SHM_ADDR] = get_shm_addr,
	[EC_CODEC_SET_SHM_ADDR] = set_shm_addr,
};

#ifdef DEBUG_AUDIO_CODEC
static char *strcmd[] = {
	[EC_CODEC_GET_CAPABILITIES] = "EC_CODEC_GET_CAPABILITIES",
	[EC_CODEC_GET_SHM_ADDR] = "EC_CODEC_GET_SHM_ADDR",
	[EC_CODEC_SET_SHM_ADDR] = "EC_CODEC_SET_SHM_ADDR",
};
BUILD_ASSERT(ARRAY_SIZE(sub_cmds) == ARRAY_SIZE(strcmd));
#endif

static enum ec_status host_command(struct host_cmd_handler_args *args)
{
	const struct ec_param_ec_codec *p = args->params;

#ifdef DEBUG_AUDIO_CODEC
	CPRINTS("subcommand: %s", strcmd[p->cmd]);
#endif

	if (p->cmd < EC_CODEC_SUBCMD_COUNT)
		return sub_cmds[p->cmd](args);

	return EC_RES_INVALID_PARAM;
}
DECLARE_HOST_COMMAND(EC_CMD_EC_CODEC, host_command, EC_VER_MASK(0));

/*
 * Exported interfaces.
 */
int audio_codec_capable(uint8_t cap)
{
	return capabilities & BIT(cap);
}

int audio_codec_register_shm(uint8_t shm_id, uint8_t cap,
		uintptr_t *addr, uint32_t len, uint8_t type)
{
	if (shm_id >= EC_CODEC_SHM_ID_LAST)
		return EC_ERROR_INVAL;
	if (cap >= EC_CODEC_CAP_LAST)
		return EC_ERROR_INVAL;
	if (shms[shm_id].addr || shms[shm_id].len)
		return EC_ERROR_BUSY;

	shms[shm_id].cap = cap;
	shms[shm_id].addr = addr;
	shms[shm_id].len = len;
	shms[shm_id].type = type;

	return EC_SUCCESS;
}

__attribute__((weak))
int audio_codec_memmap_ap_to_ec(uintptr_t ap_addr, uintptr_t *ec_addr)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int16_t audio_codec_s16_scale_and_clip(int16_t orig, uint8_t scalar)
{
	int32_t val;

	val = (int32_t)orig * (int32_t)scalar;
	val = MIN(val, (int32_t)INT16_MAX);
	val = MAX(val, (int32_t)INT16_MIN);
	return val;
}
