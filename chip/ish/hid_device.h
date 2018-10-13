/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __HID_DEVICE_H
#define __HID_DEVICE_H

#include <stdint.h>
#include <stddef.h>

#include "hooks.h"

#define HID_SUBSYS_MAX_PAYLOAD_SIZE			4954

enum HID_SUBSYS_ERR {
	HID_SUBSYS_ERR_NOT_READY		= EC_ERROR_INTERNAL_FIRST + 0,
	HID_SUBSYS_ERR_TOO_MANY_HID_DEVICES	= EC_ERROR_INTERNAL_FIRST + 1,
};

typedef void *					hid_handle_t;
#define HID_INVALID_HANDLE			NULL

struct hid_callbacks {
	/*
	 * function called during registration.
	 * if returns non-zero, the registration will fail.
	 */
	int (*initialize)(const hid_handle_t handle);

	/* return size of data copied to buf. if returns <= 0, error */
	int (*get_hid_descriptor)(const hid_handle_t handle, uint8_t *buf,
				  const size_t buf_size);
	/* return size of data copied  to buf. if return <= 0, error */
	int (*get_report_descriptor)(const hid_handle_t handle, uint8_t *buf,
				     const size_t buf_size);
	/* return size of data copied to buf. if return <= 0, error */
	int (*get_feature_report)(const hid_handle_t handle,
				  const uint8_t report_id, uint8_t *buf,
				  const size_t buf_size);
	/* return tranferred data size. if returns <= 0, error */
	int (*set_feature_report)(const hid_handle_t handle,
				  const uint8_t report_id, const uint8_t *data,
				  const size_t data_size);
	/* return size of data copied to buf. if returns <= 0, error */
	int (*get_input_report)(const hid_handle_t handle,
				const uint8_t report_id, uint8_t *buf,
				const size_t buf_size);

	/* suspend/resume, if returns non-zero, error */
	int (*resume)(const hid_handle_t handle);
	int (*suspend)(const hid_handle_t handle);
};

struct hid_device {
	uint8_t dev_class;
	uint16_t pid;
	uint16_t vid;

	const struct hid_callbacks *cbs;
};

/*
 * Do not call this function directly.
 * The function should be called only by HID_DEVICE_ENTRY()
 */
hid_handle_t hid_subsys_register_device(const struct hid_device *dev_info);
/* send HID input report */
int hid_subsys_send_input_report(const hid_handle_t handle, uint8_t *buf,
				 const size_t buf_size);
/* store HID device specific data */
int hid_subsys_set_device_data(const hid_handle_t handle, void *data);
/* retrieve HID device specific data */
void *hid_subsys_get_device_data(const hid_handle_t handle);

#define HID_DEVICE_ENTRY(hid_dev) \
	void _hid_dev_entry_##hid_dev(void) \
	{ \
		hid_subsys_register_device(&(hid_dev)); \
	} \
	DECLARE_HOOK(HOOK_INIT, _hid_dev_entry_##hid_dev, HOOK_PRIO_LAST - 2)

#endif /* __HID_DEVICE_H */
