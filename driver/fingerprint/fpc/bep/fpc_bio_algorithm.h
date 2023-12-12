/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_FPC_BIO_ALGORITHM_H
#define __CROS_EC_FPC_BIO_ALGORITHM_H

#include "common.h"

#include <stdint.h>

/*
 * An opaque pointer representing an image (scan).
 */
typedef void *bio_image_t;
/*
 * An opaque pointer representing/uniquely identifying an (serialized) enrolled
 * template.
 */
typedef void *bio_template_t;
/*
 * An opaque pointer representing/uniquely identifying enrollment attempt.
 */
typedef void *bio_enrollment_t;
/*
 * An opaque struct representing algorithm.
 */
typedef struct fpc_bep_algorithm fpc_bep_algorithm_t;
/*
 * Struct with biometric algorithm information.
 */
typedef struct {
	const fpc_bep_algorithm_t *algorithm;
	uint32_t template_size;
} fpc_bio_info_t;
/*
 * Initializes biometric algorithm library. Should be the very first function
 * to be invoked by the biometric daemon.
 *
 * Returns 0 on success, negative error code (such as -ENOMEM) on failure.
 */
__staticlib int bio_algorithm_init(void);
/*
 * Instructs the biometric library to release all resources in preparation
 * for the process termination (or unloading the library). Regardless of
 * the returned error code the action is considered unrecoverable.
 *
 * Returns 0 on success, negative error code (such as -ENOMEM) on failure.
 */
__staticlib int bio_algorithm_exit(void);
/*
 * Compares given biometric image against a list of enrolled template(s).
 * In case the image match a template the match_index will indicate which
 * template in the list that matched.
 * The algorithm library can update templates with additional biometric data
 * from the image, if it chooses to do so. The updated template(s) will be
 * indicated by the out parameter 'updated_templates', a bit-field where
 * updated template(s) indicated by the corresponding bit being set
 * Returns:
 * - negative value on error
 * - BIO_TEMPLATE_NO_MATCH on non-match
 * - BIO_TEMPLATE_MATCH for match when template was not updated with new data
 * - BIO_TEMPLATE_MATCH_UPDATED for match when template was updated
 * - BIO_TEMPLATE_MATCH_UPDATE_FAILED match, but update failed (do not save)
 * - BIO_TEMPLATE_LOW_QUALITY when matching could not be performed due to low
 *   image quality
 * - BIO_TEMPLATE_LOW_COVERAGE when matching could not be performed due to
 *   finger covering too little area of the sensor
 */
#define BIO_TEMPLATE_NO_MATCH 0
#define BIO_TEMPLATE_MATCH 1
#define BIO_TEMPLATE_MATCH_UPDATED 3
#define BIO_TEMPLATE_MATCH_UPDATE_FAILED 5
#define BIO_TEMPLATE_LOW_QUALITY 2
#define BIO_TEMPLATE_LOW_COVERAGE 4

__staticlib int bio_template_image_match_list(bio_template_t templ,
					      uint32_t num_templ,
					      bio_image_t image,
					      int32_t *match_index,
					      uint32_t *updated_templ);
/*
 * Initiates biometric data enrollment process. Algorithm library returns
 * 'enrollment handle' that is used for all subsequent enrollment operations.
 *
 * Returns 0 on success, negative error code (such as -ENOMEM) on failure.
 */
__staticlib int bio_enrollment_begin(bio_enrollment_t *enrollment);
/*
 * Adds fingerprint image to an enrollment.
 *
 * The library should expect to copy any relevant data from the “image”
 * as it is likely to be destroyed (via bio_image_destroy() call) shortly after
 * this call completes.
 *
 * Returns:
 * - negative value on error
 * - BIO_ENROLLMENT_OK when image was successfully enrolled
 * - BIO_ENROLLMENT_LOW_QUALITY when image could not be used due to low
 *   image quality
 * - BIO_ENROLLMENT_IMMOBILE when image added, but user should be advised
 *   to move finger
 * - BIO_ENROLLMENT_LOW_COVERAGE when image could not be used due to
 *   finger covering too little area of the sensor
 * - BIO_ENROLLMENT_INTERNAL_ERROR when an internal error occurred
 */
#define BIO_ENROLLMENT_OK 0
#define BIO_ENROLLMENT_LOW_QUALITY 1
#define BIO_ENROLLMENT_IMMOBILE 2
#define BIO_ENROLLMENT_LOW_COVERAGE 3
#define BIO_ENROLLMENT_INTERNAL_ERROR 5

/* Can be used to detect if image was usable for enrollment or not. */
#define BIO_ENROLLMENT_PROBLEM_MASK 1
__staticlib int bio_enrollment_add_image(bio_enrollment_t enrollment,
					 bio_image_t image);
/*
 * Returns percent of coverage accumulated during enrollment process.
 * Optional method. Regardless of value returned by this call user should call
 * bio_enrollment_is_complete() to check if algorithm library accumulated enough
 * data to create a template.
 *
 * Returns value in the range 0..100, or negative error (such as -EINVAL);
 */
__staticlib int
bio_enrollment_get_percent_complete(bio_enrollment_t enrollment);
/*
 * Indicates that given enrollment process is complete, and algorithm library
 * should generate an active template that can be used in subsequent calls
 * to bio_image_match() and bio_template_serialize() from enrollment data.
 * After the template is created the library should release all resources
 * associated with this enrollment.
 *
 * Argument 'templ' is optional and can be set to NULL if caller wishes to
 * abort enrollment process.
 *
 * Returns 0 on success, negative error code (such as -EINVAL) on failure.
 */
__staticlib int bio_enrollment_finish(bio_enrollment_t enrollment,
				      bio_template_t *templ);

#endif /* __CROS_EC_FPC_BIO_ALGORITHM_H */
