/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock_fingerprint_algorithm.h"

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
#include <fingerprint/fingerprint_alg.h>
#include <fpsensor/fpsensor_state.h>
#include <host_command.h>
#include <rollback.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

#define fp_sim DEVICE_DT_GET(DT_CHOSEN(cros_fp_fingerprint_sensor))
#define IMAGE_SIZE                          \
	FINGERPRINT_SENSOR_REAL_IMAGE_SIZE( \
		DT_CHOSEN(cros_fp_fingerprint_sensor))
static uint8_t image_buffer[IMAGE_SIZE];

static const char fake_rollback_entropy[] = "some_rollback_entropy";

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

static const uint8_t example_template[CONFIG_FP_ALGORITHM_TEMPLATE_SIZE] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
	0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
	0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
};

static const uint8_t
	example_positive_match_secret[FP_POSITIVE_MATCH_SECRET_BYTES] = {
		0xc8, 0x3a, 0x56, 0x56, 0xe7, 0x96, 0x06, 0xc3,
		0xb3, 0xed, 0x47, 0x20, 0x7e, 0x60, 0xbd, 0x5e,
		0xef, 0x6c, 0xa8, 0x84, 0xf2, 0x71, 0x86, 0x1a,
		0xf2, 0xa3, 0x6b, 0xa8, 0x1a, 0x82, 0x59, 0x45
	};

/*
 * Encrypted template with metadata and positive match salt, for more
 * information please check comment in fpsensor_template.c
 */
static const uint8_t
	example_template_encrypted[sizeof(struct ec_params_fp_template) +
				   FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE] = {
		/*
		 * FP_TEMPLATE params.
		 *
		 * offset - 4 bytes
		 * size - 4 bytes
		 */
		0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x80,
		/*
		 * Encryption metadata.
		 *
		 * struct_version - 2 bytes
		 * reserved - 2 bytes
		 * nonce - 12 bytes
		 * encryption_salt - 16 bytes
		 * tag - 16 bytes
		 */
		0x04, 0x00, 0x00, 0x00, 0x94, 0x1e, 0xe3, 0x47, 0x31, 0x0b,
		0x89, 0x73, 0x1a, 0xeb, 0xa4, 0x45, 0x2b, 0x2e, 0x90, 0x58,
		0xfa, 0x25, 0x3e, 0x3b, 0x21, 0x35, 0x9a, 0x25, 0x79, 0x20,
		0xba, 0x60, 0x6b, 0x73, 0xb8, 0xac, 0x86, 0x6f, 0xe1, 0xbc,
		0x86, 0xca, 0xf6, 0x42, 0x25, 0x1f, 0xd1, 0x22,
		/* Encrypted template. */
		0xed, 0x2f, 0xb5, 0xf3, 0x9a, 0x7a, 0xfe, 0x09, 0x82, 0x69,
		0x9a, 0xd0, 0xa0, 0x60, 0x35, 0x15, 0x87, 0xdf, 0xea, 0xf7,
		0x8f, 0x4f, 0xdf, 0x5d, 0x7a, 0x93, 0xcf, 0x61, 0xad, 0xe6,
		0xc2, 0x3a,
		/* Encrypted positive match salt. */
		0x0f, 0x8d, 0xe0, 0x47, 0x69, 0x0f, 0xda, 0xea, 0xbc, 0xdc,
		0x96, 0x7d, 0x69, 0x19, 0xac, 0xe7
	};

static int match_result;
static int32_t finger_index;
static uint32_t finger_updated_bitmap;

ZTEST_USER(fpsensor_match, test_match_no_templates_mkbp_event)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_MATCH,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	uint32_t fp_events;

	/* Switch mode to match. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_MATCH);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task to process event. */
	k_msleep(1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);

	/*
	 * Confirm that:
	 * - MKBP event is FP_MATCH
	 * - Match failed with NO_TEMPLATES
	 * - Finger ID is FP_NO_SUCH_TEMPLATE
	 */
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_MATCH);
	zassert_equal(EC_MKBP_FP_ERRCODE(fp_events),
		      EC_MKBP_FP_ERR_MATCH_NO_TEMPLATES);
	zassert_equal(EC_MKBP_FP_MATCH_IDX(fp_events),
		      FP_NO_SUCH_TEMPLATE & 0xF);
}

