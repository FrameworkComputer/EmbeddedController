/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "compile_time_macros.h"
#include "console.h"
#include "heci_client.h"
#include "hid_device.h"
#include "util.h"

#ifdef HID_SUBSYS_DEBUG
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_LPC, format, ## args)
#else
#define CPUTS(outstr)
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

#define __packed __attribute__((packed))

#define HECI_CLIENT_HID_GUID { 0x33AECD58, 0xB679, 0x4E54,\
			 { 0x9B, 0xD9, 0xA0, 0x4D, 0x34, 0xF0, 0xC2, 0x26 } }

#define HID_SUBSYS_MAX_HID_DEVICES			3

/*
 * the following enum values and data structures with __packed are used for
 * communicating with host driver and they are copied from host driver.
 */
enum {
	HID_GET_HID_DESCRIPTOR = 0,
	HID_GET_REPORT_DESCRIPTOR,
	HID_GET_FEATURE_REPORT,
	HID_SET_FEATURE_REPORT,
	HID_GET_INPUT_REPORT,
	HID_PUBLISH_INPUT_REPORT,
	HID_PUBLISH_INPUT_REPORT_LIST, /* TODO: need to support batch report */

	HID_HID_CLIENT_READY_CMD = 30,
	HID_HID_COMMAND_MAX = 31,

	HID_DM_COMMAND_BASE,
	HID_DM_ENUM_DEVICES,
	HID_DM_ADD_DEVICE,
	HID_COMMAND_LAST
};

struct hid_device_info {
	uint32_t dev_id;
	uint8_t dev_class;
	uint16_t pid;
	uint16_t vid;
} __packed;

struct hid_enum_payload	{
	uint8_t num_of_hid_devices;
	struct hid_device_info dev_info[0];
} __packed;

#define COMMAND_MASK			0x7F
#define RESPONSE_FLAG			0x80
struct hid_msg_hdr {
	uint8_t command; /* bit 7 is used to indicate "response" */
	uint8_t device_id;
	uint8_t status;
	uint8_t flags;
	uint16_t size;
} __packed;

struct hid_msg {
	struct hid_msg_hdr hdr;
	uint8_t payload[HID_SUBSYS_MAX_PAYLOAD_SIZE];
} __packed;

struct hid_subsys_hid_device {
	struct hid_device_info info;
	const struct hid_callbacks *cbs;
	int can_send_hid_input;

	void *data;
};

struct hid_subsystem {
	heci_handle_t heci_handle;

	uint32_t num_of_hid_devices;
	struct hid_subsys_hid_device hid_devices[HID_SUBSYS_MAX_HID_DEVICES];
};

static struct hid_subsystem hid_subsys_ctx = {
	.heci_handle = HECI_INVALID_HANDLE,
};

#define handle_to_dev_id(_handle)	((uintptr_t)(_handle))
#define dev_id_to_handle(_dev_id)	((hid_handle_t)(uintptr_t)(_dev_id))

static inline hid_handle_t device_index_to_handle(int device_index)
{
	return (hid_handle_t)(uintptr_t)(device_index + 1);
}

static inline int is_valid_handle(hid_handle_t handle)
{
	return (uintptr_t)handle > 0 &&
	       (uintptr_t)handle <= hid_subsys_ctx.num_of_hid_devices;
}

static inline
struct hid_subsys_hid_device *handle_to_hid_device(hid_handle_t handle)
{
	if (!is_valid_handle(handle))
		return NULL;

	return &hid_subsys_ctx.hid_devices[(uintptr_t)handle - 1];
}


hid_handle_t hid_subsys_register_device(const struct hid_device *dev_info)
{
	struct hid_subsys_hid_device *hid_device;
	hid_handle_t handle;
	int ret, hid_device_index;

	if (hid_subsys_ctx.num_of_hid_devices >= HID_SUBSYS_MAX_HID_DEVICES)
		return HID_INVALID_HANDLE;

	hid_device_index = hid_subsys_ctx.num_of_hid_devices++;

	handle = device_index_to_handle(hid_device_index);

	hid_device = &hid_subsys_ctx.hid_devices[hid_device_index];

	hid_device->info.dev_class = dev_info->dev_class;
	hid_device->info.pid = dev_info->pid;
	hid_device->info.vid = dev_info->vid;
	hid_device->info.dev_id = handle_to_dev_id(handle);

	hid_device->cbs = dev_info->cbs;

	if (dev_info->cbs->initialize) {
		ret = dev_info->cbs->initialize(handle);
		if (ret) {
			CPRINTF("initialize error %d\n", ret);
			hid_subsys_ctx.num_of_hid_devices--;
			return HID_INVALID_HANDLE;
		}
	}

	return handle;
}

