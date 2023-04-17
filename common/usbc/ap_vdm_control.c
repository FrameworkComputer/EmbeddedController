/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * AP VDM control support
 * Note: this is mutually exclusive with EC VDM control
 */

#include "builtin/assert.h"
#include "compile_time_macros.h"
#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "queue.h"
#include "system.h"
#include "task.h"
#include "tcpm/tcpm.h"
#include "temp_sensor.h"
#include "usb_pd.h"
#include "usb_pd_dpm_sm.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#else
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

/*
 * VDM:Attention queue for boards using AP-driven VDMs
 *
 * Depth must be a power of 2, which is normally enforced by the queue init
 * code, but must be manually enforced here.
 */
#define DPM_ATTENTION_QUEUE_DEPTH 8
BUILD_ASSERT(POWER_OF_TWO(DPM_ATTENTION_QUEUE_DEPTH));

struct attention_queue_entry {
	int objects;
	uint32_t attention[PD_ATTENTION_MAX_VDO];
};

static struct {
	uint32_t vdm_reply[VDO_MAX_SIZE];
	uint8_t vdm_reply_cnt;
	enum tcpci_msg_type vdm_reply_type;
	struct queue attention_queue;
	struct queue_state queue_state;
	struct attention_queue_entry queue_buffer[DPM_ATTENTION_QUEUE_DEPTH];
	mutex_t queue_lock;
} ap_storage[CONFIG_USB_PD_PORT_MAX_COUNT];

#ifdef CONFIG_ZEPHYR
static int init_ap_vdm_mutexes(void)
{
	int port;

	for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++) {
		k_mutex_init(&ap_storage[port].queue_lock);
	}

	return 0;
}
SYS_INIT(init_ap_vdm_mutexes, POST_KERNEL, 50);
#endif /* CONFIG_ZEPHYR */

static void init_attention_queue_structs(void)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		ap_storage[i].attention_queue.state =
			&ap_storage[i].queue_state;
		ap_storage[i].attention_queue.policy = &queue_policy_null;
		ap_storage[i].attention_queue.buffer_units =
			DPM_ATTENTION_QUEUE_DEPTH;
		ap_storage[i].attention_queue.buffer_units_mask =
			DPM_ATTENTION_QUEUE_DEPTH - 1;
		ap_storage[i].attention_queue.unit_bytes =
			sizeof(struct attention_queue_entry);
		ap_storage[i].attention_queue.buffer =
			(uint8_t *)&ap_storage[i].queue_buffer[0];
	}
}
DECLARE_HOOK(HOOK_INIT, init_attention_queue_structs, HOOK_PRIO_FIRST);

void ap_vdm_attention_enqueue(int port, int length, uint32_t *buf)
{
	struct attention_queue_entry new_entry;

	new_entry.objects = length;
	memcpy(new_entry.attention, buf, length * sizeof(uint32_t));

	mutex_lock(&ap_storage[port].queue_lock);

	/* If the queue is already full, discard the last entry */
	if (queue_is_full(&ap_storage[port].attention_queue))
		queue_advance_head(&ap_storage[port].attention_queue, 1);

	/* Note: this should not happen, but log anyway */
	if (queue_add_unit(&ap_storage[port].attention_queue, &new_entry) == 0)
		CPRINTS("Error: Dropping port %d Attention", port);
	else
		pd_notify_event(port, PD_STATUS_EVENT_VDM_ATTENTION);

	mutex_unlock(&ap_storage[port].queue_lock);
}

static uint8_t ap_vdm_attention_pop(int port, uint32_t *buf,
				    uint8_t *items_left)
{
	int length = 0;
	struct attention_queue_entry popped_entry;

	mutex_lock(&ap_storage[port].queue_lock);

	if (!queue_is_empty(&ap_storage[port].attention_queue)) {
		queue_remove_unit(&ap_storage[port].attention_queue,
				  &popped_entry);

		length = popped_entry.objects;
		memcpy(buf, popped_entry.attention, length * sizeof(buf[0]));
	}
	*items_left = queue_count(&ap_storage[port].attention_queue);

	mutex_unlock(&ap_storage[port].queue_lock);

	return length;
}

void ap_vdm_init(int port)
{
	/* Clear any stored AP messages */
	ap_storage[port].vdm_reply_cnt = 0;
	queue_init(&ap_storage[port].attention_queue);
}

void ap_vdm_acked(int port, enum tcpci_msg_type type, int vdo_count,
		  uint32_t *vdm)
{
	assert(vdo_count >= 1);
	assert(vdo_count <= VDO_MAX_SIZE);

	/* Store and notify the AP */
	ap_storage[port].vdm_reply_cnt = vdo_count;
	memcpy(ap_storage[port].vdm_reply, vdm, vdo_count * sizeof(uint32_t));
	ap_storage[port].vdm_reply_type = type;
	pd_notify_event(port, PD_STATUS_EVENT_VDM_REQ_REPLY);

	/* Clear the flag now that reply fields are updated */
	dpm_clear_vdm_request(port);
}

void ap_vdm_naked(int port, enum tcpci_msg_type type, uint16_t svid,
		  uint8_t vdm_cmd, uint32_t vdm_header)
{
	/* Store and notify the AP */
	ap_storage[port].vdm_reply_type = type;

	if (vdm_header != 0) {
		ap_storage[port].vdm_reply_cnt = 1;
		ap_storage[port].vdm_reply[0] = vdm_header;
		pd_notify_event(port, PD_STATUS_EVENT_VDM_REQ_REPLY);
	} else {
		ap_storage[port].vdm_reply_cnt = 0;
		pd_notify_event(port, PD_STATUS_EVENT_VDM_REQ_FAILED);
	}

	/* Clear the flag now that reply fields are updated */
	dpm_clear_vdm_request(port);
}

static enum ec_status ap_vdm_copy_reply(int port, uint8_t *type, uint8_t *size,
					uint32_t *buf)
{
	if (dpm_check_vdm_request(port))
		return EC_RES_BUSY;

	if (ap_storage[port].vdm_reply_cnt == 0)
		return EC_RES_UNAVAILABLE;

	*type = ap_storage[port].vdm_reply_type;
	*size = ap_storage[port].vdm_reply_cnt;
	memcpy(buf, ap_storage[port].vdm_reply, *size * sizeof(uint32_t));

	return EC_RES_SUCCESS;
}

/* Feature-specific host commands */
static enum ec_status hc_typec_vdm_response(struct host_cmd_handler_args *args)
{
	const struct ec_params_typec_vdm_response *p = args->params;
	struct ec_response_typec_vdm_response *r = args->response;
	uint32_t data[VDO_MAX_SIZE];

	if (p->port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	if (args->response_max < sizeof(*r))
		return EC_RES_RESPONSE_TOO_BIG;

	args->response_size = sizeof(*r);

	r->vdm_response_err = ap_vdm_copy_reply(p->port, &r->partner_type,
						&r->vdm_data_objects, data);

	if (r->vdm_data_objects > 0)
		memcpy(r->vdm_response, data,
		       r->vdm_data_objects * sizeof(uint32_t));

	r->vdm_attention_objects =
		ap_vdm_attention_pop(p->port, data, &r->vdm_attention_left);
	if (r->vdm_attention_objects > 0)
		memcpy(r->vdm_attention, data,
		       r->vdm_attention_objects * sizeof(uint32_t));

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_TYPEC_VDM_RESPONSE, hc_typec_vdm_response,
		     EC_VER_MASK(0));
