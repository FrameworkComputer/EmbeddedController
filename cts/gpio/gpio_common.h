/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __GPIO_COMMON_H
#define __GPIO_COMMON_H

/* sync_test tests whether sync completes successfully
 * set_low_test checks if the dut can set a line low
 * set_high test checks if the dut can set a line high
 * read_low_test checks if the dut can read a line that is low
 * read_high_test checks if the dut can read a line that is high
 * od_read_high_test checks if the dut reads its true pin level (success)
 * or its register level when configured as a low open drain output pin
   */

#define READ_WAIT_TIME_MS 100
#define GPIO_CTS_TEST_COUNT 6

#endif
