/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Seven Segment Display module for Chrome EC */

#ifndef __CROS_EC_DISPLAY_7SEG_H
#define __CROS_EC_DISPLAY_7SEG_H

enum seven_seg_module_display {
	SEVEN_SEG_CONSOLE_DISPLAY,	/* Console data */
	SEVEN_SEG_EC_DISPLAY,		/* power state */
	SEVEN_SEG_PORT80_DISPLAY,	/* port80 data */
};

/**
 * Write register to MAX656x 7-segment display.
 *
 * @param module which is writing to the display
 * @param data to be displayed
 * @return EC_SUCCESS is write is successful
 */
int display_7seg_write(enum seven_seg_module_display module, uint16_t data);

#endif  /* __CROS_EC_DISPLAY_7SEG_H */

