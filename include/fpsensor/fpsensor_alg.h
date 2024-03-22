/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor interface */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_ALG_H
#define __CROS_EC_FPSENSOR_FPSENSOR_ALG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Compares given finger image against enrolled templates.
 *
 * The matching algorithm can update the template with additional biometric data
 * from the image, if it chooses to do so.
 *
 * @param[in,out] templ a pointer to the array of template buffers.
 * @param templ_count the number of buffers in the array of templates.
 * @param[in] image the buffer containing the finger image
 * @param match_index index of the matched finger in the template array if any.
 * @param[out] update_bitmap contains one bit per template, the bit is set if
 * the match has updated the given template.
 *
 * @return EC_MKBP_FP_ERR_MATCH_NO on non-match
 * @return EC_MKBP_FP_ERR_MATCH_YES for match when template was not updated with
 * new data
 * @return EC_MKBP_FP_ERR_MATCH_YES_UPDATED for match when template was updated
 * @return EC_MKBP_FP_ERR_MATCH_YES_UPDATE_FAILED match, but update failed (not
 * saved)
 * @return EC_MKBP_FP_ERR_MATCH_LOW_QUALITY when matching could not be performed
 * due to low image quality
 * @return EC_MKBP_FP_ERR_MATCH_LOW_COVERAGE when matching could not be
 * performed due to finger covering too little area of the sensor
 * @return negative value on error
 */
int fp_finger_match(void *templ, uint32_t templ_count, uint8_t *image,
		    int32_t *match_index, uint32_t *update_bitmap);

/**
 * Start a finger enrollment session.
 *
 * @return 0 on success
 * @return negative value on error
 */
int fp_enrollment_begin(void);

/**
 * Generate a template from the finger whose enrollment has just being
 * completed.
 *
 * @param[out] templ template buffer, which can be set to NULL to abort current
 * enrollment process.
 *
 * @return 0 on success, negative value on error
 */
int fp_enrollment_finish(void *templ);

/**
 * Adds fingerprint image to the current enrollment session.
 *
 * @param[in] image Image to add to enrollment
 * @param[out] completion The percentage of the enrollment process that is
 * complete: [0-100].
 *
 * @return 0 on success
 * @return EC_MKBP_FP_ERR_ENROLL_OK when image was successfully enrolled
 * @return EC_MKBP_FP_ERR_ENROLL_IMMOBILE when image added, but user should be
 * advised to move finger
 * @return EC_MKBP_FP_ERR_ENROLL_LOW_QUALITY when image could not be used due to
 * low image quality
 * @return EC_MKBP_FP_ERR_ENROLL_LOW_COVERAGE when image could not be used
 * due to finger covering too little area of the sensor
 */
int fp_finger_enroll(uint8_t *image, int *completion);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_ALG_H */
