/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_FINGERPRINT_FINGERPRINT_ALG_H_
#define ZEPHYR_INCLUDE_FINGERPRINT_FINGERPRINT_ALG_H_

#include <errno.h>
#include <stdint.h>

#include <zephyr/sys/iterable_sections.h>

/** Forward declaration of the fingerprint_algorithm type. */
struct fingerprint_algorithm;

/** Fingerprint matching algorithm API. */
struct fingerprint_algorithm_api {
	int (*init)(const struct fingerprint_algorithm *const alg);
	int (*exit)(const struct fingerprint_algorithm *const alg);
	int (*enroll_start)(const struct fingerprint_algorithm *const alg);
	int (*enroll_step)(const struct fingerprint_algorithm *const alg,
			   const uint8_t *const image, int *completion);
	int (*enroll_finish)(const struct fingerprint_algorithm *const alg,
			     void *templ);
	int (*match)(const struct fingerprint_algorithm *const alg, void *templ,
		     uint32_t templ_count, const uint8_t *const image,
		     int32_t *match_index, uint32_t *update_bitmap);
};

/** Fingerprint algorithm structure. */
struct fingerprint_algorithm {
	const struct fingerprint_algorithm_api *api;
	void *data;
	const char *name;
};

/**
 * Macro for creating a fingerprint algorithm instance
 *
 * @param _api  Pointer to fingerprint algorithm API
 * @param _data Pointer to fingerprint algorithm private data
 * @param _name Name of the fingerprint algorithm instance
 */
#define FINGERPRINT_ALGORITHM_DEFINE(_name, _data, _api)                \
	const STRUCT_SECTION_ITERABLE(fingerprint_algorithm, _name) = { \
		.api = _api,                                            \
		.data = _data,                                          \
		.name = STRINGIFY(_name),                               \
	}

/** Fingerprint enrollment result. */
enum fp_enrollment_result {
	/** The algorithm accepted the image. */
	FP_ENROLLMENT_RESULT_OK = 0,
	/** The fingerprint image has low quality. */
	FP_ENROLLMENT_RESULT_LOW_QUALITY = 1,
	/** The image is similar to previously added. */
	FP_ENROLLMENT_RESULT_IMMOBILE = 2,
	/** The image has low coverage. */
	FP_ENROLLMENT_RESULT_LOW_COVERAGE = 3,
	/** Internal algorithm error. */
	FP_ENROLLMENT_RESULT_INTERNAL_ERROR = 5,
};

/** Fingerprint match result. */
enum fp_match_result {
	/** The image doesn't match any template. */
	FP_MATCH_RESULT_NO_MATCH = 0,
	/** The image matches one of the templates. */
	FP_MATCH_RESULT_MATCH = 1,
	/** The image matches one of the templates and the template
	 *  was updated.
	 */
	FP_MATCH_RESULT_MATCH_UPDATED = 3,
	/** The image matches one of the templates, but updating template
	 *  failed.
	 */
	FP_MATCH_RESULT_MATCH_UPDATE_FAILED = 5,
	/** The fingerprint image has low quality. */
	FP_MATCH_RESULT_LOW_QUALITY = 2,
	/** The image has low coverage. */
	FP_MATCH_RESULT_LOW_COVERAGE = 4,
};

/**
 * Get number of available algorithm instances.
 *
 * @return Number of instances.
 */
static inline int fingerprint_algorithm_count_get(void)
{
	int cnt;

	STRUCT_SECTION_COUNT(fingerprint_algorithm, &cnt);

	return cnt;
}

/**
 * Get pointer to fingerprint algorithm instance.
 *
 * @param idx Fingerprint algorithm instance index.
 *
 * @return Pointer to the algorithm instance.
 */
static inline const struct fingerprint_algorithm *
fingerprint_algorithm_get(uint32_t idx)
{
	const struct fingerprint_algorithm *alg;

	STRUCT_SECTION_GET(fingerprint_algorithm, idx, &alg);

	return alg;
}