ZTEST_USER(fpsensor_match, test_match_no_templates_mode_cleared)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_MATCH,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;

	/* Switch mode to match. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_MATCH);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task to process event. */
	k_msleep(1);

	/* Confirm that capture mode is not enabled. */
	params.mode = FP_MODE_DONT_CHANGE;
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_false(response.mode & FP_MODE_MATCH);
}

static int match_compare(const struct fingerprint_algorithm *const alg,
			 void *templ, uint32_t templ_count,
			 const uint8_t *const image, int32_t *match_index,
			 uint32_t *update_bitmap)
{
	zassert_equal(templ_count, 1);
	zassert_mem_equal((uint8_t *)templ, example_template,
			  CONFIG_FP_ALGORITHM_TEMPLATE_SIZE);
	zassert_mem_equal(image, image_buffer, IMAGE_SIZE);

	return 0;
}

ZTEST_USER(fpsensor_match, test_match_correct_template_and_image)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_MATCH,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	/*
	 * Use custom match function to check if template and scan passed to
	 * matching algorithm is correct.
	 */
	mock_alg_match_fake.custom_fake = match_compare;

	/* Switch mode to match. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_MATCH);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Prepare image. */
	memset(image_buffer, 1, IMAGE_SIZE);

	/* Load image to simulator. */
	fingerprint_load_image(fp_sim, image_buffer, IMAGE_SIZE);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);
}

ZTEST_USER(fpsensor_match, test_match_no_match_mkbp_event)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_MATCH,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	uint32_t fp_events;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	mock_alg_match_fake.return_val = FP_MATCH_RESULT_NO_MATCH;

	/* Switch mode to match. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_MATCH);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);

	/*
	 * Confirm that:
	 * - MKBP event is FP_MATCH
	 * - Match failed with NO_MATCH
	 * - Finger ID is FP_NO_SUCH_TEMPLATE
	 */
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_MATCH);
	zassert_equal(EC_MKBP_FP_ERRCODE(fp_events), EC_MKBP_FP_ERR_MATCH_NO);
	zassert_equal(EC_MKBP_FP_MATCH_IDX(fp_events),
		      FP_NO_SUCH_TEMPLATE & 0xF);
}

static int custom_match(const struct fingerprint_algorithm *const alg,
			void *templ, uint32_t templ_count,
			const uint8_t *const image, int32_t *match_index,
			uint32_t *update_bitmap)
{
	*match_index = finger_index;
	*update_bitmap = finger_updated_bitmap;

	return match_result;
}

ZTEST_USER(fpsensor_match, test_match_success_mkbp_event)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_MATCH,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	uint32_t fp_events;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	match_result = FP_MATCH_RESULT_MATCH;
	finger_index = 0;
	finger_updated_bitmap = 0;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Switch mode to match. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_MATCH);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);

	/*
	 * Confirm that:
	 * - MKBP event is FP_MATCH
	 * - Match succeeded with MATCH_YES
	 * - Finger ID is 0
	 */
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_MATCH);
	zassert_equal(EC_MKBP_FP_ERRCODE(fp_events), EC_MKBP_FP_ERR_MATCH_YES);
	zassert_equal(EC_MKBP_FP_MATCH_IDX(fp_events), 0);
}

ZTEST_USER(fpsensor_match, test_match_success_template_updated_mkbp_event)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_MATCH,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	uint32_t fp_events;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	match_result = FP_MATCH_RESULT_MATCH_UPDATED;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Switch mode to match. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_MATCH);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);

	/*
	 * Confirm that:
	 * - MKBP event is FP_MATCH
	 * - Match succeeded with MATCH_YES_UPDATED
	 * - Finger ID is 0
	 */
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_MATCH);
	zassert_equal(EC_MKBP_FP_ERRCODE(fp_events),
		      EC_MKBP_FP_ERR_MATCH_YES_UPDATED);
	zassert_equal(EC_MKBP_FP_MATCH_IDX(fp_events), 0);
}

