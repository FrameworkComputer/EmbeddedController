/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpc_libfp_bio_alg.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <fingerprint/fingerprint_alg.h>

LOG_MODULE_REGISTER(fpc_libfp_alg, LOG_LEVEL_INF);

/*
 * This file contains structures required by FPC library (algorithm) and
 * implementation of algorithm API
 */

/* FPC LIBFP algorithm private data. */
struct fpc_libfp_data {
	bio_sensor_t bio_sensor;
	uint8_t enroll_ctx[FP_ALGORITHM_ENROLLMENT_SIZE];
};

static int
fpc_libfp_algorithm_init(const struct fingerprint_algorithm *const alg)
{
	struct fpc_libfp_data *data = (struct fpc_libfp_data *)alg->data;
	int rc;

	if (!IS_ENABLED(CONFIG_HAVE_LIBFP_PRIVATE_DRIVER)) {
		LOG_WRN("Initializing LIBFP algorithm without private library");
		return 0;
	}

	if (data->bio_sensor != NULL) {
		/* Release any previously held resources from earlier iterations
		 */
		rc = bio_sensor_destroy(data->bio_sensor);
		if (rc) {
			LOG_ERR("FPC Sensor resources release failed: %d", rc);
		}
		data->bio_sensor = NULL;
	}

	rc = bio_algorithm_exit();
	if (rc) {
		LOG_ERR("FPC Algorithm resources release failed: %d", rc);
	}
	rc = bio_algorithm_init();
	LOG_INF("Algorithm init: 0x%x", rc);
	rc = bio_sensor_create(&data->bio_sensor);
	LOG_INF("Sensor create: 0x%x", rc);

	return rc;
}

static int
fpc_libfp_algorithm_exit(const struct fingerprint_algorithm *const alg)
{
	struct fpc_libfp_data *data = (struct fpc_libfp_data *)alg->data;
	int rc;

	if (!IS_ENABLED(CONFIG_HAVE_LIBFP_PRIVATE_DRIVER)) {
		return 0;
	}

	if (data->bio_sensor != NULL) {
		rc = bio_sensor_destroy(data->bio_sensor);
		if (rc) {
			LOG_ERR("FPC Sensor resources release failed: %d", rc);
		}
		data->bio_sensor = NULL;
	}

	rc = bio_algorithm_exit();
	if (rc < 0) {
		LOG_ERR("bio_algorithm_exit() failed, result %d", rc);
		return -EINVAL;
	}

	return 0;
}

static int fpc_libfp_enroll_start(const struct fingerprint_algorithm *const alg)
{
	struct fpc_libfp_data *data = (struct fpc_libfp_data *)alg->data;
	bio_enrollment_t bio_enroll = data->enroll_ctx;
	int rc;

	if (!IS_ENABLED(CONFIG_HAVE_LIBFP_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	rc = bio_enrollment_begin(data->bio_sensor, &bio_enroll);
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

static int fpc_libfp_enroll_step(const struct fingerprint_algorithm *const alg,
				 const uint8_t *const image, int *completion)
{
	struct fpc_libfp_data *data = (struct fpc_libfp_data *)alg->data;
	bio_enrollment_t bio_enroll = data->enroll_ctx;
	int rc;

	if (!IS_ENABLED(CONFIG_HAVE_LIBFP_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	/*
	 * FPC LIBFP library takes image as 'void *', so we are casting away
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

static int
fpc_libfp_enroll_finish(const struct fingerprint_algorithm *const alg,
			void *templ)
{
	struct fpc_libfp_data *data = (struct fpc_libfp_data *)alg->data;
	bio_enrollment_t bio_enroll = data->enroll_ctx;
	bio_template_t bio_templ = templ;
	int rc;

	if (!IS_ENABLED(CONFIG_HAVE_LIBFP_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	rc = bio_enrollment_finish(bio_enroll, templ ? &bio_templ : NULL);
	if (rc < 0) {
		LOG_ERR("bio_enrollment_finish() failed, result %d", rc);
		return -EINVAL;
	}

	return 0;
}

static int fpc_libfp_match(const struct fingerprint_algorithm *const alg,
			   void *templ, uint32_t templ_count,
			   const uint8_t *const image, int32_t *match_index,
			   uint32_t *update_bitmap)
{
	int rc;

	if (!IS_ENABLED(CONFIG_HAVE_LIBFP_PRIVATE_DRIVER)) {
		return -ENOTSUP;
	}

	/*
	 * FPC LIBFP library takes image as 'void *', so we are casting away
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

const struct fingerprint_algorithm_api fpc_libfp_api = {
	.init = fpc_libfp_algorithm_init,
	.exit = fpc_libfp_algorithm_exit,
	.enroll_start = fpc_libfp_enroll_start,
	.enroll_step = fpc_libfp_enroll_step,
	.enroll_finish = fpc_libfp_enroll_finish,
	.match = fpc_libfp_match,
};

static struct fpc_libfp_data fpc_libfp_data;
FINGERPRINT_ALGORITHM_DEFINE(fpc_libfp_algorithm, &fpc_libfp_data,
			     &fpc_libfp_api);

#ifdef CONFIG_HAVE_LIBFP_PRIVATE_DRIVER
K_HEAP_DEFINE(libfp_heap, CONFIG_LIBFP_PRIVATE_DRIVER_HEAP_SIZE);

int32_t __unused FpcMalloc(void **data, size_t size)
{
	void *p = k_heap_aligned_alloc(&libfp_heap, sizeof(void *), size,
				       K_NO_WAIT);

	if (p == NULL) {
		LOG_ERR("Failed to allocate %d bytes", size);
		k_oops();
		CODE_UNREACHABLE;
	}
	*data = p;

	return 0;
}

void __unused FpcFree(void **data)
{
	k_heap_free(&libfp_heap, *data);
	*data = NULL;
}
#endif /* CONFIG_HAVE_LIBFP_PRIVATE_DRIVER */
