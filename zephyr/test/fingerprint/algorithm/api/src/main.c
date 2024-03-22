/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/fff.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <fingerprint/fingerprint_alg.h>

DEFINE_FFF_GLOBALS;

ZTEST_SUITE(fp_alg_api, NULL, NULL, NULL, NULL, NULL);

FAKE_VALUE_FUNC(int, test_init, const struct fingerprint_algorithm *const);
FAKE_VALUE_FUNC(int, test_exit, const struct fingerprint_algorithm *const);
FAKE_VALUE_FUNC(int, test_enroll_start,
		const struct fingerprint_algorithm *const);
FAKE_VALUE_FUNC(int, test_enroll_step,
		const struct fingerprint_algorithm *const, const uint8_t *const,
		int *);
FAKE_VALUE_FUNC(int, test_enroll_finish,
		const struct fingerprint_algorithm *const, void *);
FAKE_VALUE_FUNC(int, test_match, const struct fingerprint_algorithm *const,
		void *, uint32_t, const uint8_t *const, int32_t *, uint32_t *);

const struct fingerprint_algorithm_api test1_api = {
	.init = test_init,
	.exit = test_exit,
	.enroll_start = test_enroll_start,
	.enroll_step = test_enroll_step,
	.enroll_finish = test_enroll_finish,
	.match = test_match,
};

FINGERPRINT_ALGORITHM_DEFINE(test1, NULL, &test1_api);

const struct fingerprint_algorithm_api test2_api = {
	.init = NULL,
	.exit = NULL,
	.enroll_start = NULL,
	.enroll_step = NULL,
	.enroll_finish = NULL,
	.match = NULL,
};

FINGERPRINT_ALGORITHM_DEFINE(test2, NULL, &test2_api);

ZTEST(fp_alg_api, test_algorithm_count)
{
	zassert_equal(fingerprint_algorithm_count_get(), 2);
}

ZTEST(fp_alg_api, test_algorithm_get)
{
	/*
	 * Cast pointers to uintptr_t to force compiler to compare values
	 * here. If pointers are derived from different objects, GCC will
	 * evaluate comparison to false, even when pointers point to the
	 * same memory.
	 */
	zassert_equal((uintptr_t)fingerprint_algorithm_get(0),
		      (uintptr_t)&test1);
	zassert_equal((uintptr_t)fingerprint_algorithm_get(1),
		      (uintptr_t)&test2);
}

ZTEST(fp_alg_api, test_algorithm_name)
{
	zassert_mem_equal(fingerprint_algorithm_get(0)->name, "test1", 6);
	zassert_mem_equal(fingerprint_algorithm_get(1)->name, "test2", 6);
}

ZTEST(fp_alg_api, test_init)
{
	const struct fingerprint_algorithm *alg;

	RESET_FAKE(test_init);

	test_init_fake.return_val = 0;
	alg = fingerprint_algorithm_get(0);
	zassert_ok(fingerprint_algorithm_init(alg));
	zassert_equal(1, test_init_fake.call_count);

	alg = fingerprint_algorithm_get(1);
	zassert_equal(-ENOTSUP, fingerprint_algorithm_init(alg));
}

ZTEST(fp_alg_api, test_exit)
{
	const struct fingerprint_algorithm *alg;

	RESET_FAKE(test_exit);

	test_exit_fake.return_val = 0;
	alg = fingerprint_algorithm_get(0);
	zassert_ok(fingerprint_algorithm_exit(alg));
	zassert_equal(1, test_exit_fake.call_count);

	alg = fingerprint_algorithm_get(1);
	zassert_equal(-ENOTSUP, fingerprint_algorithm_exit(alg));
}

ZTEST(fp_alg_api, test_enroll_start)
{
	const struct fingerprint_algorithm *alg;

	RESET_FAKE(test_enroll_start);

	test_enroll_start_fake.return_val = 0;
	alg = fingerprint_algorithm_get(0);
	zassert_ok(fingerprint_enroll_start(alg));
	zassert_equal(1, test_enroll_start_fake.call_count);

	alg = fingerprint_algorithm_get(1);
	zassert_equal(-ENOTSUP, fingerprint_enroll_start(alg));
}

ZTEST(fp_alg_api, test_enroll_step)
{
	const struct fingerprint_algorithm *alg;

	RESET_FAKE(test_enroll_step);

	test_enroll_step_fake.return_val = 0;
	alg = fingerprint_algorithm_get(0);
	zassert_ok(fingerprint_enroll_step(alg, NULL, NULL));
	zassert_equal(1, test_enroll_step_fake.call_count);

	alg = fingerprint_algorithm_get(1);
	zassert_equal(-ENOTSUP, fingerprint_enroll_step(alg, NULL, NULL));
}

ZTEST(fp_alg_api, test_enroll_finish)
{
	const struct fingerprint_algorithm *alg;
	uint8_t templ;

	RESET_FAKE(test_enroll_finish);

	test_enroll_finish_fake.return_val = 0;
	alg = fingerprint_algorithm_get(0);
	zassert_ok(fingerprint_enroll_finish(alg, &templ));
	zassert_equal(1, test_enroll_finish_fake.call_count);

	alg = fingerprint_algorithm_get(1);
	zassert_equal(-ENOTSUP, fingerprint_enroll_finish(alg, &templ));
}

ZTEST(fp_alg_api, test_match)
{
	const struct fingerprint_algorithm *alg;
	uint8_t templ;

	RESET_FAKE(test_match);

	test_match_fake.return_val = 0;
	alg = fingerprint_algorithm_get(0);
	zassert_ok(fingerprint_match(alg, &templ, 0, NULL, NULL, NULL));
	zassert_equal(1, test_match_fake.call_count);

	alg = fingerprint_algorithm_get(1);
	zassert_equal(-ENOTSUP,
		      fingerprint_match(alg, &templ, 0, NULL, NULL, NULL));
}