ZTEST_USER(fpsensor_match, test_match_success_template_update_failed_mkbp_event)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_MATCH,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	uint32_t fp_events;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	match_result = FP_MATCH_RESULT_MATCH_UPDATE_FAILED;
	finger_index = 0;
	finger_updated_bitmap = 0;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Switch mode to match. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_MATCH);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);

	/*
	 * Confirm that:
	 * - MKBP event is FP_MATCH
	 * - Match succeeded with MATCH_YES_UPDATE_FAILED
	 * - Finger ID is 0
	 */
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_MATCH);
	zassert_equal(EC_MKBP_FP_ERRCODE(fp_events),
		      EC_MKBP_FP_ERR_MATCH_YES_UPDATE_FAILED);
	zassert_equal(EC_MKBP_FP_MATCH_IDX(fp_events), 0);
}

ZTEST_USER(fpsensor_match, test_match_success_template_updated_dirty_template)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_MATCH,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_response_fp_info info;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	match_result = FP_MATCH_RESULT_MATCH_UPDATED;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Switch mode to match. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_MATCH);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm that dirty templates bitmap is correct. */
	zassert_ok(ec_cmd_fp_info(NULL, &info));
	zassert_equal(info.template_dirty, 0x1);
}

ZTEST_USER(fpsensor_match,
	   test_match_success_template_update_failed_dirty_template)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_MATCH,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_response_fp_info info;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	match_result = FP_MATCH_RESULT_MATCH_UPDATE_FAILED;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Switch mode to match. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_MATCH);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm that dirty templates bitmap is correct. */
	zassert_ok(ec_cmd_fp_info(NULL, &info));
	zassert_equal(info.template_dirty, 0x0);
}

ZTEST_USER(fpsensor_match, test_match_success_no_template_update_dirty_template)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_MATCH,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_response_fp_info info;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	match_result = FP_MATCH_RESULT_MATCH;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Switch mode to match. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_MATCH);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm that dirty templates bitmap is correct. */
	zassert_ok(ec_cmd_fp_info(NULL, &info));
	zassert_equal(info.template_dirty, 0x0);
}

ZTEST_USER(fpsensor_match,
	   test_match_success_read_positive_match_secret_allowed)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_MATCH,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_params_fp_read_match_secret secret_params;
	struct ec_response_fp_read_match_secret secret_response;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	match_result = FP_MATCH_RESULT_MATCH;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Switch mode to match. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_MATCH);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Read positive match secret for matched template. */
	secret_params.fgr = 0;
	zassert_ok(ec_cmd_fp_read_match_secret(NULL, &secret_params,
					       &secret_response));
	zassert_mem_equal(secret_response.positive_match_secret,
			  example_positive_match_secret,
			  FP_POSITIVE_MATCH_SECRET_BYTES);
}

ZTEST_USER(fpsensor_match,
	   test_match_success_read_positive_match_secret_timeout)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_MATCH,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_params_fp_read_match_secret secret_params;
	struct ec_response_fp_read_match_secret secret_response;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	match_result = FP_MATCH_RESULT_MATCH;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Switch mode to match. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_MATCH);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Wait at least 5 seconds for positive match secret to timeout. */
	k_msleep(5000);

	/* Confirm that we can't read positive match secret. */
	secret_params.fgr = 0;
	zassert_equal(EC_RES_TIMEOUT,
		      ec_cmd_fp_read_match_secret(NULL, &secret_params,
						  &secret_response));
}

ZTEST_USER(fpsensor_match, test_match_success_read_positive_match_secret_twice)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_MATCH,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_params_fp_read_match_secret secret_params;
	struct ec_response_fp_read_match_secret secret_response;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	match_result = FP_MATCH_RESULT_MATCH;
	finger_index = 0;
	finger_updated_bitmap = 0x1;
	mock_alg_match_fake.custom_fake = custom_match;

	/* Switch mode to match. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_MATCH);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	secret_params.fgr = 0;

	/* Expect that we can read positive match secret for the first time. */
	zassert_ok(ec_cmd_fp_read_match_secret(NULL, &secret_params,
					       &secret_response));

	/*
	 * Confirm that we can't read positive match secret for the second time.
	 */
	zassert_equal(EC_RES_TIMEOUT,
		      ec_cmd_fp_read_match_secret(NULL, &secret_params,
						  &secret_response));
}

