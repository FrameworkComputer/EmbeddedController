/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>

#include <zephyr/drivers/fingerprint/fingerprint_egis630_private.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <fingerprint/fingerprint_alg.h>

LOG_MODULE_REGISTER(egis_egis630_alg, LOG_LEVEL_INF);

/*
 * This file contains implementation of the algorithm API of the EGIS egis630
 * library (algorithm).
 */

static int
egis_egis630_algorithm_init(const struct fingerprint_algorithm *const alg)
{
	return 0;
}

static int
egis_egis630_algorithm_exit(const struct fingerprint_algorithm *const alg)
{
	return 0;
}

static int
egis_egis630_enroll_start(const struct fingerprint_algorithm *const alg)
{
	if (!IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	egis_api_return_t ret = egis_enrollment_begin();
	if (ret < 0) {
		LOG_ERR("egis_enrollment_begin() failed, result %d", ret);
		return -EINVAL;
	}

	return 0;
}

static int
egis_egis630_enroll_step(const struct fingerprint_algorithm *const alg,
			 const uint8_t *const image, int *completion)
{
	if (!IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	egis_api_return_t ret =
		egis_finger_enroll((FP_EGIS_IMAGE *)image, completion);
	switch (ret) {
	case EGIS_API_ENROLL_FINISH:
	case EGIS_API_ENROLL_IMAGE_OK:
		return FP_ENROLLMENT_RESULT_OK;
	case EGIS_API_ENROLL_REDUNDANT_INPUT:
		return FP_ENROLLMENT_RESULT_IMMOBILE;
	case EGIS_API_ENROLL_LOW_QUALITY:
		return FP_ENROLLMENT_RESULT_LOW_QUALITY;
	case EGIS_API_ENROLL_LOW_COVERAGE:
		return FP_ENROLLMENT_RESULT_LOW_COVERAGE;
	default:
		assert(ret < 0);
		LOG_ERR("egis_finger_enroll() failed, result %d", ret);
		return -EINVAL;
	}
}

static int
egis_egis630_enroll_finish(const struct fingerprint_algorithm *const alg,
			   void *templ)
{
	if (!IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	egis_api_return_t ret = egis_enrollment_finish(templ);
	if (ret < 0) {
		LOG_ERR("egis_enrollment_finish() failed, result %d", ret);
		return -EINVAL;
	}

	return 0;
}

static int egis_egis630_match(const struct fingerprint_algorithm *const alg,
			      void *templ, uint32_t templ_count,
			      const uint8_t *const image, bool template_update,
			      int32_t *match_index, uint32_t *update_bitmap)
{
	if (!IS_ENABLED(CONFIG_HAVE_EGIS630_PRIVATE_ALGORITHM)) {
		return -ENOTSUP;
	}

	egis_api_return_t ret = egis_finger_match(
		templ, templ_count, (FP_EGIS_IMAGE *)image, match_index,
		template_update ? update_bitmap : NULL);

	switch (ret) {
	case EGIS_API_MATCH_MATCHED:
		return FP_MATCH_RESULT_MATCH;
	case EGIS_API_MATCH_MATCHED_UPDATED:
		return FP_MATCH_RESULT_MATCH_UPDATED;
	case EGIS_API_MATCH_MATCHED_UPDATED_FAILED:
		return FP_MATCH_RESULT_MATCH_UPDATE_FAILED;
	case EGIS_API_MATCH_NOT_MATCHED:
		return FP_MATCH_RESULT_NO_MATCH;
	case EGIS_API_MATCH_LOW_QUALITY:
		return FP_MATCH_RESULT_LOW_QUALITY;
	case EGIS_API_MATCH_LOW_COVERAGE:
		return FP_MATCH_RESULT_LOW_COVERAGE;
	default:
		assert(ret < 0);
		LOG_ERR("egis_finger_match() failed, result %d", ret);
		return -EINVAL;
	}
}

const struct fingerprint_algorithm_api egis_egis630_api = {
	.init = egis_egis630_algorithm_init,
	.exit = egis_egis630_algorithm_exit,
	.enroll_start = egis_egis630_enroll_start,
	.enroll_step = egis_egis630_enroll_step,
	.enroll_finish = egis_egis630_enroll_finish,
	.match = egis_egis630_match,
};

FINGERPRINT_ALGORITHM_DEFINE(egis_egis630_algorithm, NULL, &egis_egis630_api);
