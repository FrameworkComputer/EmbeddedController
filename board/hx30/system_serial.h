/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* flash serial structure */

#ifndef __SYSTEM_SERIAL_H
#define __SYSTEM_SERIAL_H

#include <stdint.h>

#define SERIAL_STR_SIZE 21

enum ec_serial_idx {
	SN_MAINBOARD,
	SN_LAPTOP,
	SN_CAMERA,
	SN_DISPLAY,
	SN_BATTERY,
	SN_TOUCHPAD,
	SN_KEYBOARD,
	SN_FINGERPRINT,
	SN_AUDIO_DAUGHTERCARD,
	SN_A_COVER,
	SN_B_COVER,
	SN_C_COVER,
	SN_D_COVER,
	SN_ANTENNA_MAIN,
	SN_ANTENNA_AUX, /*Currently not used*/
	SN_TOUCHPAD_FPC,
	SN_FINGERPRINT_FFC,
	SN_EDP_CABLE,
	SN_LCD_CABLE,
	SN_THERMAL_ASSY,
	SN_WIFI_MODULE, /*Currently not used*/
	SN_SPEAKER,
	SN_RAM_SLOT_1, /*Currently not used*/
	SN_RAM_SLOT_2, /*Currently not used*/
	SN_SSD, /*Currently not used*/
	SN_AUDIO_FFC,
	SN_RESERVED1,
	SN_MAX
};
struct ec_flash_serial_info {
	/* Header */
	uint32_t magic; /* 0xF5A3E */
	uint32_t length; /* Length of fields following this */
	uint32_t version; /* Version=1, update this if field structures below this change */
	/**
	 * An incrementing counter that should be
	 * incremented every time the structure is written to flash */
	uint32_t update_number;

	/* Serial section */
	int8_t serials[SN_MAX][SERIAL_STR_SIZE];
	uint8_t reserved_zeros[1024 - (SN_MAX*SERIAL_STR_SIZE) - 16];

	/* Certificate section */
	/*certificate starts at 1024*/
	uint8_t mainboard_certificate_der[1024];
	uint8_t encrypted_mainboard_key_der[256];
	uint8_t reserved1_zeros[1024 - 260];
	/* crc-32 */
	uint32_t crc;
} __ec_align1;

#endif /* __SYSTEM_SERIAL_H */