ZTEST_USER(fpsensor_match,
	   test_match_read_positive_match_secret_without_match_fails)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_MATCH,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_params_fp_read_match_secret secret_params;
	struct ec_response_fp_read_match_secret secret_response;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	mock_alg_match_fake.return_val = FP_MATCH_RESULT_NO_MATCH;

	/* Switch mode to match. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_MATCH);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm that we can't read positive match secret. */
	secret_params.fgr = 0;
	zassert_equal(EC_RES_TIMEOUT,
		      ec_cmd_fp_read_match_secret(NULL, &secret_params,
						  &secret_response));
}

ZTEST_USER(fpsensor_match, test_match_error_no_positive_match_secret)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_MATCH,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	struct ec_params_fp_read_match_secret secret_params;
	struct ec_response_fp_read_match_secret secret_response;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	/* Negative value means error. */
	mock_alg_match_fake.return_val = -1;

	/* Switch mode to match. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_MATCH);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm that we can't read positive match secret. */
	secret_params.fgr = 0;
	zassert_equal(EC_RES_TIMEOUT,
		      ec_cmd_fp_read_match_secret(NULL, &secret_params,
						  &secret_response));
}

ZTEST_USER(fpsensor_match, test_match_error_mkbp_event)
{
	struct ec_params_fp_mode params = {
		.mode = FP_MODE_MATCH,
	};
	struct ec_response_fp_mode response;
	struct fingerprint_sensor_state state;
	uint32_t fp_events;

	/* Load example template. */
	zassert_ok(ec_cmd_fp_template(
		NULL,
		(struct ec_params_fp_template *)example_template_encrypted,
		sizeof(example_template_encrypted)));

	/* Negative value means error. */
	mock_alg_match_fake.return_val = -1;

	/* Switch mode to match. */
	zassert_ok(ec_cmd_fp_mode(NULL, &params, &response));
	zassert_true(response.mode & FP_MODE_MATCH);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Put finger on the sensor. */
	fingerprint_get_state(fp_sim, &state);
	state.finger_state = FINGERPRINT_FINGER_STATE_PRESENT;
	fingerprint_set_state(fp_sim, &state);

	/* Ping fpsensor task. */
	fingerprint_run_callback(fp_sim);

	/* Give opportunity for fpsensor task process event. */
	k_msleep(1);

	/* Make sure that 'match' was called. */
	zassert_equal(mock_alg_match_fake.call_count, 1);

	/* Confirm MKBP event was sent. */
	zassert_equal(mkbp_send_event_fake.call_count, 1);
	zassert_equal(mkbp_send_event_fake.arg0_val, EC_MKBP_EVENT_FINGERPRINT);

	/*
	 * Confirm that:
	 * - MKBP event is FP_MATCH
	 * - Match failed with NO_INTERNAL
	 * - Finger ID is FP_NO_SUCH_TEMPLATE
	 */
	fp_get_next_event((uint8_t *)&fp_events);
	zassert_true(fp_events & EC_MKBP_FP_MATCH);
	zassert_equal(EC_MKBP_FP_ERRCODE(fp_events),
		      EC_MKBP_FP_ERR_MATCH_NO_INTERNAL);
	zassert_equal(EC_MKBP_FP_MATCH_IDX(fp_events),
		      FP_NO_SUCH_TEMPLATE & 0xF);
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
	uint32_t fp_events;

	fingerprint_set_state(fp_sim, &state);
	RESET_FAKE(mkbp_send_event);

	RESET_FAKE(mock_alg_init);
	RESET_FAKE(mock_alg_exit);
	RESET_FAKE(mock_alg_enroll_start);
	RESET_FAKE(mock_alg_enroll_step);
	RESET_FAKE(mock_alg_enroll_finish);
	RESET_FAKE(mock_alg_match);

	match_result = 0;
	finger_index = 0;
	finger_updated_bitmap = 0;

	/* Set context (FP_CONTEXT_ASYNC). */
	ec_cmd_fp_context_v1(NULL, &fp_context_params);

	/* Give opportunity for fpsensor task to change mode. */
	k_msleep(1);

	/* Get context setting result. */
	fp_context_params.action = FP_CONTEXT_GET_RESULT;
	ec_cmd_fp_context_v1(NULL, &fp_context_params);

	/* Clear MKBP events from previous tests. */
	fp_get_next_event((uint8_t *)&fp_events);
}

ZTEST_SUITE(fpsensor_match, NULL, fpsensor_setup, fpsensor_before, NULL, NULL);
