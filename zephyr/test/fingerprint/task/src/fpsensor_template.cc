/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if defined(CONFIG_NEWLIB_LIBC) || defined(CONFIG_EXTERNAL_LIBC)
/* For srandom() */
#define _XOPEN_SOURCE 500
#endif

#include "mock_fingerprint_algorithm.h"

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <drivers/fingerprint.h>
#include <drivers/fingerprint_sim.h>
#include <ec_commands.h>
#include <ec_tasks.h>
#include <flash.h>
#include <fpsensor/fpsensor_state.h>
#include <host_command.h>
#include <mkbp_event.h>
#include <rollback.h>
#include <rollback_private.h>
#include <system.h>

#define ROLLBACK0_ADDR DT_REG_ADDR(DT_NODELABEL(rollback0))
#define ROLLBACK0_SIZE DT_REG_SIZE(DT_NODELABEL(rollback0))

#define ROLLBACK1_ADDR DT_REG_ADDR(DT_NODELABEL(rollback1))
#define ROLLBACK1_SIZE DT_REG_SIZE(DT_NODELABEL(rollback1))

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);
FAKE_VALUE_FUNC(int, system_is_locked);

#define fp_sim DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))
#define IMAGE_SIZE                                                 \
	MAX_FROM_LIST(LISTIFY(NUM_IMAGE_CAPTURE_TYPES,             \
			      FINGERPRINT_SENSOR_FRAME_SIZE, (, ), \
			      DT_CHOSEN(cros_fp_fingerprint_sensor)))
static uint8_t frame_buffer[IMAGE_SIZE];

static const uint8_t fake_rollback_entropy[] = "some_rollback_entropy";

/* The fake TPM seed is "very_secret_32_bytes_of_tpm_seed" */
#define FAKE_TPM_SEED                                                       \
	{ 0x76, 0x65, 0x72, 0x79, 0x5f, 0x73, 0x65, 0x63, 0x72, 0x65, 0x74, \
	  0x5f, 0x33, 0x32, 0x5f, 0x62, 0x79, 0x74, 0x65, 0x73, 0x5f, 0x6f, \
	  0x66, 0x5f, 0x74, 0x70, 0x6d, 0x5f, 0x73, 0x65, 0x65, 0x64 }

/* The fake UserID is "i_m_a_fake_user_id_used_for_test" */
#define FAKE_USER_ID                                      \
	{ 0x5f6d5f69, 0x61665f61, 0x755f656b, 0x5f726573, \
	  0x755f6469, 0x5f646573, 0x5f726f66, 0x74736574 }

/*
 * How to manually encrypt a template
 *
 * After adding "some_rollback_entropy" entropy to empty rollback secret
 * (32 bytes of 0x00), the secret stored in rollback region is
 * 3ce9c8011d3f98d96fa741da4f10f2f410d80372ebba98ff726b521338e6cfd9
 *
 * The IKM (input key material) is a concatenation of the rollback secret and
 * the TPM seed, so it's
 * 3ce9c8011d3f98d96fa741da4f10f2f410d80372ebba98ff726b521338e6cfd9
 * 766572795f7365637265745f33325f62797465735f6f665f74706d5f73656564
 *
 * UserID is 695f6d5f615f66616b655f757365725f69645f757365645f666f725f74657374
 *
 * To encrypt the template we also need nonce and encryption salt.
 * We get these values from entropy source. In the test environment
 * our entropy source is random(), which is initialized using
 * srandom(0xdeadc0de) in fpsensor_before(), so:
 *
 * The nonce is 941ee3310b891aeba42b2e90
 * Encryption salt is fa253e21359a7920ba1a9868cc26a88f
 *
 * The key used for encryption is HKDF-SHA256(encryption_salt, ikm, user_id)
 * 1a5146988b7e7423fc11c99b5edd6c22
 *
 * The template is appended with positive match salt which is obtained from TRNG
 * in our case it is 64c55d3ee3b3a45ef85bce1ed5caf1e0
 *
 * Finally, we can encrypt (template || positive match salt) with AES-GCM which
 * will give 020dc692bb286c1001e35f17c368053c15c07cd88bf640c897986ddbde23390059
 * ea827f4fee49955c38f0caeb69730e with tag a1ba7aa11e61194bdf7627a72f25d2f8
 */

static const uint8_t example_template[CONFIG_FP_ALGORITHM_TEMPLATE_SIZE] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
	0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
	0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
};

static const struct ec_fp_template_encryption_metadata expected_enc_info = {
	.struct_version = 4,
	.nonce = { 0x94, 0x1e, 0xe3, 0x31, 0x0b, 0x89, 0x1a, 0xeb, 0xa4, 0x2b,
		   0x2e, 0x90 },
	.encryption_salt = { 0xfa, 0x25, 0x3e, 0x21, 0x35, 0x9a, 0x79, 0x20,
			     0xba, 0x1a, 0x98, 0x68, 0xcc, 0x26, 0xa8, 0x8f },
	.tag = { 0xa1, 0xba, 0x7a, 0xa1, 0x1e, 0x61, 0x19, 0x4b, 0xdf, 0x76,
		 0x27, 0xa7, 0x2f, 0x25, 0xd2, 0xf8 },
};

static const uint8_t
	example_template_encrypted[CONFIG_FP_ALGORITHM_TEMPLATE_SIZE +
				   FP_POSITIVE_MATCH_SALT_BYTES] = {
		0x02, 0x0d, 0xc6, 0x92, 0xbb, 0x28, 0x6c, 0x10, 0x01, 0xe3,
		0x5f, 0x17, 0xc3, 0x68, 0x05, 0x3c, 0x15, 0xc0, 0x7c, 0xd8,
		0x8b, 0xf6, 0x40, 0xc8, 0x97, 0x98, 0x6d, 0xdb, 0xde, 0x23,
		0x39, 0x00, 0x59, 0xea, 0x82, 0x7f, 0x4f, 0xee, 0x49, 0x95,
		0x5c, 0x38, 0xf0, 0xca, 0xeb, 0x69, 0x73, 0x0e
	};

/*
 * Positive match secret is a HKDF-SHA256(positive_match_salt, ikm, message)
 *
 * The message is a concatenation of string "positive_match_secret for user "
 * and userid. In our case it's 706f7369746976655f6d617463685f7365637265742066
 * 6f72207573657220695f6d5f615f66616b655f757365725f69645f757365645f666f725f74
 * 657374
 *
 * The positive match secret is: 69af2f7a35a249d6584bda3ba9f99f872aa560cce9c4
 * 1f832bf2cdd26e29a76b
 */
