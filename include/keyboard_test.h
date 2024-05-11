/*
 * Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Keyboard scanner test module for Chrome EC */

#ifndef __CROS_EC_KEYBOARD_TEST_H
#define __CROS_EC_KEYBOARD_TEST_H

#include <timer.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Keyboard scan test item - contains a single scan to 'present' to key scan
 * logic.
 */
struct keyscan_item {
	timestamp_t abs_time; /* absolute timestamp to present this item */
	uint32_t time_us; /* time for this item relative to test start */
	uint8_t done; /* 1 if we managed to present this */
	uint8_t scan[KEYBOARD_COLS_MAX];
};

/**
 * Get the next key scan from the test sequence, if any
 *
 * @param column	Column to read (-1 to OR all columns together
 * @param scan		Raw scan data read from GPIOs
 * @return test scan, or just 'scan' if no test is active
 */
uint8_t keyscan_seq_get_scan(int column, uint8_t scan);

/**
 * Calculate the delay until the next key scan event needs to be presented
 *
 * @return number of microseconds from now until the next key scan event, or
 *	-1 if there is no future key scan event (e.g. testing is complete)
 */
int keyscan_seq_next_event_delay(void);

#ifdef __cplusplus
}
#endif

#endif
