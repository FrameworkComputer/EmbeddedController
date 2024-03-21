/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpc_bep_bio_alg.h"

#include <zephyr/logging/log.h>

#include <fingerprint/fingerprint_alg.h>

LOG_MODULE_REGISTER(fpc_bep_alg, LOG_LEVEL_INF);

/*
 * This file contains structures required by FPC library (algorithm) and
 * implementation of algorithm API
 */

const fpc_bio_info_t fpc_bio_info = {
	.algorithm = &fpc_bep_algorithm_pfe_1025,
	.template_size = CONFIG_FP_ALGORITHM_TEMPLATE_SIZE,
};

/* FPC BEP algorithm private data. */
struct fpc_bep_data {
	uint32_t enroll_ctx;
};

BUILD_ASSERT(sizeof(((struct fpc_bep_data *)0)->enroll_ctx) ==
		     FP_ALGORITHM_ENROLLMENT_SIZE,
	     "Wrong enroll_ctx size");

static int fpc_bep_algorithm_init(const struct fingerprint_algorithm *const alg)
{
	int rc;

	if (!IS_ENABLED(CONFIG_HAVE_BEP_PRIVATE_DRIVER)) {
		LOG_WRN("Initializing BEP algorithm without private library");
		return 0;
	}

	rc = bio_algorithm_init();
	if (rc < 0) {
		LOG_ERR("bio_algorithm_init() failed, result %d", rc);
		return -EINVAL;
	}

	return 0;
}

static int fpc_bep_algorithm_exit(const struct fingerprint_algorithm *const alg)
{
	int rc;

	if (!IS_ENABLED(CONFIG_HAVE_BEP_PRIVATE_DRIVER)) {
		return 0;
	}

	rc = bio_algorithm_exit();
	if (rc < 0) {
		LOG_ERR("bio_algorithm_exit() failed, result %d", rc);
		return -EINVAL;
	}

	return 0;
}

static int fpc_bep_enroll_start(const struct fingerprint_algorithm *const alg)
{
	struct fpc_bep_data *data = (struct fpc_bep_data *)alg->data;
	bio_enrollment_t bio_enroll = &data->enroll_ctx;
	int rc;

	if (!IS_ENABLED(CONFIG_HAVE_BEP_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	rc = bio_enrollment_begin(&bio_enroll);
	if (rc < 0) {
		LOG_ERR("bio_enrollment_begin() failed, result %d", rc);
		return -EINVAL;
	}

	return 0;
}

BUILD_ASSERT(FP_ENROLLMENT_RESULT_OK == BIO_ENROLLMENT_OK);
BUILD_ASSERT(FP_ENROLLMENT_RESULT_LOW_QUALITY == BIO_ENROLLMENT_LOW_QUALITY);
BUILD_ASSERT(FP_ENROLLMENT_RESULT_IMMOBILE == BIO_ENROLLMENT_IMMOBILE);
BUILD_ASSERT(FP_ENROLLMENT_RESULT_LOW_COVERAGE == BIO_ENROLLMENT_LOW_COVERAGE);
BUILD_ASSERT(FP_ENROLLMENT_RESULT_INTERNAL_ERROR ==
	     BIO_ENROLLMENT_INTERNAL_ERROR);

static int fpc_bep_enroll_step(const struct fingerprint_algorithm *const alg,
			       const uint8_t *const image, int *completion)
{
	struct fpc_bep_data *data = (struct fpc_bep_data *)alg->data;
	bio_enrollment_t bio_enroll = &data->enroll_ctx;
	int rc;

	if (!IS_ENABLED(CONFIG_HAVE_BEP_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	/*
	 * FPC BEP library takes image as 'void *', so we are casting away
	 * the 'const' here.
	 */
	rc = bio_enrollment_add_image(bio_enroll, (bio_image_t)image);
	if (rc < 0) {
		LOG_ERR("bio_enrollment_add_image() failed, result %d", rc);
		return -EINVAL;
	}

	*completion = bio_enrollment_get_percent_complete(bio_enroll);

	/*
	 * FP_ENROLLMENT_RESULT_* are synchronized with BIO_ENROLLMENT_*, so
	 * there is no need to translate codes.
	 */

	return rc;
}

static int fpc_bep_enroll_finish(const struct fingerprint_algorithm *const alg,
				 void *templ)
{
	struct fpc_bep_data *data = (struct fpc_bep_data *)alg->data;
	bio_enrollment_t bio_enroll = &data->enroll_ctx;
	bio_template_t bio_templ = templ;
	int rc;

	if (!IS_ENABLED(CONFIG_HAVE_BEP_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	rc = bio_enrollment_finish(bio_enroll, templ ? &bio_templ : NULL);
	if (rc < 0) {
		LOG_ERR("bio_enrollment_finish() failed, result %d", rc);
		return -EINVAL;
	}

	return 0;
}

BUILD_ASSERT(FP_MATCH_RESULT_NO_MATCH == BIO_TEMPLATE_NO_MATCH);
BUILD_ASSERT(FP_MATCH_RESULT_MATCH == BIO_TEMPLATE_MATCH);
BUILD_ASSERT(FP_MATCH_RESULT_MATCH_UPDATED == BIO_TEMPLATE_MATCH_UPDATED);
BUILD_ASSERT(FP_MATCH_RESULT_MATCH_UPDATE_FAILED ==
	     BIO_TEMPLATE_MATCH_UPDATE_FAILED);
BUILD_ASSERT(FP_MATCH_RESULT_LOW_QUALITY == BIO_TEMPLATE_LOW_QUALITY);
BUILD_ASSERT(FP_MATCH_RESULT_LOW_COVERAGE == BIO_TEMPLATE_LOW_COVERAGE);

static int fpc_bep_match(const struct fingerprint_algorithm *const alg,
			 void *templ, uint32_t templ_count,
			 const uint8_t *const image, int32_t *match_index,
			 uint32_t *update_bitmap)
{
	int rc;

	if (!IS_ENABLED(CONFIG_HAVE_BEP_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	/*
	 * FPC BEP library takes image as 'void *', so we are casting away
	 * the 'const' here.
	 */
	rc = bio_template_image_match_list(templ, templ_count,
					   (bio_image_t)image, match_index,
					   update_bitmap);
	if (rc < 0) {
		LOG_ERR("bio_template_image_match_list() failed, result %d",
			rc);
		return -EINVAL;
	}

	/*
	 * FP_MATCH_RESULT_* are synchronized with BIO_TEMPLATE_*, so there
	 * is no need to translate codes.
	 */

	return rc;
}

const struct fingerprint_algorithm_api fpc_bep_api = {
	.init = fpc_bep_algorithm_init,
	.exit = fpc_bep_algorithm_exit,
	.enroll_start = fpc_bep_enroll_start,
	.enroll_step = fpc_bep_enroll_step,
	.enroll_finish = fpc_bep_enroll_finish,
	.match = fpc_bep_match,
};

static struct fpc_bep_data fpc_bep_data;
FINGERPRINT_ALGORITHM_DEFINE(fpc_bep_algorithm, &fpc_bep_data, &fpc_bep_api);
