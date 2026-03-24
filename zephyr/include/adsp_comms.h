/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ADSP_COMMS_H
#define __CROS_EC_ADSP_COMMS_H

#include <stdint.h>

#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus

extern "C" {
#endif

typedef void (*adsp_comms_callback_t)(uint8_t fid, uint8_t addr, uint16_t data);

struct adsp_comms_callback {
	uint8_t fid;
	uint8_t addr;
	adsp_comms_callback_t cb;
};

/**
 * Register a callback for a specific ADSP feature ID and register address.
 *
 * @param _fid   The feature ID.
 * @param _addr  The register address.
 * @param _cb    The callback function.
 */
#define ADSP_COMMS_REGISTER_CB(_fid, _addr, _cb)                        \
	static const STRUCT_SECTION_ITERABLE(                           \
		adsp_comms_callback, UTIL_CAT(adsp_comms_cb_, _cb)) = { \
		.fid = (_fid),                                          \
		.addr = (_addr),                                        \
		.cb = (_cb),                                            \
	}

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ADSP_COMMS_H */
