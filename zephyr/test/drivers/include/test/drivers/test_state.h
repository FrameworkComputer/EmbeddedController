/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_DRIVERS_INCLUDE_TEST_STATE_H_
#define ZEPHYR_TEST_DRIVERS_INCLUDE_TEST_STATE_H_

struct test_state {
	bool ec_app_main_run;
};

bool drivers_predicate_pre_main(const void *state);

bool drivers_predicate_post_main(const void *state);

#endif /* ZEPHYR_TEST_DRIVERS_INCLUDE_TEST_STATE_H_ */
