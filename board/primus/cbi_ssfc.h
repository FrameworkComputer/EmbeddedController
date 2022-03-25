/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef _PRIMUS_CBI_SSFC_H_
#define _PRIMUS_CBI_SSFC_H_
#include "stdint.h"
/****************************************************************************
 * Primus CBI Second Source Factory Cache
 */
/*
 * Trackpoint (Bit 0)
 */
enum ec_ssfc_trackpoint {
	SSFC_SENSOR_TRACKPOINT_ELAN = 0,
	SSFC_SENSOR_TRACKPOINT_SYNAPTICS = 1,
};
union primus_cbi_ssfc {
	struct {
		enum ec_ssfc_trackpoint trackpoint : 2;
		uint32_t reserved_1 : 30;
	};
	uint32_t raw_value;
};
/**
 * Get the trackpoint type from SSFC_CONFIG.
 *
 * @return the Trackpoint board type.
 */
enum ec_ssfc_trackpoint get_cbi_ssfc_trackpoint(void);
#endif /* _PRIMUS_CBI_SSFC_H_ */