static const uint8_t
	example_positive_match_secret[FP_POSITIVE_MATCH_SECRET_BYTES] = {
		0x69, 0xaf, 0x2f, 0x7a, 0x35, 0xa2, 0x49, 0xd6,
		0x58, 0x4b, 0xda, 0x3b, 0xa9, 0xf9, 0x9f, 0x87,
		0x2a, 0xa5, 0x60, 0xcc, 0xe9, 0xc4, 0x1f, 0x83,
		0x2b, 0xf2, 0xcd, 0xd2, 0x6e, 0x29, 0xa7, 0x6b
	};

static int enroll_percent;
static int custom_enroll_step(const struct fingerprint_algorithm *const alg,
			      const uint8_t *const image, int *percent)
{
	*percent = enroll_percent;

	return 0;
}

static int custom_enroll_finish(const struct fingerprint_algorithm *const alg,
				void *templ)
{
	memcpy((uint8_t *)templ, example_template, sizeof(example_template));

	return 0;
}

static uint8_t encrypted_template[FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE];

static const size_t test_info_buffer_size =
	sizeof(struct ec_response_fp_info_v3) +
	sizeof(struct fp_image_frame_params_v2) * FP_MAX_CAPTURE_TYPES;
static uint8_t buffer[test_info_buffer_size];
static struct ec_response_fp_info_v3 *test_info_buffer =
	(struct ec_response_fp_info_v3 *)buffer;

/*
 * Size of params buffer for FP_TEMPLATE command. Its size must be big enough
 * to keep ec_params_fp_template structure and a part of template.
 */
#define FP_TEMPLATE_PARAMS_BUFFER_SIZE 16
BUILD_ASSERT(FP_TEMPLATE_PARAMS_BUFFER_SIZE >
	     sizeof(struct ec_params_fp_template));

#define FP_TEMPLATE_V1_PARAMS_BUFFER_SIZE 32
BUILD_ASSERT(FP_TEMPLATE_V1_PARAMS_BUFFER_SIZE >
	     sizeof(struct ec_params_fp_template_v1));

ZTEST_USER(fpsensor_template, test_fp_template_v1_load_template_success)
{
	uint8_t params_buffer[FP_TEMPLATE_V1_PARAMS_BUFFER_SIZE];
	struct ec_params_fp_template_v1 *params =
		(struct ec_params_fp_template_v1 *)params_buffer;

	const size_t data_size =
		FP_TEMPLATE_V1_PARAMS_BUFFER_SIZE -
		offsetof(struct ec_params_fp_template_v1, data);
	uint8_t *data =
		params_buffer + offsetof(struct ec_params_fp_template_v1, data);
	size_t offset = 0;

	memcpy(encrypted_template, &expected_enc_info,
	       sizeof(struct ec_fp_template_encryption_metadata));
	memcpy(encrypted_template +
		       sizeof(struct ec_fp_template_encryption_metadata),
	       example_template_encrypted,
	       CONFIG_FP_ALGORITHM_TEMPLATE_SIZE +
		       FP_POSITIVE_MATCH_SALT_BYTES);

	params->cmd = FP_TEMPLATE_LOAD;
	while (offset < FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE) {
		params->offset = offset;
		params->size =
			MIN(data_size,
			    FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE - offset);
		memcpy(data, encrypted_template + offset, params->size);

		zassert_ok(ec_cmd_fp_template_v1(
			NULL, params,
			params->size + offsetof(struct ec_params_fp_template_v1,
						data)));

		offset += params->size;
	}

	/* Start decryption. */
	params->cmd = FP_TEMPLATE_DECRYPT;
	params->offset = 0;
	params->size = 0;
	zassert_ok(ec_cmd_fp_template_v1(
		NULL, params, offsetof(struct ec_params_fp_template_v1, data)));

	/* Wait for decryption to finish. */
	params->cmd = FP_TEMPLATE_GET_RESULT;
	while (ec_cmd_fp_template_v1(NULL, params,
				     offsetof(struct ec_params_fp_template_v1,
					      data)) == EC_RES_BUSY) {
		k_msleep(1);
	}

	/* Confirm that there is 1 valid template. */
	zassert_ok(ec_cmd_fp_info_v3(NULL, test_info_buffer,
				     test_info_buffer_size));
	zassert_equal(test_info_buffer->template_info.template_valid, 1);
}

ZTEST_USER(fpsensor_template, test_fp_template_v1_load_template_invalid_tag)
{
	uint8_t params_buffer[FP_TEMPLATE_V1_PARAMS_BUFFER_SIZE];
	struct ec_params_fp_template_v1 *params =
		(struct ec_params_fp_template_v1 *)params_buffer;

	const size_t data_size =
		FP_TEMPLATE_V1_PARAMS_BUFFER_SIZE -
		offsetof(struct ec_params_fp_template_v1, data);
	uint8_t *data =
		params_buffer + offsetof(struct ec_params_fp_template_v1, data);
	size_t offset = 0;

	struct ec_fp_template_encryption_metadata enc_info_with_invalid_tag =
		expected_enc_info;

	/* Corrupt the tag. We expect that the template will be rejected. */
	enc_info_with_invalid_tag.tag[0] = expected_enc_info.tag[0] ^ 0xFF;

	memcpy(encrypted_template, &enc_info_with_invalid_tag,
	       sizeof(struct ec_fp_template_encryption_metadata));
	memcpy(encrypted_template +
		       sizeof(struct ec_fp_template_encryption_metadata),
	       example_template_encrypted,
	       CONFIG_FP_ALGORITHM_TEMPLATE_SIZE +
		       FP_POSITIVE_MATCH_SALT_BYTES);

	params->cmd = FP_TEMPLATE_LOAD;
	while (offset < FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE) {
		params->offset = offset;
		params->size =
			MIN(data_size,
			    FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE - offset);
		memcpy(data, encrypted_template + offset, params->size);

		zassert_ok(ec_cmd_fp_template_v1(
			NULL, params,
			params->size + offsetof(struct ec_params_fp_template_v1,
						data)));

		offset += params->size;
	}

	/* Start decryption. */
	params->cmd = FP_TEMPLATE_DECRYPT;
	params->offset = 0;
	params->size = 0;
	zassert_ok(ec_cmd_fp_template_v1(
		NULL, params, offsetof(struct ec_params_fp_template_v1, data)));

	/* Wait for decryption to finish. */
	params->cmd = FP_TEMPLATE_GET_RESULT;
	int status;
	while ((status = ec_cmd_fp_template_v1(
			NULL, params,
			offsetof(struct ec_params_fp_template_v1, data))) ==
	       EC_RES_BUSY) {
		k_msleep(1);
	}

	/* Expect decryption failure (EC_RES_UNAVAILABLE). */
	zassert_equal(EC_RES_UNAVAILABLE, status);

	/* Confirm that there is no valid template. */
	zassert_ok(ec_cmd_fp_info_v3(NULL, test_info_buffer,
				     test_info_buffer_size));
	zassert_equal(test_info_buffer->template_info.template_valid, 0);
}

