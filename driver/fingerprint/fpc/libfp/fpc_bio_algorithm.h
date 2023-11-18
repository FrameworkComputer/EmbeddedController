/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef BIOD_BIO_ALGORITHM_H_
#define BIOD_BIO_ALGORITHM_H_

#include "common.h"

#include <stddef.h>
#include <stdint.h>

#include <sys/types.h>

enum bio_algorithm_type {
	BIO_ALGORITHM_FINGERPRINT,
	BIO_ALGORITHM_IRIS,
};
/*
 * An opaque pointer representing/uniquely identifying a sensor.
 */
typedef void *bio_sensor_t;
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
 * Used to retrieve type of the algorithm library. Might be used by
 * configuration processor module to match sensors and algorithm libraries.
 */
__staticlib enum bio_algorithm_type bio_algorithm_get_type(void);
/*
 * Used to retrieve name of the algorithm library, to be used in diagnostics.
 * Also might be used by configuration processor module to match sensors and
 * algorithm libraries.
 */
__staticlib const char *bio_algorithm_get_name(void);
/*
 * Used to retrieve version of the algorithm library, to be used in diagnostics.
 */
__staticlib const char *bio_algorithm_get_version(void);
/*
 * Used to retrieve additional information from the algorithm library, to be
 * used in diagnostics.
 */
__staticlib const char *bio_algorithm_get_banner(void);
/*
 * Initializes a new sensor structure and returns its handle that will be used
 * in other calls to identify the sensor involved in the operation.
 *
 * Returns 0 on success, negative error code (such as -ENOMEM) on failure.
 */
__staticlib int bio_sensor_create(bio_sensor_t *sensor);
/*
 * Releases all resources held by the library in conjunction with given sensor.
 *
 * Returns 0 on success, negative error code (such as -EINVAL) on failure.
 */
__staticlib int bio_sensor_destroy(bio_sensor_t sensor);
/*
 * Communicates particulars of a given sensor so that algorithm library can
 * adjust its behavior as needed.
 *
 * Returns 0 on success, negative error code (such as -EINVAL) on failure.
 */
__staticlib int bio_sensor_set_model(bio_sensor_t sensor, uint32_t vendor_id,
				     uint32_t product_id, uint32_t model_id,
				     uint32_t version);
/*
 * Communicates format of data used by given sensor to the algorithm library.
 * This is a fourcc value defined by V4L2 API.
 * Could be a new define for biometric sensors or V4L2_PIX_FMT_GREY.
 * Algorithm library will return error if it can not work with given format.
 *
 * Returns 0 on success, negative error code (such as -EINVAL) on failure.
 */
__staticlib int bio_sensor_set_format(bio_sensor_t sensor,
				      uint32_t pixel_format);
/*
 * Communicates dimensions of given sensor to the algorithm library.
 *
 * Returns 0 on success, negative error code (such as -EINVAL) on failure.
 */
__staticlib int bio_sensor_set_size(bio_sensor_t sensor, uint32_t width,
				    uint32_t height);
/*
 * Instructs the algorithm library to initialize a new structure to hold
 * biometric image of given dimensions acquired from given sensor.
 * It will return image handle that will be used in other calls to identify
 * the image involved in the operation.
 *
 * Returns 0 on success, negative error code (such as -ENOMEM) on failure.
 */
__staticlib int bio_image_create(bio_sensor_t sensor, uint32_t width,
				 uint32_t height, bio_image_t *image);
/*
 * Communicates dimensions of image to the algorithm library.
 * Can be used if image is less than full sensor resolution.
 *
 * Returns 0 on success, negative error code (such as -EINVAL) on failure.
 */
__staticlib int bio_image_set_size(bio_image_t image, uint32_t width,
				   uint32_t height);
/*
 * Attaches data from biometric sensor to image structure. The caller must
 * ensure that there is enough of data for given image dimensions for given
 * format used by the sensor.
 *
 * It is assumes that the data pointer stays valid until bio_image_destroy()
 * is called.
 *
 * Returns 0 on success, negative error code (such as -EINVAL) on failure.
 */