/**
 * Initialize the fingerprint algorithm
 *
 * @param alg Pointer to fingerprint algorithm instance.
 * @return 0 on success.
 * @return -ENOTSUP if the algorithm doesn't implement the function.
 * @return negative value on error.
 */
static inline int
fingerprint_algorithm_init(const struct fingerprint_algorithm *const alg)
{
	__ASSERT_NO_MSG(alg != NULL);
	if (alg->api->init == NULL) {
		return -ENOTSUP;
	}

	return alg->api->init(alg);
}

/**
 * Deinitialize the fingerprint algorithm
 *
 * @param alg Pointer to fingerprint algorithm instance.
 * @return 0 on success.
 * @return -ENOTSUP if the algorithm doesn't implement the function.
 * @return negative value on error.
 */
static inline int
fingerprint_algorithm_exit(const struct fingerprint_algorithm *const alg)
{
	__ASSERT_NO_MSG(alg != NULL);
	if (alg->api->exit == NULL) {
		return -ENOTSUP;
	}

	return alg->api->exit(alg);
}

/**
 * Start process of creating new template
 *
 * @param alg Pointer to fingerprint algorithm instance.
 * @return 0 on success.
 * @return -ENOTSUP if the algorithm doesn't implement the function.
 * @return negative value on error.
 */
static inline int
fingerprint_enroll_start(const struct fingerprint_algorithm *const alg)
{
	__ASSERT_NO_MSG(alg != NULL);
	if (alg->api->enroll_start == NULL) {
		return -ENOTSUP;
	}

	return alg->api->enroll_start(alg);
}

/**
 * Add image to started enroll session
 *
 * @param alg Pointer to fingerprint algorithm instance.
 * @param image Pointer to image we want to add.
 * @param completion Pointer to variable where current enroll progress is
 *                   stored.
 * @return fp_enroll_status enum on success.
 * @return -ENOTSUP if the algorithm doesn't implement the function.
 * @return negative value on error.
 */
static inline int
fingerprint_enroll_step(const struct fingerprint_algorithm *const alg,
			const uint8_t *const image, int *completion)
{
	__ASSERT_NO_MSG(alg != NULL);
	if (alg->api->enroll_step == NULL) {
		return -ENOTSUP;
	}

	return alg->api->enroll_step(alg, image, completion);
}

/**
 * Finish enroll session
 *
 * @param alg Pointer to fingerprint algorithm instance.
 * @param templ Pointer to buffer where template will be stored.
 * @return 0 on success.
 * @return -ENOTSUP if the algorithm doesn't implement the function.
 * @return negative value on error.
 */
static inline int
fingerprint_enroll_finish(const struct fingerprint_algorithm *const alg,
			  void *templ)
{
	__ASSERT_NO_MSG(alg != NULL);
	if (alg->api->enroll_finish == NULL) {
		return -ENOTSUP;
	}

	return alg->api->enroll_finish(alg, templ);
}

/**
 * Match image against templates
 *
 * @param alg Pointer to fingerprint algorithm instance.
 * @param templ Pointer to buffer where template will be stored.
 * @param templ_count Number of templates in 'templ' buffer.
 * @param image Pointer to finger image.
 * @param match_index Pointer to variable where index of matched template will
 *                    be stored.
 * @param update_bitmap Pointer to variable where bitmap of update templates
 *                      will be stored.
 * @return fp_match_status enum on success.
 * @return -ENOTSUP if the algorithm doesn't implement the function.
 * @return negative value on error.
 */
static inline int
fingerprint_match(const struct fingerprint_algorithm *const alg, void *templ,
		  uint32_t templ_count, const uint8_t *const image,
		  int32_t *match_index, uint32_t *update_bitmap)
{
	__ASSERT_NO_MSG(alg != NULL);
	__ASSERT_NO_MSG(templ != NULL);
	if (alg->api->match == NULL) {
		return -ENOTSUP;
	}

	return alg->api->match(alg, templ, templ_count, image, match_index,
			       update_bitmap);
}

#endif /* ZEPHYR_INCLUDE_FINGERPRINT_FINGERPRINT_ALG_H_ */