ZTEST_USER(fpsensor_template, test_fp_template_v1_busy)
{
	struct ec_params_fp_template_v1 params = {
		.cmd = FP_TEMPLATE_LOAD,
	};

	/* Simulate crypto operation in progress. */
	global_context.sensor_mode |= FP_MODE_ENCRYPT_TEMPLATE;

	zassert_equal(EC_RES_BUSY,
		      ec_cmd_fp_template_v1(NULL, &params, sizeof(params)));

	params.cmd = FP_TEMPLATE_DECRYPT;
	zassert_equal(EC_RES_BUSY,
		      ec_cmd_fp_template_v1(NULL, &params, sizeof(params)));

	global_context.sensor_mode &= ~FP_MODE_ENCRYPT_TEMPLATE;
}

ZTEST_USER(fpsensor_template, test_fp_template_v0_busy)
{
	struct ec_params_fp_template_v1 params_v1 = {
		.cmd = FP_TEMPLATE_DECRYPT,
	};
	struct ec_params_fp_template params_v0 = {
		.offset = 0,
		.size = 0,
	};

	/* Start decryption via v1 command. */
	zassert_ok(ec_cmd_fp_template_v1(NULL, &params_v1, sizeof(params_v1)));

	/* v0 command should return BUSY. */
	zassert_equal(EC_RES_BUSY,
		      ec_cmd_fp_template(NULL, &params_v0, sizeof(params_v0)));

	/* Give opportunity for fpsensor task to finish. */
	k_msleep(1);
}

ZTEST_USER(fpsensor_template, test_fp_frame_v0_busy)
{
	struct ec_params_fp_frame_v1 params_v1 = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
	};
	struct ec_params_fp_frame params_v0 = {
		.offset = FP_FRAME_INDEX_TEMPLATE << FP_FRAME_INDEX_SHIFT,
		.size = 0,
	};

	/* We need at least one valid template for FP_FRAME_ENCRYPT_TEMPLATE. */
	global_context.templ_valid = 1;

	/* Start encryption via v1 command. */
	zassert_ok(ec_cmd_fp_frame_v1(NULL, &params_v1, NULL));

	/* v0 command should return BUSY. */
	zassert_equal(EC_RES_BUSY,
		      ec_cmd_fp_frame(NULL, &params_v0, frame_buffer));

	/* Give opportunity for fpsensor task to finish. */
	k_msleep(1);

	/* b/114160734: Not more than 1 encrypted message per second. */
	k_sleep(K_SECONDS(1));
}

ZTEST_USER(fpsensor_template, test_fp_frame_v0_raw_image_not_busy)
{
	struct ec_params_fp_template_v1 params_v1 = {
		.cmd = FP_TEMPLATE_DECRYPT,
	};
	struct ec_params_fp_frame params_v0 = {
		.offset = FP_FRAME_INDEX_RAW_IMAGE << FP_FRAME_INDEX_SHIFT,
		.size = 0,
	};

	/* Start decryption via v1 command. */
	zassert_ok(ec_cmd_fp_template_v1(NULL, &params_v1, sizeof(params_v1)));

	/*
	 * Confirm that getting raw image is NOT blocked by crypto operation.
	 *
	 * Note: It will return EC_RES_INVALID_PARAM because no image was
	 * captured, but it must NOT be EC_RES_BUSY.
	 */
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_fp_frame(NULL, &params_v0, frame_buffer));

	/* Give opportunity for fpsensor task to finish. */
	k_msleep(1);
}

ZTEST_USER(fpsensor_template, test_fp_template_v1_overflow)
{
	struct ec_params_fp_template_v1 params = {
		.cmd = FP_TEMPLATE_LOAD,
	};

	/* Simulate maximum number of templates reached. */
	global_context.templ_valid = FP_MAX_FINGER_COUNT;

	zassert_equal(EC_RES_OVERFLOW,
		      ec_cmd_fp_template_v1(NULL, &params, sizeof(params)));

	global_context.templ_valid = 0;
}

ZTEST_USER(fpsensor_template, test_fp_template_v1_invalid_params)
{
	uint8_t params_buffer[FP_TEMPLATE_V1_PARAMS_BUFFER_SIZE];
	struct ec_params_fp_template_v1 *params =
		(struct ec_params_fp_template_v1 *)params_buffer;

	params->cmd = FP_TEMPLATE_LOAD;
	params->offset = 0;
	params->size = 8;

	/* 1. Incorrect args->params_size. */
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_fp_template_v1(
			      NULL, params,
			      offsetof(struct ec_params_fp_template_v1, data) +
				      params->size + 1));

	/* 2. Offset/size out of bounds. */
	params->offset = FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE;
	params->size = 1;
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_fp_template_v1(
			      NULL, params,
			      offsetof(struct ec_params_fp_template_v1, data) +
				      params->size));
}

ZTEST_USER(fpsensor_template, test_fp_template_v1_sequential_integrity)
{
	struct ec_params_fp_template_v1 params = {
		.offset = 0,
		.size = 0,
		.cmd = FP_TEMPLATE_LOAD,
	};

	/* Set FP_ENCRYPTED_TEMPLATE_READY and valid encrypted id. */
	global_context.fp_encryption_status |= FP_ENCRYPTED_TEMPLATE_READY;
	global_context.template_encrypted_id = 0;

	/* Loading any template chunk should clear them. */
	zassert_ok(ec_cmd_fp_template_v1(NULL, &params, sizeof(params)));

	zassert_false(global_context.fp_encryption_status &
		      FP_ENCRYPTED_TEMPLATE_READY);
	zassert_equal(global_context.template_encrypted_id,
		      FP_NO_SUCH_TEMPLATE);
}

ZTEST_USER(fpsensor_template, test_fp_frame_raw_image_system_is_locked)
{
	struct ec_params_fp_frame frame_request = {
		.offset = FP_FRAME_INDEX_RAW_IMAGE << FP_FRAME_INDEX_SHIFT,
		.size = IMAGE_SIZE,
	};

	/* Lock the system. */
	system_is_locked_fake.return_val = true;

	/*
	 * Confirm that it's not possible to get raw image when system is
	 * locked.
	 */
	zassert_equal(ec_cmd_fp_frame(NULL, &frame_request, frame_buffer),
		      EC_RES_ACCESS_DENIED);
}