int hid_subsys_send_input_report(const hid_handle_t handle, uint8_t *buf,
				 const size_t buf_size)
{
	struct hid_subsys_hid_device *hid_device;
	struct hid_msg_hdr hid_msg_hdr = {0};
	struct heci_msg_item msg_item[2];
	struct heci_msg_list msg_list;

	hid_device = handle_to_hid_device(handle);
	if (!hid_device)
		return -EC_ERROR_INVAL;

	if (buf_size > HID_SUBSYS_MAX_PAYLOAD_SIZE)
		return -EC_ERROR_OVERFLOW;

	if (hid_subsys_ctx.heci_handle == HECI_INVALID_HANDLE)
		return -HID_SUBSYS_ERR_NOT_READY;

	if (!hid_device->can_send_hid_input)
		return -HID_SUBSYS_ERR_NOT_READY;

	hid_msg_hdr.command = HID_PUBLISH_INPUT_REPORT;
	hid_msg_hdr.device_id = hid_device->info.dev_id;
	hid_msg_hdr.size = buf_size;

	msg_item[0].size = sizeof(hid_msg_hdr);
	msg_item[0].buf = (uint8_t *)&hid_msg_hdr;

	msg_item[1].size = buf_size;
	msg_item[1].buf = buf;

	msg_list.num_of_items = 2;
	msg_list.items[0] = &msg_item[0];
	msg_list.items[1] = &msg_item[1];

	heci_send_msgs(hid_subsys_ctx.heci_handle, &msg_list);

	return 0;
}

int hid_subsys_set_device_data(const hid_handle_t handle, void *data)
{
	struct hid_subsys_hid_device *hid_device;

	hid_device = handle_to_hid_device(handle);
	if (!hid_device)
		return -EC_ERROR_INVAL;

	hid_device->data = data;

	return 0;
}

void *hid_subsys_get_device_data(const hid_handle_t handle)
{
	struct hid_subsys_hid_device *hid_device;

	hid_device = handle_to_hid_device(handle);
	if (!hid_device)
		return NULL;

	return hid_device->data;
}

static int handle_hid_device_msg(struct hid_msg *hid_msg)
{
	int ret = 0, payload_size, buf_size;
	uint8_t *payload;
	struct hid_subsys_hid_device *hid_dev;
	const struct hid_callbacks *cbs;
	hid_handle_t handle;

	handle = dev_id_to_handle(hid_msg->hdr.device_id);
	hid_dev = handle_to_hid_device(handle);

	if (!hid_dev) {
		/*
		 * use HID_HID_COMMAND_MAX as error message.
		 * host driver will reset ISH.
		 */
		hid_msg->hdr.size = 0;
		hid_msg->hdr.status = 0;
		hid_msg->hdr.command |= RESPONSE_FLAG | HID_HID_COMMAND_MAX;
		hid_msg->hdr.flags = 0;

		heci_send_msg(hid_subsys_ctx.heci_handle, (uint8_t *)hid_msg,
			      sizeof(hid_msg->hdr));

		return 0;
	}

	cbs = hid_dev->cbs;

	payload = hid_msg->payload;
	payload_size = hid_msg->hdr.size; /* input data */
	buf_size = sizeof(hid_msg->payload); /* buffer to be written by cb */

	/*
	 * re-use hid_msg from host for reply.
	 */
	switch (hid_msg->hdr.command & COMMAND_MASK) {
	case  HID_GET_HID_DESCRIPTOR:
		if (cbs->get_hid_descriptor)
			ret = cbs->get_hid_descriptor(handle, payload,
						      buf_size);

		break;
	case HID_GET_REPORT_DESCRIPTOR:
		if (cbs->get_report_descriptor)
			ret = cbs->get_report_descriptor(handle, payload,
							 buf_size);

		hid_dev->can_send_hid_input = 1;

		break;

	case HID_GET_FEATURE_REPORT:
		if (cbs->get_feature_report)
			ret = cbs->get_feature_report(handle, payload[0],
						      payload, buf_size);

		break;

	case HID_SET_FEATURE_REPORT:
		if (cbs->set_feature_report) {
			ret = cbs->set_feature_report(handle,
						      payload[0],
						      payload,
						      payload_size);
			/*
			 * if no error, reply only with the report id.
			 * re-use the first byte of payload
			 * from host that has report id
			 */
			if (ret >= 0)
				ret = sizeof(uint8_t);
		}

		break;
	case HID_GET_INPUT_REPORT:
		if (cbs->get_input_report)
			ret = cbs->get_input_report(handle, payload[0],
						    payload, buf_size);

		break;

	default:
		CPRINTF("invalid hid command %d, ignoring request\n",
			hid_msg->hdr.command & COMMAND_MASK);
		ret = -1; /* send error */
	}

	if (ret > 0) {
		hid_msg->hdr.size = ret;
		hid_msg->hdr.status = 0;
	} else { /* error in callback */
		/*
		 * Note : errors of HID device should be transferred
		 * through their HID formatted data.
		 */
		hid_msg->hdr.size = 0;
		hid_msg->hdr.status = -ret;
	}

	hid_msg->hdr.command |= RESPONSE_FLAG;
	hid_msg->hdr.flags = 0;

	heci_send_msg(hid_subsys_ctx.heci_handle, (uint8_t *)hid_msg,
		      sizeof(hid_msg->hdr) + hid_msg->hdr.size);

	return 0;
}

