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
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <drivers/fingerprint.h>
#include <drivers/fingerprint_sim.h>
#include <ec_commands.h>
#include <ec_tasks.h>
#include <fpsensor/fpsensor_state.h>
#include <host_command.h>
#include <mkbp_event.h>
#include <rollback.h>
#include <system.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);
FAKE_VALUE_FUNC(int, system_is_locked);

#define fp_sim DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))
#define IMAGE_SIZE                          \
	FINGERPRINT_SENSOR_REAL_IMAGE_SIZE( \
		DT_CHOSEN(cros_fp_fingerprint_sensor))
static uint8_t frame_buffer[IMAGE_SIZE];

static const uint8_t fake_rollback_entropy[] = "some_rollback_entropy";

/* The fake TPM seed is "very_secret_32_bytes_of_tpm_seed" */
#define FAKE_TPM_SEED                                                         \
	{                                                                     \
		0x76, 0x65, 0x72, 0x79, 0x5f, 0x73, 0x65, 0x63, 0x72, 0x65,   \
			0x74, 0x5f, 0x33, 0x32, 0x5f, 0x62, 0x79, 0x74, 0x65, \
			0x73, 0x5f, 0x6f, 0x66, 0x5f, 0x74, 0x70, 0x6d, 0x5f, \
			0x73, 0x65, 0x65, 0x64                                \
	}

/* The fake UserID is "i_m_a_fake_user_id_used_for_test" */
#define FAKE_USER_ID                                                        \
	{                                                                   \
		0x5f6d5f69, 0x61665f61, 0x755f656b, 0x5f726573, 0x755f6469, \
			0x5f646573, 0x5f726f66, 0x74736574                  \
	}

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

/*
 * Size of params buffer for FP_TEMPLATE command. Its size must be big enough
 * to keep ec_params_fp_template structure and a part of template.
 */
#define FP_TEMPLATE_PARAMS_BUFFER_SIZE 16
BUILD_ASSERT(FP_TEMPLATE_PARAMS_BUFFER_SIZE >
	     sizeof(struct ec_params_fp_template));

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
}

ZTEST_USER(fpsensor_template, test_fp_template_load_template_success)
{
	uint8_t params_buffer[FP_TEMPLATE_PARAMS_BUFFER_SIZE];
	struct ec_params_fp_template *params =
		(struct ec_params_fp_template *)params_buffer;

	const size_t data_size =
		FP_TEMPLATE_PARAMS_BUFFER_SIZE - sizeof(*params);
	uint8_t *data = params_buffer + sizeof(*params);
	struct ec_response_fp_info info;
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
	zassert_ok(ec_cmd_fp_info(NULL, &info));
	zassert_equal(info.template_valid, 1);
}

ZTEST_USER(fpsensor_template, test_fp_template_load_template_invalid_tag)
{
	uint8_t params_buffer[FP_TEMPLATE_PARAMS_BUFFER_SIZE];
	struct ec_params_fp_template *params =
		(struct ec_params_fp_template *)params_buffer;

	const size_t data_size =
		FP_TEMPLATE_PARAMS_BUFFER_SIZE - sizeof(*params);
	uint8_t *data = params_buffer + sizeof(*params);
	struct ec_response_fp_info info;
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
	zassert_ok(ec_cmd_fp_info(NULL, &info));
	zassert_equal(info.template_valid, 0);
}

static void *fpsensor_setup(void)
{
	struct ec_params_fp_seed fp_seed_params = {
		.struct_version = 4,
		.reserved = 0,
		.seed = FAKE_TPM_SEED,
	};

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