ZTEST_USER(fpsensor_template, test_fp_frame_v1_raw_image_system_is_locked)
{
	struct ec_params_fp_frame_v1 frame_request = {
		.cmd = FP_FRAME_GET_RAW_IMAGE,
		.offset = 0,
		.size = IMAGE_SIZE,
	};

	/* Lock the system. */
	system_is_locked_fake.return_val = true;

	/*
	 * Confirm that it's not possible to get raw image when system is
	 * locked.
	 */
	zassert_equal(ec_cmd_fp_frame_v1(NULL, &frame_request, frame_buffer),
		      EC_RES_ACCESS_DENIED);
}

ZTEST_USER(fpsensor_template, test_fp_frame_raw_image_size_too_big)
{
	uint8_t buffer[IMAGE_SIZE + 1];
	struct ec_params_fp_frame frame_request = {
		.offset = FP_FRAME_INDEX_RAW_IMAGE << FP_FRAME_INDEX_SHIFT,
		.size = IMAGE_SIZE + 1,
	};

	/*
	 * Confirm that FP_FRAME host command will return an error when
	 * requested more than fingerprint frame size.
	 */
	zassert_equal(ec_cmd_fp_frame(NULL, &frame_request, buffer),
		      EC_RES_INVALID_PARAM);
}

ZTEST_USER(fpsensor_template, test_fp_frame_raw_image_bad_offset)
{
	struct ec_params_fp_frame frame_request = {
		.offset = ((FP_FRAME_INDEX_RAW_IMAGE << FP_FRAME_INDEX_SHIFT) |
			   (IMAGE_SIZE + 1)),
		.size = 1,
	};

	/*
	 * Confirm that FP_FRAME host command will return an error when
	 * trying to read from bad offset.
	 */
	zassert_equal(ec_cmd_fp_frame(NULL, &frame_request, frame_buffer),
		      EC_RES_INVALID_PARAM);
}

ZTEST_USER(fpsensor_template, test_fp_frame_no_template)
{
	struct ec_params_fp_frame template_request = {
		.offset = FP_FRAME_INDEX_TEMPLATE << FP_FRAME_INDEX_SHIFT,
		.size = sizeof(encrypted_template),
	};

	/* Get encrypted template (should fail with EC_RES_UNAVAILABLE). */
	zassert_equal(EC_RES_UNAVAILABLE,
		      ec_cmd_fp_frame(NULL, &template_request,
				      encrypted_template));
}

ZTEST_USER(fpsensor_template, test_fp_frame_template_id_out_of_range)
{
	struct ec_params_fp_frame template_request = {
		.offset = (FP_FRAME_INDEX_TEMPLATE + FP_MAX_FINGER_COUNT)
			  << FP_FRAME_INDEX_SHIFT,
		.size = sizeof(encrypted_template),
	};

	/* Get encrypted template (should fail with EC_RES_INVALID_PARAM). */
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_fp_frame(NULL, &template_request,
				      encrypted_template));
}

ZTEST_USER(fpsensor_template, test_fp_frame_get_encrypted_template_success)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_params_fp_frame template_request = {
		.offset = FP_FRAME_INDEX_TEMPLATE << FP_FRAME_INDEX_SHIFT,
		.size = sizeof(encrypted_template),
	};
	struct ec_fp_template_encryption_metadata *enc_info;
	struct ec_params_fp_read_match_secret secret_params;
	struct ec_response_fp_read_match_secret secret_response;

	/* Switch mode to enroll. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode &
		     (FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/*
	 * Use custom enroll step function to tell the fpsensor task that
	 * enroll is finished.
	 */
	enroll_percent = 100;
	mock_alg_enroll_step_fake.custom_fake = custom_enroll_step;

	/* Use custom enroll finish function to return the template */
	mock_alg_enroll_finish_fake.custom_fake = custom_enroll_finish;

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Get encrypted template. */
	zassert_ok(
		ec_cmd_fp_frame(NULL, &template_request, encrypted_template));

	enc_info =
		(struct ec_fp_template_encryption_metadata *)encrypted_template;
	zassert_equal(enc_info->struct_version,
		      expected_enc_info.struct_version);
	zassert_mem_equal(enc_info->nonce, expected_enc_info.nonce,
			  FP_CONTEXT_NONCE_BYTES);
	zassert_mem_equal(enc_info->encryption_salt,
			  expected_enc_info.encryption_salt,
			  FP_CONTEXT_ENCRYPTION_SALT_BYTES);
	zassert_mem_equal(enc_info->tag, expected_enc_info.tag,
			  FP_CONTEXT_TAG_BYTES);

	zassert_mem_equal(
		encrypted_template +
			sizeof(struct ec_fp_template_encryption_metadata),
		example_template_encrypted,
		CONFIG_FP_ALGORITHM_TEMPLATE_SIZE +
			FP_POSITIVE_MATCH_SALT_BYTES);

	/* Read positive match secret for matched template. */
	secret_params.fgr = 0;
	zassert_ok(ec_cmd_fp_read_match_secret(NULL, &secret_params,
					       &secret_response));
	zassert_mem_equal(secret_response.positive_match_secret,
			  example_positive_match_secret,
			  FP_POSITIVE_MATCH_SECRET_BYTES);

	/* b/114160734: Not more than 1 encrypted message per second. */
	k_sleep(K_SECONDS(1));
}

ZTEST_USER(fpsensor_template, test_fp_frame_v1_no_template)
{
	struct ec_params_fp_frame_v1 template_request = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
	};

	/* Encrypt template (should fail with EC_RES_UNAVAILABLE). */
	zassert_equal(EC_RES_UNAVAILABLE,
		      ec_cmd_fp_frame_v1(NULL, &template_request, NULL));
}

ZTEST_USER(fpsensor_template, test_fp_frame_v1_template_id_out_of_range)
{
	struct ec_params_fp_frame_v1 template_request = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = FP_MAX_FINGER_COUNT,
	};

	/* Encrypt template (should fail with EC_RES_INVALID_PARAM). */
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_fp_frame_v1(NULL, &template_request, NULL));
}

