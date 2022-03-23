/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_PRIMUS_PS2_H
#define __CROS_EC_PRIMUS_PS2_H

/* Primus board-specific PS2 configuration */
/*
 * Valid first byte responses to the "Read Secondary ID" (0xE1) command.
 * 0x01 was the original IBM trackpoint, others implement very limited
 * subset of trackpoint features.
 */
#define TP_READ_ID			0xE1	/* Sent for device identification */

#define TP_COMMAND			0xE2	/* Commands start with this */

/*
 * Toggling Flag bits
 */
#define TP_TOGGLE			0x47	/* Toggle command */

#define TP_VARIANT_ELAN			0x03
#define TP_VARIANT_SYNAPTICS		0x06
#define TP_TOGGLE_SOURCE_TAG		0x20
#define TP_TOGGLE_BURST			0x28
#define TP_TOGGLE_SNAPTICS_SLEEP	0x10
#define TP_TOGGLE_ELAN_SLEEP		0x8

#endif /* __CROS_EC_PRIMUS_PS2_H */
