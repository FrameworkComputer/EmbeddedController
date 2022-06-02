/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef EMUL_POWER_SIGNALS_H_
#define EMUL_POWER_SIGNALS_H_

/**
 * @brief Test platform definition,
 *        This structure contains all power signal nodes associated to one
 *        test.
 */
struct power_signal_emul_test_platform {
	char *name_id;
	int nodes_count;
	struct power_signal_emul_node **nodes;
};

#define EMUL_POWER_SIGNAL_TEST_PLATFORM_DECL(inst) \
	extern const struct power_signal_emul_test_platform inst;

#define EMUL_POWER_SIGNAL_TEST_PLATFORM(inst) (&DT_CAT(DT_N_S_, inst))

DT_FOREACH_STATUS_OKAY(intel_ap_pwr_test_platform,
		       EMUL_POWER_SIGNAL_TEST_PLATFORM_DECL)
/**
 * @brief Load test platform.
 *
 * This initializes each of the test platform nodes.
 *
 * @param test_platform Pointer to test platform structure.
 *
 * @return 0 indicating success.
 * @return -EINVAL `test_id` parameter is invalid.
 * @return -EBUSY `test_id` One test platform is currently loaded.
 */
int power_signal_emul_load(
	const struct power_signal_emul_test_platform *test_platform);

/**
 * @brief Unload test platform.
 *
 * @return 0 indicating success.
 * @return -EINVAL no test platform has been loaded.
 */
int power_signal_emul_unload(void);

#endif /* EMUL_POWER_SIGNALS_H_ */