ZTEST_USER(fpsensor_template, test_fp_frame_v1_get_encrypted_template_success)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_params_fp_frame_v1 encrypt_request = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
	};
	struct ec_params_fp_frame_v1 get_request = {
		.cmd = FP_FRAME_GET_ENCRYPTED_TEMPLATE,
		.offset = 0,
		.size = sizeof(encrypted_template),
	};
	struct ec_fp_template_encryption_metadata *enc_info;

	/* Switch mode to enroll. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode &
		     (FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/*
	 * Use custom enroll step function to tell the fpsensor task that
	 * enroll is finished.
	 */
	enroll_percent = 100;
	mock_alg_enroll_step_fake.custom_fake = custom_enroll_step;

	/* Use custom enroll finish function to return the template */
	mock_alg_enroll_finish_fake.custom_fake = custom_enroll_finish;

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Request encryption. */
	zassert_ok(ec_cmd_fp_frame_v1(NULL, &encrypt_request, NULL));

	/* b/114160734: Not more than 1 encrypted message per second. */
	k_sleep(K_SECONDS(1));

	/* Get encrypted template. */
	zassert_ok(ec_cmd_fp_frame_v1(NULL, &get_request, encrypted_template));

	enc_info =
		(struct ec_fp_template_encryption_metadata *)encrypted_template;
	zassert_equal(enc_info->struct_version,
		      expected_enc_info.struct_version);
	zassert_mem_equal(enc_info->nonce, expected_enc_info.nonce,
			  FP_CONTEXT_NONCE_BYTES);
	zassert_mem_equal(enc_info->encryption_salt,
			  expected_enc_info.encryption_salt,
			  FP_CONTEXT_ENCRYPTION_SALT_BYTES);
	zassert_mem_equal(enc_info->tag, expected_enc_info.tag,
			  FP_CONTEXT_TAG_BYTES);

	zassert_mem_equal(
		encrypted_template +
			sizeof(struct ec_fp_template_encryption_metadata),
		example_template_encrypted,
		CONFIG_FP_ALGORITHM_TEMPLATE_SIZE +
			FP_POSITIVE_MATCH_SALT_BYTES);
}

ZTEST_USER(fpsensor_template, test_fp_frame_v1_get_encrypted_template_busy)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_params_fp_frame_v1 encrypt_request = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
	};
	struct ec_params_fp_frame_v1 get_request = {
		.cmd = FP_FRAME_GET_ENCRYPTED_TEMPLATE,
		.offset = 0,
		.size = sizeof(encrypted_template),
	};

	/* Switch mode to enroll. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode &
		     (FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/*
	 * Use custom enroll step function to tell the fpsensor task that
	 * enroll is finished.
	 */
	enroll_percent = 100;
	mock_alg_enroll_step_fake.custom_fake = custom_enroll_step;

	/* Use custom enroll finish function to return the template */
	mock_alg_enroll_finish_fake.custom_fake = custom_enroll_finish;

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Request encryption. */
	zassert_ok(ec_cmd_fp_frame_v1(NULL, &encrypt_request, NULL));

	/*
	 * Get encrypted template immediately (deferred task didn't run yet).
	 * Should return EC_RES_BUSY.
	 */
	zassert_equal(EC_RES_BUSY, ec_cmd_fp_frame_v1(NULL, &get_request,
						      encrypted_template));

	/* b/114160734: Not more than 1 encrypted message per second. */
	k_sleep(K_SECONDS(1));
}

ZTEST_USER(fpsensor_template, test_fp_frame_v1_get_encrypted_template_not_ready)
{
	struct ec_params_fp_frame_v1 get_request = {
		.cmd = FP_FRAME_GET_ENCRYPTED_TEMPLATE,
		.offset = 0,
		.size = sizeof(encrypted_template),
	};

	zassert_equal(EC_RES_UNAVAILABLE,
		      ec_cmd_fp_frame_v1(NULL, &get_request,
					 encrypted_template));
}

ZTEST_USER(fpsensor_template,
	   test_fp_frame_v1_get_encrypted_template_bad_offset)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_params_fp_frame_v1 encrypt_request = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
	};
	struct ec_params_fp_frame_v1 get_request = {
		.cmd = FP_FRAME_GET_ENCRYPTED_TEMPLATE,
		.offset = 0,
		.size = sizeof(encrypted_template) + 1,
	};

	/* Switch mode to enroll. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode &
		     (FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/*
	 * Use custom enroll step function to tell the fpsensor task that
	 * enroll is finished.
	 */
	enroll_percent = 100;
	mock_alg_enroll_step_fake.custom_fake = custom_enroll_step;

	/* Use custom enroll finish function to return the template */
	mock_alg_enroll_finish_fake.custom_fake = custom_enroll_finish;

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Request encryption. */
	zassert_ok(ec_cmd_fp_frame_v1(NULL, &encrypt_request, NULL));

	/* Give opportunity for deferred task to run. */
	k_msleep(1);

	/* Get encrypted template with bad size. */
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_fp_frame_v1(NULL, &get_request,
					 encrypted_template));

	/* b/114160734: Not more than 1 encrypted message per second. */
	k_sleep(K_SECONDS(1));
}

ZTEST_USER(fpsensor_template, test_fp_frame_v1_encrypt_template_busy)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_params_fp_frame_v1 encrypt_request = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
	};

	/* Switch mode to enroll. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode &
		     (FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/*
	 * Use custom enroll step function to tell the fpsensor task that
	 * enroll is finished.
	 */
	enroll_percent = 100;
	mock_alg_enroll_step_fake.custom_fake = custom_enroll_step;

	/* Use custom enroll finish function to return the template */
	mock_alg_enroll_finish_fake.custom_fake = custom_enroll_finish;

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Request encryption. */
	zassert_ok(ec_cmd_fp_frame_v1(NULL, &encrypt_request, NULL));

	/* Request encryption again. Should return EC_RES_BUSY. */
	zassert_equal(EC_RES_BUSY,
		      ec_cmd_fp_frame_v1(NULL, &encrypt_request, NULL));

	/* b/114160734: Not more than 1 encrypted message per second. */
	k_sleep(K_SECONDS(1));
}

