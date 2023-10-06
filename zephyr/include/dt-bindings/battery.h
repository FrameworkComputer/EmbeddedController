/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef DT_BINDINGS_BATTERY_H_
#define DT_BINDINGS_BATTERY_H_

/*
 * Macros used by LED devicetree files (led.dts) to define battery-level
 * range.
 */
#define BATTERY_LEVEL_EMPTY 0
#define BATTERY_LEVEL_SHUTDOWN 3
#define BATTERY_LEVEL_CRITICAL 5
#define BATTERY_LEVEL_LOW 10
#define BATTERY_LEVEL_FULL 100

/* Battery status */
#define SB_STATUS_FULLY_DISCHARGED BIT(4)
#define SB_STATUS_FULLY_CHARGED BIT(5)
#define SB_STATUS_DISCHARGING BIT(6)
#define SB_STATUS_INITIALIZED BIT(7)
#define SB_STATUS_REMAINING_TIME_ALARM BIT(8)
#define SB_STATUS_REMAINING_CAPACITY_ALARM BIT(9)
#define SB_STATUS_TERMINATE_DISCHARGE_ALARM BIT(11)
#define SB_STATUS_OVERTEMP_ALARM BIT(12)
#define SB_STATUS_TERMINATE_CHARGE_ALARM BIT(14)
#define SB_STATUS_OVERCHARGED_ALARM BIT(15)

#define FG_FLAG_WRITE_BLOCK BIT(0)
#define FG_FLAG_SLEEP_MODE BIT(1)
#define FG_FLAG_MFGACC BIT(2)
#define FG_FLAG_MFGACC_SMB_BLOCK BIT(3)

#endif /* DT_BINDINGS_BATTERY_H_ */
