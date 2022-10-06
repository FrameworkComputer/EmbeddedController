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
#define BATTERY_LEVEL_NEAR_FULL 97
#define BATTERY_LEVEL_FULL 100

#endif /* DT_BINDINGS_BATTERY_H_ */