ZTEST_USER(fpsensor_template, test_fp_frame_v1_encrypt_template_deadline)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_params_fp_frame_v1 encrypt_request = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
	};

	/* Switch mode to enroll. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode &
		     (FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/*
	 * Use custom enroll step function to tell the fpsensor task that
	 * enroll is finished.
	 */
	enroll_percent = 100;
	mock_alg_enroll_step_fake.custom_fake = custom_enroll_step;

	/* Use custom enroll finish function to return the template */
	mock_alg_enroll_finish_fake.custom_fake = custom_enroll_finish;

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Request encryption. */
	zassert_ok(ec_cmd_fp_frame_v1(NULL, &encrypt_request, NULL));

	/* Give opportunity for deferred task to run (clear IN_PROGRESS). */
	k_msleep(100);

	/* Request encryption again (hit deadline). */
	zassert_equal(EC_RES_BUSY,
		      ec_cmd_fp_frame_v1(NULL, &encrypt_request, NULL));

	/* b/114160734: Not more than 1 encrypted message per second. */
	k_sleep(K_SECONDS(1));
}

ZTEST_USER(fpsensor_template,
	   test_fp_frame_v1_encryption_status_ready_set_and_cleared)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_ENROLL_SESSION | FP_MODE_ENROLL_IMAGE,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_params_fp_frame_v1 encrypt_request = {
		.cmd = FP_FRAME_ENCRYPT_TEMPLATE,
		.index = 0,
	};
	struct ec_response_fp_encryption_status status_response;

	/* Switch mode to enroll. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/*
	 * Use custom enroll step function to tell the fpsensor task that
	 * enroll is finished.
	 */
	enroll_percent = 100;
	mock_alg_enroll_step_fake.custom_fake = custom_enroll_step;

	/* Use custom enroll finish function to return the template */
	mock_alg_enroll_finish_fake.custom_fake = custom_enroll_finish;

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Verify initially FP_ENCRYPTED_TEMPLATE_READY is NOT set. */
	zassert_ok(ec_cmd_fp_encryption_status(NULL, &status_response));
	zassert_false(status_response.status & FP_ENCRYPTED_TEMPLATE_READY);

	/* Request encryption. */
	zassert_ok(ec_cmd_fp_frame_v1(NULL, &encrypt_request, NULL));

	/* b/114160734: Not more than 1 encrypted message per second. */
	k_sleep(K_SECONDS(1));

	/* Verify FP_ENCRYPTED_TEMPLATE_READY IS set. */
	zassert_ok(ec_cmd_fp_encryption_status(NULL, &status_response));
	zassert_true(status_response.status & FP_ENCRYPTED_TEMPLATE_READY);

	/* Request encryption AGAIN. */
	zassert_ok(ec_cmd_fp_frame_v1(NULL, &encrypt_request, NULL));

	/* Verify FP_ENCRYPTED_TEMPLATE_READY IS CLEARED immediately. */
	zassert_ok(ec_cmd_fp_encryption_status(NULL, &status_response));
	zassert_false(status_response.status & FP_ENCRYPTED_TEMPLATE_READY);

	/* b/114160734: Not more than 1 encrypted message per second. */
	k_sleep(K_SECONDS(1));

	/* Verify FP_ENCRYPTED_TEMPLATE_READY IS set again. */
	zassert_ok(ec_cmd_fp_encryption_status(NULL, &status_response));
	zassert_true(status_response.status & FP_ENCRYPTED_TEMPLATE_READY);
}

ZTEST_USER(fpsensor_template, test_fp_template_load_template_success)
{
	uint8_t params_buffer[FP_TEMPLATE_PARAMS_BUFFER_SIZE];
	struct ec_params_fp_template *params =
		(struct ec_params_fp_template *)params_buffer;

	const size_t data_size =
		FP_TEMPLATE_PARAMS_BUFFER_SIZE - sizeof(*params);
	uint8_t *data = params_buffer + sizeof(*params);
	size_t offset = 0;

	memcpy(encrypted_template, &expected_enc_info,
	       sizeof(struct ec_fp_template_encryption_metadata));
	memcpy(encrypted_template +
		       sizeof(struct ec_fp_template_encryption_metadata),
	       example_template_encrypted,
	       CONFIG_FP_ALGORITHM_TEMPLATE_SIZE +
		       FP_POSITIVE_MATCH_SALT_BYTES);

	while (offset < FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE) {
		params->offset = offset;
		params->size =
			MIN(data_size,
			    FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE - offset);
		memcpy(data, encrypted_template + offset, params->size);
		offset += params->size;

		if (offset == FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE) {
			params->size |= FP_TEMPLATE_COMMIT;
		}

		zassert_ok(ec_cmd_fp_template(NULL, params,
					      FP_TEMPLATE_PARAMS_BUFFER_SIZE));
	}

	/* Confirm that there is 1 valid template. */
	zassert_ok(ec_cmd_fp_info_v3(NULL, test_info_buffer,
				     test_info_buffer_size));
	zassert_equal(test_info_buffer->template_info.template_valid, 1);
}

ZTEST_USER(fpsensor_template, test_fp_template_load_template_invalid_tag)
{
	uint8_t params_buffer[FP_TEMPLATE_PARAMS_BUFFER_SIZE];
	struct ec_params_fp_template *params =
		(struct ec_params_fp_template *)params_buffer;

	const size_t data_size =
		FP_TEMPLATE_PARAMS_BUFFER_SIZE - sizeof(*params);
	uint8_t *data = params_buffer + sizeof(*params);
	size_t offset = 0;

	struct ec_fp_template_encryption_metadata enc_info_with_invalid_tag =
		expected_enc_info;

	/* Corrupt the tag. We expect that the template will be rejected. */
	enc_info_with_invalid_tag.tag[0] = 0x00;

	memcpy(encrypted_template, &enc_info_with_invalid_tag,
	       sizeof(struct ec_fp_template_encryption_metadata));
	memcpy(encrypted_template +
		       sizeof(struct ec_fp_template_encryption_metadata),
	       example_template_encrypted,
	       CONFIG_FP_ALGORITHM_TEMPLATE_SIZE +
		       FP_POSITIVE_MATCH_SALT_BYTES);

	while (offset < FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE) {
		params->offset = offset;
		params->size =
			MIN(data_size,
			    FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE - offset);
		memcpy(data, encrypted_template + offset, params->size);
		offset += params->size;

		if (offset != FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE) {
			/* Encrypted template is copied correctly. */
			zassert_ok(ec_cmd_fp_template(
				NULL, params, FP_TEMPLATE_PARAMS_BUFFER_SIZE));
		} else {
			params->size |= FP_TEMPLATE_COMMIT;
			/* Expect decryption failure (EC_RES_UNAVAILABLE). */
			zassert_equal(EC_RES_UNAVAILABLE,
				      ec_cmd_fp_template(
					      NULL, params,
					      FP_TEMPLATE_PARAMS_BUFFER_SIZE));
		}
	}

	/* Confirm that there is no valid template. */
	zassert_ok(ec_cmd_fp_info_v3(NULL, test_info_buffer,
				     test_info_buffer_size));
	zassert_equal(test_info_buffer->template_info.template_valid, 0);
}