__staticlib int bio_image_set_data(bio_image_t image, const uint8_t *data,
				   size_t size);
/*
 * Releases all resources held by the library in conjunction with given image.
 *
 * Returns 0 on success, negative error code (such as -EINVAL) on failure.
 */
__staticlib int bio_image_destroy(bio_image_t image);

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
__staticlib int bio_template_image_match_list(bio_template_t tmpl,
					      uint32_t num_templates,
					      bio_image_t image,
					      int32_t *match_index,
					      uint32_t *updated_templates);
__staticlib int bio_template_image_match(bio_template_t tmpl,
					 bio_image_t image);
/*
 * Returns size of template data in serialized form.
 *
 * Returns negative error code (such as -EINVAL) on failure, or size of the
 * serialized form in bytes.
 */
__staticlib ssize_t bio_template_get_serialized_size(bio_template_t tmpl);
/*
 * Releases all resources held by the library in conjunction with given
 * template.
 *
 * Returns 0 on success, negative error code (such as -EINVAL) on failure.
 */
__staticlib int bio_template_destroy(bio_template_t tmpl);
/*
 * Initiates biometric data enrollment process. Algorithm library returns
 * 'enrollment handle' that is used for all subsequent enrollment operations.
 *
 * Returns 0 on success, negative error code (such as -ENOMEM) on failure.
 */
__staticlib int bio_enrollment_begin(bio_sensor_t sensor,
				     bio_enrollment_t *enrollment);
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
 * - BIO_ENROLLMENT_IMMOBILE when image added, but user should be advised
 *   to move finger
 * - BIO_ENROLLMENT_LOW_QUALITY when image could not be used due to low
 *   image quality
 * - BIO_ENROLLMENT_LOW_COVERAGE when image could not be used  due to
 *   finger covering too little area of the sensor
 */
#define BIO_ENROLLMENT_OK 0
#define BIO_ENROLLMENT_IMMOBILE 2
#define BIO_ENROLLMENT_LOW_QUALITY 1
#define BIO_ENROLLMENT_LOW_COVERAGE 3
/* Can be used to detect if image was usable for enrollment or not. */
#define BIO_ENROLLMENT_PROBLEM_MASK 1
__staticlib int bio_enrollment_add_image(bio_enrollment_t enrollment,
					 bio_image_t image);
/*
 * Indicates whether there is enough data in the enrollment for it to be
 * converted into a template to be used for identification.
 *
 * Returns 0 for if enrollment does not have enough data yet, 1 if enrollment
 * is complete, or negative error code (such as -EINVAL) on failure.
 *
 */
__staticlib int bio_enrollment_is_complete(bio_enrollment_t enrollment);
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
 * Argument 'tmpl' is optional and can be set to NULL if caller wishes to
 * abort enrollment process.
 *
 * Returns 0 on success, negative error code (such as -EINVAL) on failure.
 */
__staticlib int bio_enrollment_finish(bio_enrollment_t enrollment,
				      bio_template_t *tmpl);

typedef struct {
	int32_t coverage; /* Sensor coverage in range [0..100]        */
	int32_t quality; /* Image quality in range [0..100]          */
	int32_t min_coverage; /* Minimum coverage accepted by enroll      */
	int32_t min_quality; /* Minimum image quality accepted by enroll */
} bio_image_status_t;

/*
 * Get the image quality and threshold values for a bio_image_t
 *
 * @param[in]  image         Image data as acquired by
 *                           fp_sensor_acquire_image_with_mode
 * @param[out] image_status  Populated structure with quality/coverage values
 *                           and corresponding threshold values
 *
 * @note This function will alter the internal states of the bio algorithm
 *       library and must not be used during an enroll sequence. The typical
 *       use case for this function is to qualify images during image
 *       collection.
 *
 * @return negative on error.
 * @return 0 if the quality and coverage threshold values aren't reached.
 * @return 1 if the quality and coverage threshold values are reached.
 */
__staticlib int bio_sensor_get_image_status(bio_image_t image,
					    bio_image_status_t *image_status);

#endif /* BIOD_BIO_ALGORITHM_H_ */