static int handle_hid_subsys_msg(struct hid_msg *hid_msg)
{
	int size = 0, i;
	struct hid_enum_payload *enum_payload;

	switch (hid_msg->hdr.command & COMMAND_MASK) {
	case  HID_DM_ENUM_DEVICES:
		enum_payload = (struct hid_enum_payload *)hid_msg->payload;

		for (i = 0; i < hid_subsys_ctx.num_of_hid_devices; i++) {
			enum_payload->dev_info[i] =
					hid_subsys_ctx.hid_devices[i].info;
		}

		enum_payload->num_of_hid_devices =
					hid_subsys_ctx.num_of_hid_devices;

		/* reply payload size */
		size = sizeof(enum_payload->num_of_hid_devices);
		size += enum_payload->num_of_hid_devices *
					sizeof(enum_payload->dev_info[0]);

		break;

	default:
		CPRINTF("invalid hid command %d, ignoring request\n",
			hid_msg->hdr.command & COMMAND_MASK);
		size = -1; /* send error */
	}

	if (size > 0) {
		hid_msg->hdr.size = size;
		hid_msg->hdr.status = 0;
	} else { /* error in callback */
		hid_msg->hdr.size = 0;
		hid_msg->hdr.status = -size;
	}

	hid_msg->hdr.command |= RESPONSE_FLAG;
	hid_msg->hdr.flags = 0;

	heci_send_msg(hid_subsys_ctx.heci_handle, (uint8_t *)hid_msg,
		      sizeof(hid_msg->hdr) + hid_msg->hdr.size);

	return 0;
}

static void hid_subsys_new_msg_received(const heci_handle_t handle,
					uint8_t *msg, const size_t msg_size)
{
	struct hid_msg *hid_msg = (struct hid_msg *)msg;

	/* workaround, since Host driver doesn't set size properly */
	if (hid_msg->hdr.size == 0 && msg_size > sizeof(hid_msg->hdr))
		hid_msg->hdr.size = msg_size - sizeof(hid_msg->hdr);

	if (hid_msg->hdr.size > HID_SUBSYS_MAX_PAYLOAD_SIZE) {
		CPRINTF("too big payload size : %d. discard heci msg\n",
			hid_msg->hdr);
		return; /* invalid hdr. discard */
	}

	if (hid_msg->hdr.device_id)
		handle_hid_device_msg(hid_msg);
	else
		handle_hid_subsys_msg(hid_msg);
}

static int hid_subsys_initialize(const heci_handle_t heci_handle)
{
	hid_subsys_ctx.heci_handle = heci_handle;

	return 0;
}

/* return zero if resume request handled successfully */
static int hid_subsys_resume(const heci_handle_t heci_handle)
{
	int i, ret = 0;

	for (i = 0; i < hid_subsys_ctx.num_of_hid_devices; i++) {
		if (hid_subsys_ctx.hid_devices[i].cbs->resume)
			ret |= hid_subsys_ctx.hid_devices[i].cbs->resume(
					device_index_to_handle(i));
	}

	return ret;
}

/* return zero if suspend request handled successfully */
static int hid_subsys_suspend(const heci_handle_t heci_handle)
{
	int i, ret = 0;

	for (i = hid_subsys_ctx.num_of_hid_devices - 1; i >= 0; i--) {
		if (hid_subsys_ctx.hid_devices[i].cbs->suspend)
			ret |= hid_subsys_ctx.hid_devices[i].cbs->suspend(
					device_index_to_handle(i));
	}

	return ret;
}

static const struct heci_client_callbacks hid_subsys_heci_cbs = {
	.initialize = hid_subsys_initialize,
	.new_msg_received = hid_subsys_new_msg_received,
	.suspend = hid_subsys_suspend,
	.resume = hid_subsys_resume,
};

static const struct heci_client hid_subsys_heci_client = {
	.protocol_id = HECI_CLIENT_HID_GUID,
	.max_msg_size = HECI_MAX_MSG_SIZE,
	.protocol_ver = 1,
	.max_n_of_connections = 1,

	.cbs = &hid_subsys_heci_cbs,
};

HECI_CLIENT_ENTRY(hid_subsys_heci_client);