static const struct enc_buffer zero_enc_buffer = {};

ZTEST_USER(fpsensor_template, test_fp_template_v1_cleans_buffer)
{
	uint8_t params_buffer[FP_TEMPLATE_V1_PARAMS_BUFFER_SIZE];
	struct ec_params_fp_template_v1 *params =
		(struct ec_params_fp_template_v1 *)params_buffer;

	const size_t data_size =
		FP_TEMPLATE_V1_PARAMS_BUFFER_SIZE -
		offsetof(struct ec_params_fp_template_v1, data);
	uint8_t *data =
		params_buffer + offsetof(struct ec_params_fp_template_v1, data);
	size_t offset = 0;

	memcpy(encrypted_template, &expected_enc_info,
	       sizeof(struct ec_fp_template_encryption_metadata));
	memcpy(encrypted_template +
		       sizeof(struct ec_fp_template_encryption_metadata),
	       example_template_encrypted,
	       CONFIG_FP_ALGORITHM_TEMPLATE_SIZE +
		       FP_POSITIVE_MATCH_SALT_BYTES);

	params->cmd = FP_TEMPLATE_LOAD;
	while (offset < FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE) {
		params->offset = offset;
		params->size =
			MIN(data_size,
			    FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE - offset);
		memcpy(data, encrypted_template + offset, params->size);

		zassert_ok(ec_cmd_fp_template_v1(
			NULL, params,
			params->size + offsetof(struct ec_params_fp_template_v1,
						data)));

		offset += params->size;
	}

	/* Confirm that the buffer is NOT clean before decryption. */
	zassert_true(memcmp(&fp_enc_buffer, &zero_enc_buffer,
			    sizeof(fp_enc_buffer)) != 0,
		     "fp_enc_buffer should not be clean before decryption.");

	/* Start decryption. */
	params->cmd = FP_TEMPLATE_DECRYPT;
	params->offset = 0;
	params->size = 0;
	zassert_ok(ec_cmd_fp_template_v1(
		NULL, params, offsetof(struct ec_params_fp_template_v1, data)));

	/* Wait for decryption to finish. */
	params->cmd = FP_TEMPLATE_GET_RESULT;
	while (ec_cmd_fp_template_v1(NULL, params,
				     offsetof(struct ec_params_fp_template_v1,
					      data)) == EC_RES_BUSY) {
		k_msleep(1);
	}

	/* Confirm that the buffer IS clean after decryption. */
	zassert_mem_equal(&fp_enc_buffer, &zero_enc_buffer,
			  sizeof(fp_enc_buffer),
			  "fp_enc_buffer should be clean after decryption.");
}

ZTEST_USER(fpsensor_template, test_fp_template_v1_cleans_buffer_on_failure)
{
	uint8_t params_buffer[FP_TEMPLATE_V1_PARAMS_BUFFER_SIZE];
	struct ec_params_fp_template_v1 *params =
		(struct ec_params_fp_template_v1 *)params_buffer;

	const size_t data_size =
		FP_TEMPLATE_V1_PARAMS_BUFFER_SIZE -
		offsetof(struct ec_params_fp_template_v1, data);
	uint8_t *data =
		params_buffer + offsetof(struct ec_params_fp_template_v1, data);
	size_t offset = 0;

	struct ec_fp_template_encryption_metadata enc_info_with_invalid_tag =
		expected_enc_info;

	/* Corrupt the tag. We expect that the template will be rejected. */
	enc_info_with_invalid_tag.tag[0] = expected_enc_info.tag[0] ^ 0xFF;

	memcpy(encrypted_template, &enc_info_with_invalid_tag,
	       sizeof(struct ec_fp_template_encryption_metadata));
	memcpy(encrypted_template +
		       sizeof(struct ec_fp_template_encryption_metadata),
	       example_template_encrypted,
	       CONFIG_FP_ALGORITHM_TEMPLATE_SIZE +
		       FP_POSITIVE_MATCH_SALT_BYTES);

	params->cmd = FP_TEMPLATE_LOAD;
	while (offset < FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE) {
		params->offset = offset;
		params->size =
			MIN(data_size,
			    FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE - offset);
		memcpy(data, encrypted_template + offset, params->size);

		zassert_ok(ec_cmd_fp_template_v1(
			NULL, params,
			params->size + offsetof(struct ec_params_fp_template_v1,
						data)));

		offset += params->size;
	}

	/* Confirm that the buffer is NOT clean before decryption. */
	zassert_true(memcmp(&fp_enc_buffer, &zero_enc_buffer,
			    sizeof(fp_enc_buffer)) != 0,
		     "fp_enc_buffer should not be clean before decryption.");

	/* Start decryption. */
	params->cmd = FP_TEMPLATE_DECRYPT;
	params->offset = 0;
	params->size = 0;
	zassert_ok(ec_cmd_fp_template_v1(
		NULL, params, offsetof(struct ec_params_fp_template_v1, data)));

	/* Wait for decryption to finish. */
	params->cmd = FP_TEMPLATE_GET_RESULT;
	while (ec_cmd_fp_template_v1(NULL, params,
				     offsetof(struct ec_params_fp_template_v1,
					      data)) == EC_RES_BUSY) {
		k_msleep(1);
	}

	/* Confirm that the buffer IS clean even after failed decryption. */
	zassert_mem_equal(
		&fp_enc_buffer, &zero_enc_buffer, sizeof(fp_enc_buffer),
		"fp_enc_buffer should be clean even after failed decryption.");
}

