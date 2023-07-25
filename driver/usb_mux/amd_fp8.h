/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * AMD FP8 USB/DP/USB4 Mux.
 */

#ifndef __CROS_EC_USB_MUX_AMD_FP8_H
#define __CROS_EC_USB_MUX_AMD_FP8_H

#include "gpio.h"

#define AMD_FP8_MUX_ADDR0 0x54
#define AMD_FP8_MUX_ADDR1 0x58
#define AMD_FP8_MUX_ADDR2 0x5C

/* Type-1 Write - Up to 5 bytes */
/* On non-USB4 muxes only the first two bytes are sent. */
#define AMD_FP8_WRITE1_USB3_LEN 2
#define AMD_FP8_WRITE1_USB4_LEN 5

#define AMD_FP8_MUX_WRITE1_INDEX_BYTE 0
#define AMD_FP8_MUX_WRITE1_CONTROL_BYTE 1
#define AMD_FP8_MUX_WRITE1_CABLE_BYTE 2
#define AMD_FP8_MUX_WRITE1_VER_BYTE 3
#define AMD_FP8_MUX_WRITE1_SPEED_BYTE 4

enum amd_fp8_control_mode {
	AMD_FP8_CONTROL_SAFE = 0,
	AMD_FP8_CONTROL_USB = 1,
	AMD_FP8_CONTROL_DP = 2,
	AMD_FP8_CONTROL_DOCK = 3,
	AMD_FP8_CONTROL_TBT3_USB4 = 4,
};

#define AMD_FP8_MUX_W1_CTRL_MODE_MASK GENMASK(3, 0)
#define AMD_FP8_MUX_W1_CTRL_FLIP BIT(4)
#define AMD_FP8_MUX_W1_CTRL_DATA_RESET BIT(6)
#define AMD_FP8_MUX_W1_CTRL_UFP BIT(7)

#define AMD_FP8_MUX_W1_CABLE_USB4 BIT(0)
#define AMD_FP8_MUX_W1_CABLE_TBT3 BIT(1)
#define AMD_FP8_MUX_W1_CABLE_CLX BIT(2)
#define AMD_FP8_MUX_W1_CABLE_RETIMED BIT(3)
#define AMD_FP8_MUX_W1_CABLE_BIDIR BIT(4)
#define AMD_FP8_MUX_W1_CABLE_GEN3 BIT(5)
#define AMD_FP8_MUX_W1_CABLE_ACTIVE BIT(7)

/* TODO(b/276335130): Fill in 3 bytes for cable info */
#define AMD_FP8_MUX_W1_SPEED_TC BIT(0)

/* Type-3 Read - 3 bytes */
#define AMD_FP8_MUX_READ3_CODE 0x80

#define AMD_FP8_MUX_READ3_STATUS_BYTE 0
#define AMD_FP8_MUX_READ3_PORT0_BYTE 1

#define AMD_FP8_MUX_R3_STATUS_ERROR BIT(0)
#define AMD_FP8_MUX_R3_STATUS_READY BIT(6)

enum amd_fp8_command_status {
	AMD_FP8_COMMAND_STATUS_IN_PROGRESS = 0,
	AMD_FP8_COMMAND_STATUS_COMPLETE = 1,
	AMD_FP8_COMMAND_STATUS_TIMEOUT = 2,
};
#define AMD_FP8_MUX_R3_PORT0_CONTROL_MASK GENMASK(5, 0)
#define AMD_FP8_MUX_R3_PORT0_STATUS_MASK GENMASK(7, 6)

/* TODO(b/276335130): Fill in APU mailbox if needed */

/* Type-4 Read - APU mailbox, 4 bytes. */
#define AMD_FP8_MUX_READ4_CODE 0xA0
#define AMD_FP8_MUX_READ4_LEN 4

#define AMD_FP8_MUX_R4_BYTE0_INT_STATUS BIT(7)

/* Type-5 Read - Interrupt status, 1 byte */
#define AMD_FP8_MUX_READ5_CODE 0xA2

#define AMD_FP8_MUX_R5_XBAR_INT BIT(0)
#define AMD_FP8_MUX_R5_COMMAND_INT BIT(1)
#define AMD_FP8_MUX_R5_ERROR_INT BIT(2)
#define AMD_FP8_MUX_R5_MAIL_INT BIT(3)
#define AMD_FP8_MUX_R5_XBAR_STATUS BIT(7)

void amd_fp8_mux_interrupt(enum gpio_signal signal);

#endif /* __CROS_EC_USB_MUX_AMD_FP8_H */
