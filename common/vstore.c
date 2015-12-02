/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Temporary secure storage commands for use by the host for verified boot
 * related activities such as storing the hash of verified firmware for use
 * in suspend/resume.
 *
 * There are a configurable number of vstore slots, with all slots having
 * the same size of EC_VSTORE_SLOT_SIZE (64 bytes).
 *
 * Slots can be written once per AP power-on and will then be locked and
 * cannot be written again until it is cleared in the CHIPSET_SHUTDOWN
 * or CHIPSET_RESET hooks.
 */

#include "common.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"
#include "util.h"

#define VSTORE_SYSJUMP_TAG  0x5653 /* "VS" */
#define VSTORE_HOOK_VERSION 1

struct vstore_slot {
	uint8_t locked;
	uint8_t data[EC_VSTORE_SLOT_SIZE];
};

static struct vstore_slot vstore_slots[CONFIG_VSTORE_SLOT_COUNT];
static const int vstore_size =
		sizeof(struct vstore_slot) * CONFIG_VSTORE_SLOT_COUNT;
BUILD_ASSERT(ARRAY_SIZE(vstore_slots) <= EC_VSTORE_SLOT_MAX);

/*
 * vstore_info - Get slot count and mask of locked slots.
 */
static int vstore_info(struct host_cmd_handler_args *args)
{
	struct ec_response_vstore_info *r = args->response;
	int i;

	r->slot_count = CONFIG_VSTORE_SLOT_COUNT;
	r->slot_locked = 0;
	for (i = 0; i < CONFIG_VSTORE_SLOT_COUNT; i++)
		if (vstore_slots[i].locked)
			r->slot_locked |= 1 << i;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_VSTORE_INFO, vstore_info, EC_VER_MASK(0));

/*
 * vstore_read - Read slot from temporary secure storage.
 *
 * Response is EC_VSTORE_SLOT_SIZE bytes of data.
 */
static int vstore_read(struct host_cmd_handler_args *args)
{
	const struct ec_params_vstore_read *p = args->params;
	struct ec_response_vstore_read *r = args->response;

	if (p->slot >= CONFIG_VSTORE_SLOT_COUNT)
		return EC_RES_INVALID_PARAM;

	memcpy(r->data, vstore_slots[p->slot].data, EC_VSTORE_SLOT_SIZE);

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_VSTORE_READ, vstore_read, EC_VER_MASK(0));

/*
 * vstore_write - Write temporary secure storage slot and lock it.
 */
static int vstore_write(struct host_cmd_handler_args *args)
{
	const struct ec_params_vstore_write *p = args->params;
	struct vstore_slot *slot;

	if (p->slot >= CONFIG_VSTORE_SLOT_COUNT)
		return EC_RES_INVALID_PARAM;
	slot = &vstore_slots[p->slot];

	if (slot->locked)
		return EC_RES_ACCESS_DENIED;
	slot->locked = 1;
	memcpy(slot->data, p->data, EC_VSTORE_SLOT_SIZE);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_VSTORE_WRITE, vstore_write, EC_VER_MASK(0));

static void vstore_clear_lock(void)
{
	int i;

	for (i = 0; i < CONFIG_VSTORE_SLOT_COUNT; i++)
		vstore_slots[i].locked = 0;
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, vstore_clear_lock, HOOK_PRIO_DEFAULT);

static void vstore_preserve_state(void)
{
	system_add_jump_tag(VSTORE_SYSJUMP_TAG, VSTORE_HOOK_VERSION,
			    vstore_size, vstore_slots);
}
DECLARE_HOOK(HOOK_SYSJUMP, vstore_preserve_state, HOOK_PRIO_DEFAULT);

static void vstore_init(void)
{
	const struct vstore_slot *prev;
	int version, size;

	prev = (const struct vstore_slot *)system_get_jump_tag(
		VSTORE_SYSJUMP_TAG, &version, &size);

	if (prev && version == VSTORE_HOOK_VERSION && size == vstore_size)
		memcpy(vstore_slots, prev, vstore_size);
}
DECLARE_HOOK(HOOK_INIT, vstore_init, HOOK_PRIO_DEFAULT);