ZTEST_USER(fpsensor_template, test_fp_template_cleans_buffer)
{
	uint8_t params_buffer[FP_TEMPLATE_PARAMS_BUFFER_SIZE];
	struct ec_params_fp_template *params =
		(struct ec_params_fp_template *)params_buffer;

	const size_t data_size =
		FP_TEMPLATE_PARAMS_BUFFER_SIZE - sizeof(*params);
	uint8_t *data = params_buffer + sizeof(*params);
	size_t offset = 0;

	memcpy(encrypted_template, &expected_enc_info,
	       sizeof(struct ec_fp_template_encryption_metadata));
	memcpy(encrypted_template +
		       sizeof(struct ec_fp_template_encryption_metadata),
	       example_template_encrypted,
	       CONFIG_FP_ALGORITHM_TEMPLATE_SIZE +
		       FP_POSITIVE_MATCH_SALT_BYTES);

	while (offset < FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE) {
		params->offset = offset;
		params->size =
			MIN(data_size,
			    FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE - offset);
		memcpy(data, encrypted_template + offset, params->size);
		offset += params->size;

		if (offset == FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE) {
			params->size |= FP_TEMPLATE_COMMIT;
		}

		zassert_ok(ec_cmd_fp_template(NULL, params,
					      FP_TEMPLATE_PARAMS_BUFFER_SIZE));

		if (!(params->size & FP_TEMPLATE_COMMIT)) {
			/* Confirm that the buffer is NOT clean before commit.
			 */
			zassert_true(
				memcmp(&fp_enc_buffer, &zero_enc_buffer,
				       sizeof(fp_enc_buffer)) != 0,
				"fp_enc_buffer should not be clean before commit.");
		}
	}

	/* Confirm that the buffer IS clean after commit. */
	zassert_mem_equal(&fp_enc_buffer, &zero_enc_buffer,
			  sizeof(fp_enc_buffer),
			  "fp_enc_buffer should be clean after commit.");
}

ZTEST_USER(fpsensor_template, test_fp_template_cleans_buffer_on_failure)
{
	uint8_t params_buffer[FP_TEMPLATE_PARAMS_BUFFER_SIZE];
	struct ec_params_fp_template *params =
		(struct ec_params_fp_template *)params_buffer;

	const size_t data_size =
		FP_TEMPLATE_PARAMS_BUFFER_SIZE - sizeof(*params);
	uint8_t *data = params_buffer + sizeof(*params);
	size_t offset = 0;

	struct ec_fp_template_encryption_metadata enc_info_with_invalid_tag =
		expected_enc_info;

	/* Corrupt the tag. We expect that the template will be rejected. */
	enc_info_with_invalid_tag.tag[0] = expected_enc_info.tag[0] ^ 0xFF;

	memcpy(encrypted_template, &enc_info_with_invalid_tag,
	       sizeof(struct ec_fp_template_encryption_metadata));
	memcpy(encrypted_template +
		       sizeof(struct ec_fp_template_encryption_metadata),
	       example_template_encrypted,
	       CONFIG_FP_ALGORITHM_TEMPLATE_SIZE +
		       FP_POSITIVE_MATCH_SALT_BYTES);

	while (offset < FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE) {
		params->offset = offset;
		params->size =
			MIN(data_size,
			    FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE - offset);
		memcpy(data, encrypted_template + offset, params->size);
		offset += params->size;

		if (offset != FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE) {
			/* Encrypted template is copied correctly. */
			zassert_ok(ec_cmd_fp_template(
				NULL, params, FP_TEMPLATE_PARAMS_BUFFER_SIZE));

			/* Confirm that the buffer is NOT clean before commit.
			 */
			zassert_true(
				memcmp(&fp_enc_buffer, &zero_enc_buffer,
				       sizeof(fp_enc_buffer)) != 0,
				"fp_enc_buffer should not be clean before commit.");
		} else {
			params->size |= FP_TEMPLATE_COMMIT;
			/* Expect decryption failure (EC_RES_UNAVAILABLE). */
			zassert_equal(EC_RES_UNAVAILABLE,
				      ec_cmd_fp_template(
					      NULL, params,
					      FP_TEMPLATE_PARAMS_BUFFER_SIZE));
		}
	}

	/* Confirm that the buffer IS clean even after failed commit. */
	zassert_mem_equal(
		&fp_enc_buffer, &zero_enc_buffer, sizeof(fp_enc_buffer),
		"fp_enc_buffer should be clean even after failed commit.");
}

static void *fpsensor_setup(void)
{
	struct ec_params_fp_seed fp_seed_params = {
		.struct_version = 4,
		.reserved = 0,
		.seed = FAKE_TPM_SEED,
	};
	const struct rollback_data data = {
		.id = 0,
		.rollback_min_version = 0,
#ifdef CONFIG_PLATFORM_EC_ROLLBACK_SECRET_SIZE
		.secret = { 0 },
#endif
		.cookie = CROS_EC_ROLLBACK_COOKIE,
	};

	zassert_ok(crec_flash_erase(ROLLBACK0_ADDR, ROLLBACK0_SIZE));
	zassert_ok(crec_flash_write(ROLLBACK0_ADDR, sizeof(data),
				    (const char *)&data));

	zassert_ok(crec_flash_erase(ROLLBACK1_ADDR, ROLLBACK1_SIZE));
	zassert_ok(crec_flash_write(ROLLBACK1_ADDR, sizeof(data),
				    (const char *)&data));

	/* Start shimmed tasks. */
	start_ec_tasks();
	k_msleep(100);

	/* Add some entropy to rollback region. */
	rollback_add_entropy(fake_rollback_entropy,
			     sizeof(fake_rollback_entropy) - 1);

	/* Set TPM seed. */
	ec_cmd_fp_seed(NULL, &fp_seed_params);

	return NULL;
}

static void fpsensor_before(void *f)
{
	struct fingerprint_sensor_state state = {
		.bad_pixels = 0,
		.maintenance_ran = false,
		.detect_mode = false,
		.low_power_mode = false,
		.finger_state = FINGERPRINT_FINGER_STATE_NONE,
		.init_result = 0,
		.deinit_result = 0,
		.config_result = 0,
		.get_info_result = 0,
		.acquire_image_result = FINGERPRINT_SENSOR_SCAN_GOOD,
		.last_acquire_image_mode = -1,
	};
	struct ec_params_fp_context_v1 fp_context_params = {
		.action = FP_CONTEXT_ASYNC,
		.userid = FAKE_USER_ID,
	};

	fingerprint_set_state(fp_sim, &state);
	RESET_FAKE(mkbp_send_event);
	RESET_FAKE(system_is_locked);

	RESET_FAKE(mock_alg_init);
	RESET_FAKE(mock_alg_exit);
	RESET_FAKE(mock_alg_enroll_start);
	RESET_FAKE(mock_alg_enroll_step);
	RESET_FAKE(mock_alg_enroll_finish);
	RESET_FAKE(mock_alg_match);

	/* Set context (FP_CONTEXT_ASYNC). */
	ec_cmd_fp_context_v1(NULL, &fp_context_params);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Get context setting result. */
	fp_context_params.action = FP_CONTEXT_GET_RESULT;
	ec_cmd_fp_context_v1(NULL, &fp_context_params);

	/* Reset seed for random() */
	srandom(0xdeadc0de);
}

ZTEST_SUITE(fpsensor_template, NULL, fpsensor_setup, fpsensor_before, NULL,
	    NULL);
