/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "compile_time_macros.h"

extern "C" {
#include "atomic.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "cryptoc/util.h"
#include "ec_commands.h"
#include "fpsensor.h"
#include "fpsensor_crypto.h"
#include "fpsensor_detect.h"
#include "fpsensor_private.h"
#include "fpsensor_state.h"
#include "gpio.h"
#include "host_command.h"
#include "link_defs.h"
#include "mkbp_event.h"
#include "overflow.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "trng.h"
#include "util.h"
#include "watchdog.h"
}

#if !defined(CONFIG_RNG)
#error "fpsensor requires RNG"
#endif

#if defined(SECTION_IS_RO)
#error "fpsensor code should not be in RO image."
#endif

/* Ready to encrypt a template. */
static timestamp_t encryption_deadline;

/* raw image offset inside the acquired frame */
#ifndef FP_SENSOR_IMAGE_OFFSET
#define FP_SENSOR_IMAGE_OFFSET 0
#endif

#define FP_MODE_ANY_CAPTURE \
	(FP_MODE_CAPTURE | FP_MODE_ENROLL_IMAGE | FP_MODE_MATCH)
#define FP_MODE_ANY_DETECT_FINGER \
	(FP_MODE_FINGER_DOWN | FP_MODE_FINGER_UP | FP_MODE_ANY_CAPTURE)
#define FP_MODE_ANY_WAIT_IRQ (FP_MODE_FINGER_DOWN | FP_MODE_ANY_CAPTURE)

/* Delay between 2 s of the sensor to detect finger removal */
#define FINGER_POLLING_DELAY (100 * MSEC)

/* Timing statistics. */
static uint32_t capture_time_us;
static uint32_t matching_time_us;
static uint32_t overall_time_us;
static timestamp_t overall_t0;
static uint8_t timestamps_invalid;

BUILD_ASSERT(sizeof(struct ec_fp_template_encryption_metadata) % 4 == 0);

/* Interrupt line from the fingerprint sensor */
void fps_event(enum gpio_signal signal)
{
	task_set_event(TASK_ID_FPSENSOR, TASK_EVENT_SENSOR_IRQ);
}

static void send_mkbp_event(uint32_t event)
{
	atomic_or(&fp_events, event);
	mkbp_send_event(EC_MKBP_EVENT_FINGERPRINT);
}

static inline int is_raw_capture(uint32_t mode)
{
	int capture_type = FP_CAPTURE_TYPE(mode);

	return (capture_type == FP_CAPTURE_VENDOR_FORMAT ||
		capture_type == FP_CAPTURE_QUALITY_TEST);
}

__maybe_unused static bool fp_match_success(int match_result)
{
	return match_result == EC_MKBP_FP_ERR_MATCH_YES ||
	       match_result == EC_MKBP_FP_ERR_MATCH_YES_UPDATED ||
	       match_result == EC_MKBP_FP_ERR_MATCH_YES_UPDATE_FAILED;
}

#ifdef HAVE_FP_PRIVATE_DRIVER
static inline int is_test_capture(uint32_t mode)
{
	int capture_type = FP_CAPTURE_TYPE(mode);

	return (mode & FP_MODE_CAPTURE) &&
	       (capture_type == FP_CAPTURE_PATTERN0 ||
		capture_type == FP_CAPTURE_PATTERN1 ||
		capture_type == FP_CAPTURE_RESET_TEST);
}

/*
 * contains the bit FP_MODE_ENROLL_SESSION if a finger enrollment is on-going.
 * It is used to detect the ENROLL_SESSION transition when sensor_mode is
 * updated by the host.
 */
static uint32_t enroll_session;

static uint32_t fp_process_enroll(void)
{
	int percent = 0;
	int res;

	if (template_newly_enrolled != FP_NO_SUCH_TEMPLATE)
		CPRINTS("Warning: previously enrolled template has not been "
			"read yet.");

	/* begin/continue enrollment */
	CPRINTS("[%d]Enrolling ...", templ_valid);
	res = fp_finger_enroll(fp_buffer, &percent);
	CPRINTS("[%d]Enroll =>%d (%d%%)", templ_valid, res, percent);
	if (res < 0)
		return EC_MKBP_FP_ENROLL |
		       EC_MKBP_FP_ERRCODE(EC_MKBP_FP_ERR_ENROLL_INTERNAL);
	templ_dirty |= BIT(templ_valid);
	if (percent == 100) {
		res = fp_enrollment_finish(fp_template[templ_valid]);
		if (res) {
			res = EC_MKBP_FP_ERR_ENROLL_INTERNAL;
		} else {
			template_newly_enrolled = templ_valid;
			fp_enable_positive_match_secret(
				templ_valid, &positive_match_secret_state);
			templ_valid++;
		}
		sensor_mode &= ~FP_MODE_ENROLL_SESSION;
		enroll_session &= ~FP_MODE_ENROLL_SESSION;
	}
	return EC_MKBP_FP_ENROLL | EC_MKBP_FP_ERRCODE(res) |
	       (percent << EC_MKBP_FP_ENROLL_PROGRESS_OFFSET);
}

static uint32_t fp_process_match(void)
{
	timestamp_t t0 = get_time();
	int res = -1;
	uint32_t updated = 0;
	int32_t fgr = FP_NO_SUCH_TEMPLATE;

	/* match finger against current templates */
	fp_disable_positive_match_secret(&positive_match_secret_state);
	CPRINTS("Matching/%d ...", templ_valid);
	if (templ_valid) {
		res = fp_finger_match(fp_template[0], templ_valid, fp_buffer,
				      &fgr, &updated);
		CPRINTS("Match =>%d (finger %d)", res, fgr);

		if (fp_match_success(res)) {
			/*
			 * Match succeded! Let's check if template number
			 * is valid. If it is not valid, overwrite result
			 * with EC_MKBP_FP_ERR_MATCH_NO_INTERNAL.
			 */
			if (fgr >= 0 && fgr < FP_MAX_FINGER_COUNT) {
				fp_enable_positive_match_secret(
					fgr, &positive_match_secret_state);
			} else {
				res = EC_MKBP_FP_ERR_MATCH_NO_INTERNAL;
			}
		} else if (res < 0) {
			/*
			 * Negative result means that there is a problem with
			 * code responsible for matching. Overwrite it with
			 * MATCH_NO_INTERNAL to let upper layers know what
			 * happened.
			 */
			res = EC_MKBP_FP_ERR_MATCH_NO_INTERNAL;
		}

		if (res == EC_MKBP_FP_ERR_MATCH_YES_UPDATED)
			templ_dirty |= updated;
	} else {
		CPRINTS("No enrolled templates");
		res = EC_MKBP_FP_ERR_MATCH_NO_TEMPLATES;
	}

	if (!fp_match_success(res))
		timestamps_invalid |= FPSTATS_MATCHING_INV;

	matching_time_us = time_since32(t0);
	return EC_MKBP_FP_MATCH | EC_MKBP_FP_ERRCODE(res) |
	       ((fgr << EC_MKBP_FP_MATCH_IDX_OFFSET) &
		EC_MKBP_FP_MATCH_IDX_MASK);
}

static void fp_process_finger(void)
{
	timestamp_t t0 = get_time();
	int res;

	CPRINTS("Capturing ...");
	res = fp_sensor_acquire_image_with_mode(fp_buffer,
						FP_CAPTURE_TYPE(sensor_mode));
	capture_time_us = time_since32(t0);
	if (!res) {
		uint32_t evt = EC_MKBP_FP_IMAGE_READY;

		/* Clean up SPI before clocking up to avoid hang on the dsb
		 * in dma_go. Ignore the return value to let the WDT reboot
		 * the MCU (and avoid getting trapped in the loop).
		 * b/112781659 */
		res = spi_transaction_flush(&spi_devices[0]);
		if (res)
			CPRINTS("Failed to flush SPI: 0x%x", res);
		/* we need CPU power to do the computations */
		clock_enable_module(MODULE_FAST_CPU, 1);

		if (sensor_mode & FP_MODE_ENROLL_IMAGE)
			evt = fp_process_enroll();
		else if (sensor_mode & FP_MODE_MATCH)
			evt = fp_process_match();

		sensor_mode &= ~FP_MODE_ANY_CAPTURE;
		overall_time_us = time_since32(overall_t0);
		send_mkbp_event(evt);

		/* go back to lower power mode */
		clock_enable_module(MODULE_FAST_CPU, 0);
	} else {
		timestamps_invalid |= FPSTATS_CAPTURE_INV;
	}
}
#endif /* HAVE_FP_PRIVATE_DRIVER */

extern "C" void fp_task(void)
{
	int timeout_us = -1;

	CPRINTS("FP_SENSOR_SEL: %s",
		fp_sensor_type_to_str(get_fp_sensor_type()));

#ifdef HAVE_FP_PRIVATE_DRIVER
	/* Reset and initialize the sensor IC */
	fp_sensor_init();

	while (1) {
		uint32_t evt;
		enum finger_state st = FINGER_NONE;

		/* Wait for a sensor IRQ or a new mode configuration */
		evt = task_wait_event(timeout_us);

		if (evt & TASK_EVENT_UPDATE_CONFIG) {
			uint32_t mode = sensor_mode;

			gpio_disable_interrupt(GPIO_FPS_INT);
			if ((mode ^ enroll_session) & FP_MODE_ENROLL_SESSION) {
				if (mode & FP_MODE_ENROLL_SESSION) {
					if (fp_enrollment_begin())
						sensor_mode &=
							~FP_MODE_ENROLL_SESSION;
				} else {
					fp_enrollment_finish(NULL);
				}
				enroll_session = sensor_mode &
						 FP_MODE_ENROLL_SESSION;
			}
			if (is_test_capture(mode)) {
				fp_sensor_acquire_image_with_mode(
					fp_buffer, FP_CAPTURE_TYPE(mode));
				sensor_mode &= ~FP_MODE_CAPTURE;
				send_mkbp_event(EC_MKBP_FP_IMAGE_READY);
				continue;
			} else if (sensor_mode & FP_MODE_ANY_DETECT_FINGER) {
				/* wait for a finger on the sensor */
				fp_sensor_configure_detect();
			}
			if (sensor_mode & FP_MODE_DEEPSLEEP)
				/* Shutdown the sensor */
				fp_sensor_low_power();
			if (sensor_mode & FP_MODE_FINGER_UP)
				/* Poll the sensor to detect finger removal */
				timeout_us = FINGER_POLLING_DELAY;
			else
				timeout_us = -1;
			if (mode & FP_MODE_ANY_WAIT_IRQ) {
				gpio_enable_interrupt(GPIO_FPS_INT);
			} else if (mode & FP_MODE_RESET_SENSOR) {
				fp_reset_and_clear_context();
				sensor_mode &= ~FP_MODE_RESET_SENSOR;
			} else if (mode & FP_MODE_SENSOR_MAINTENANCE) {
				fp_maintenance();
				sensor_mode &= ~FP_MODE_SENSOR_MAINTENANCE;
			} else {
				fp_sensor_low_power();
			}
		} else if (evt & (TASK_EVENT_SENSOR_IRQ | TASK_EVENT_TIMER)) {
			overall_t0 = get_time();
			timestamps_invalid = 0;
			gpio_disable_interrupt(GPIO_FPS_INT);
			if (sensor_mode & FP_MODE_ANY_DETECT_FINGER) {
				st = fp_sensor_finger_status();
				if (st == FINGER_PRESENT &&
				    sensor_mode & FP_MODE_FINGER_DOWN) {
					CPRINTS("Finger!");
					sensor_mode &= ~FP_MODE_FINGER_DOWN;
					send_mkbp_event(EC_MKBP_FP_FINGER_DOWN);
				}
				if (st == FINGER_NONE &&
				    sensor_mode & FP_MODE_FINGER_UP) {
					sensor_mode &= ~FP_MODE_FINGER_UP;
					timeout_us = -1;
					send_mkbp_event(EC_MKBP_FP_FINGER_UP);
				}
			}

			if (st == FINGER_PRESENT &&
			    sensor_mode & FP_MODE_ANY_CAPTURE)
				fp_process_finger();

			if (sensor_mode & FP_MODE_ANY_WAIT_IRQ) {
				fp_sensor_configure_detect();
				gpio_enable_interrupt(GPIO_FPS_INT);
			} else {
				fp_sensor_low_power();
			}
		}
	}
#else /* !HAVE_FP_PRIVATE_DRIVER */
	while (1) {
		uint32_t evt = task_wait_event(timeout_us);

		send_mkbp_event(evt);
	}
#endif /* !HAVE_FP_PRIVATE_DRIVER */
}

static enum ec_status fp_command_passthru(struct host_cmd_handler_args *args)
{
	const struct ec_params_fp_passthru *params =
		static_cast<const ec_params_fp_passthru *>(args->params);
	uint8_t *out = static_cast<uint8_t *>(args->response);
	int rc;
	enum ec_status ret = EC_RES_SUCCESS;

	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	if (params->len >
		    args->params_size +
			    offsetof(struct ec_params_fp_passthru, data) ||
	    params->len > args->response_max)
		return EC_RES_INVALID_PARAM;

	rc = spi_transaction_async(&spi_devices[0], params->data, params->len,
				   out, SPI_READBACK_ALL);
	if (params->flags & EC_FP_FLAG_NOT_COMPLETE)
		rc |= spi_transaction_wait(&spi_devices[0]);
	else
		rc |= spi_transaction_flush(&spi_devices[0]);

	if (rc == EC_ERROR_TIMEOUT)
		ret = EC_RES_TIMEOUT;
	else if (rc)
		ret = EC_RES_ERROR;

	args->response_size = params->len;
	return ret;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_PASSTHRU, fp_command_passthru, EC_VER_MASK(0));

static enum ec_status fp_command_info(struct host_cmd_handler_args *args)
{
	struct ec_response_fp_info *r =
		static_cast<ec_response_fp_info *>(args->response);

#ifdef HAVE_FP_PRIVATE_DRIVER
	if (fp_sensor_get_info(r) < 0)
#endif
		return EC_RES_UNAVAILABLE;

	r->template_size = FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE;
	r->template_max = FP_MAX_FINGER_COUNT;
	r->template_valid = templ_valid;
	r->template_dirty = templ_dirty;
	r->template_version = FP_TEMPLATE_FORMAT_VERSION;

	/* V1 is identical to V0 with more information appended */
	args->response_size = args->version ?
				      sizeof(*r) :
				      sizeof(struct ec_response_fp_info_v0);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_INFO, fp_command_info,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

BUILD_ASSERT(FP_CONTEXT_NONCE_BYTES == 12);

int validate_fp_buffer_offset(const uint32_t buffer_size, const uint32_t offset,
			      const uint32_t size)
{
	uint32_t bytes_requested;

	if (check_add_overflow(size, offset, &bytes_requested))
		return EC_ERROR_OVERFLOW;

	if (bytes_requested > buffer_size)
		return EC_ERROR_INVAL;

	return EC_SUCCESS;
}

static enum ec_status fp_command_frame(struct host_cmd_handler_args *args)
{
	const struct ec_params_fp_frame *params =
		static_cast<const struct ec_params_fp_frame *>(args->params);
	void *out = args->response;
	uint32_t idx = FP_FRAME_GET_BUFFER_INDEX(params->offset);
	uint32_t offset = params->offset & FP_FRAME_OFFSET_MASK;
	uint32_t size = params->size;
	uint32_t fgr;
	uint8_t key[SBP_ENC_KEY_LEN];
	struct ec_fp_template_encryption_metadata *enc_info;
	int ret;

	if (size > args->response_max)
		return EC_RES_INVALID_PARAM;

	if (idx == FP_FRAME_INDEX_RAW_IMAGE) {
		/* The host requested a frame. */
		if (system_is_locked())
			return EC_RES_ACCESS_DENIED;
		if (!is_raw_capture(sensor_mode))
			offset += FP_SENSOR_IMAGE_OFFSET;

		ret = validate_fp_buffer_offset(sizeof(fp_buffer), offset,
						size);
		if (ret != EC_SUCCESS)
			return EC_RES_INVALID_PARAM;

		memcpy(out, fp_buffer + offset, size);
		args->response_size = size;
		return EC_RES_SUCCESS;
	}

	/* The host requested a template. */

	/* Templates are numbered from 1 in this host request. */
	fgr = idx - FP_FRAME_INDEX_TEMPLATE;

	if (fgr >= FP_MAX_FINGER_COUNT)
		return EC_RES_INVALID_PARAM;
	if (fgr >= templ_valid)
		return EC_RES_UNAVAILABLE;
	ret = validate_fp_buffer_offset(sizeof(fp_enc_buffer), offset, size);
	if (ret != EC_SUCCESS)
		return EC_RES_INVALID_PARAM;

	if (!offset) {
		/* Host has requested the first chunk, do the encryption. */
		timestamp_t now = get_time();
		/* Encrypted template is after the metadata. */
		uint8_t *encrypted_template = fp_enc_buffer + sizeof(*enc_info);
		/* Positive match salt is after the template. */
		uint8_t *positive_match_salt =
			encrypted_template + sizeof(fp_template[0]);
		size_t encrypted_blob_size = sizeof(fp_template[0]) +
					     sizeof(fp_positive_match_salt[0]);

		/* b/114160734: Not more than 1 encrypted message per second. */
		if (!timestamp_expired(encryption_deadline, &now))
			return EC_RES_BUSY;
		encryption_deadline.val = now.val + (1 * SECOND);

		memset(fp_enc_buffer, 0, sizeof(fp_enc_buffer));
		/*
		 * The beginning of the buffer contains nonce, encryption_salt
		 * and tag.
		 */
		enc_info = (struct ec_fp_template_encryption_metadata
				    *)(fp_enc_buffer);
		enc_info->struct_version = FP_TEMPLATE_FORMAT_VERSION;
		trng_init();
		trng_rand_bytes(enc_info->nonce, FP_CONTEXT_NONCE_BYTES);
		trng_rand_bytes(enc_info->encryption_salt,
				FP_CONTEXT_ENCRYPTION_SALT_BYTES);
		trng_exit();

		/*
		 * TODO(http://b/244781166): Use consistent types so cast is
		 * not needed.
		 */
		if (fgr == (uint32_t)template_newly_enrolled) {
			/*
			 * Newly enrolled templates need new positive match
			 * salt, new positive match secret and new validation
			 * value.
			 */
			template_newly_enrolled = FP_NO_SUCH_TEMPLATE;
			trng_init();
			trng_rand_bytes(fp_positive_match_salt[fgr],
					FP_POSITIVE_MATCH_SALT_BYTES);
			trng_exit();
		}

		ret = derive_encryption_key(key, enc_info->encryption_salt);
		if (ret != EC_SUCCESS) {
			CPRINTS("fgr%d: Failed to derive key", fgr);
			return EC_RES_UNAVAILABLE;
		}

		/*
		 * Copy the payload to |fp_enc_buffer| where it will be
		 * encrypted in-place.
		 */
		memcpy(encrypted_template, fp_template[fgr],
		       sizeof(fp_template[0]));
		memcpy(positive_match_salt, fp_positive_match_salt[fgr],
		       sizeof(fp_positive_match_salt[0]));

		/* Encrypt the secret blob in-place. */
		ret = aes_gcm_encrypt(key, SBP_ENC_KEY_LEN, encrypted_template,
				      encrypted_template, encrypted_blob_size,
				      enc_info->nonce, FP_CONTEXT_NONCE_BYTES,
				      enc_info->tag, FP_CONTEXT_TAG_BYTES);
		always_memset(key, 0, sizeof(key));
		if (ret != EC_SUCCESS) {
			CPRINTS("fgr%d: Failed to encrypt template", fgr);
			return EC_RES_UNAVAILABLE;
		}
		templ_dirty &= ~BIT(fgr);
	}
	memcpy(out, fp_enc_buffer + offset, size);
	args->response_size = size;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_FRAME, fp_command_frame, EC_VER_MASK(0));

static enum ec_status fp_command_stats(struct host_cmd_handler_args *args)
{
	struct ec_response_fp_stats *r =
		static_cast<struct ec_response_fp_stats *>(args->response);

	r->capture_time_us = capture_time_us;
	r->matching_time_us = matching_time_us;
	r->overall_time_us = overall_time_us;
	r->overall_t0.lo = overall_t0.le.lo;
	r->overall_t0.hi = overall_t0.le.hi;
	r->timestamps_invalid = timestamps_invalid;
	/*
	 * Note that this is set to FP_NO_SUCH_TEMPLATE when positive match
	 * secret is read/disabled, and we are not using this field in biod.
	 */
	r->template_matched = positive_match_secret_state.template_matched;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_STATS, fp_command_stats, EC_VER_MASK(0));

static bool template_needs_validation_value(
	struct ec_fp_template_encryption_metadata *enc_info)
{
	return enc_info->struct_version == 3 && FP_TEMPLATE_FORMAT_VERSION == 4;
}

static int
validate_template_format(struct ec_fp_template_encryption_metadata *enc_info)
{
	if (template_needs_validation_value(enc_info))
		/* The host requested migration to v4. */
		return EC_RES_SUCCESS;

	if (enc_info->struct_version != FP_TEMPLATE_FORMAT_VERSION) {
		CPRINTS("Invalid template format %d", enc_info->struct_version);
		return EC_RES_INVALID_PARAM;
	}
	return EC_RES_SUCCESS;
}

static enum ec_status fp_command_template(struct host_cmd_handler_args *args)
{
	const struct ec_params_fp_template *params =
		static_cast<const struct ec_params_fp_template *>(args->params);
	uint32_t size = params->size & ~FP_TEMPLATE_COMMIT;
	int xfer_complete = params->size & FP_TEMPLATE_COMMIT;
	uint32_t offset = params->offset;
	uint32_t idx = templ_valid;
	uint8_t key[SBP_ENC_KEY_LEN];
	struct ec_fp_template_encryption_metadata *enc_info;
	int ret;

	/* Can we store one more template ? */
	if (idx >= FP_MAX_FINGER_COUNT)
		return EC_RES_OVERFLOW;

	if (args->params_size !=
	    size + offsetof(struct ec_params_fp_template, data))
		return EC_RES_INVALID_PARAM;
	ret = validate_fp_buffer_offset(sizeof(fp_enc_buffer), offset, size);
	if (ret != EC_SUCCESS)
		return EC_RES_INVALID_PARAM;

	memcpy(&fp_enc_buffer[offset], params->data, size);

	if (xfer_complete) {
		/* Encrypted template is after the metadata. */
		uint8_t *encrypted_template = fp_enc_buffer + sizeof(*enc_info);
		/* Positive match salt is after the template. */
		uint8_t *positive_match_salt =
			encrypted_template + sizeof(fp_template[0]);
		size_t encrypted_blob_size;

		/*
		 * The complete encrypted template has been received, start
		 * decryption.
		 */
		fp_clear_finger_context(idx);
		/*
		 * The beginning of the buffer contains nonce, encryption_salt
		 * and tag.
		 */
		enc_info = (struct ec_fp_template_encryption_metadata *)
			fp_enc_buffer;
		ret = validate_template_format(enc_info);
		if (ret != EC_RES_SUCCESS) {
			CPRINTS("fgr%d: Template format not supported", idx);
			return EC_RES_INVALID_PARAM;
		}

		if (enc_info->struct_version <= 3) {
			encrypted_blob_size = sizeof(fp_template[0]);
		} else {
			encrypted_blob_size = sizeof(fp_template[0]) +
					      sizeof(fp_positive_match_salt[0]);
		}

		ret = derive_encryption_key(key, enc_info->encryption_salt);
		if (ret != EC_SUCCESS) {
			CPRINTS("fgr%d: Failed to derive key", idx);
			return EC_RES_UNAVAILABLE;
		}

		/* Decrypt the secret blob in-place. */
		ret = aes_gcm_decrypt(key, SBP_ENC_KEY_LEN, encrypted_template,
				      encrypted_template, encrypted_blob_size,
				      enc_info->nonce, FP_CONTEXT_NONCE_BYTES,
				      enc_info->tag, FP_CONTEXT_TAG_BYTES);
		always_memset(key, 0, sizeof(key));
		if (ret != EC_SUCCESS) {
			CPRINTS("fgr%d: Failed to decipher template", idx);
			/* Don't leave bad data in the template buffer */
			fp_clear_finger_context(idx);
			return EC_RES_UNAVAILABLE;
		}
		memcpy(fp_template[idx], encrypted_template,
		       sizeof(fp_template[0]));
		if (template_needs_validation_value(enc_info)) {
			CPRINTS("fgr%d: Generating positive match salt.", idx);
			trng_init();
			trng_rand_bytes(positive_match_salt,
					FP_POSITIVE_MATCH_SALT_BYTES);
			trng_exit();
		}
		if (bytes_are_trivial(positive_match_salt,
				      sizeof(fp_positive_match_salt[0]))) {
			CPRINTS("fgr%d: Trivial positive match salt.", idx);
			always_memset(fp_template[idx], 0,
				      sizeof(fp_template[0]));
			return EC_RES_INVALID_PARAM;
		}
		memcpy(fp_positive_match_salt[idx], positive_match_salt,
		       sizeof(fp_positive_match_salt[0]));

		templ_valid++;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_TEMPLATE, fp_command_template, EC_VER_MASK(0));

#ifdef CONFIG_CMD_FPSENSOR_DEBUG
/* --- Debug console commands --- */

/*
 * Send the current Fingerprint buffer to the host
 * it is formatted as an 8-bpp PGM ASCII file.
 *
 * In addition, it prepends a short Z-Modem download signature,
 * which triggers automatically your preferred viewer if you configure it
 * properly in "File transfer protocols" in the Minicom options menu.
 * (as triggered by Ctrl-A O)
 * +--------------------------------------------------------------------------+
 * |     Name             Program             Name U/D FullScr IO-Red. Multi  |
 * | A  zmodem     /usr/bin/sz -vv -b          Y    U    N       Y       Y    |
 *  [...]
 * | L  pgm        /usr/bin/display_pgm        N    D    N       Y       N    |
 * | M  Zmodem download string activates... L                                 |
 *
 * My /usr/bin/display_pgm looks like this:
 * #!/bin/sh
 * TMPF=$(mktemp)
 * ascii-xfr -rdv ${TMPF}
 * display ${TMPF}
 *
 * Alternative (if you're using screen as your terminal):
 *
 * From *outside* the chroot:
 *
 * Install ascii-xfr: sudo apt-get install minicom
 * Install imagemagick: sudo apt-get install imagemagick
 *
 * Add the following to your ${HOME}/.screenrc:
 *
 * zmodem catch
 * zmodem recvcmd '!!! bash -c "ascii-xfr -rdv /tmp/finger.pgm && display
 * /tmp/finger.pgm"'
 *
 * From *outside the chroot*, use screen to connect to UART console:
 *
 * sudo screen -c ${HOME}/.screenrc /dev/pts/NN 115200
 *
 */
static void upload_pgm_image(uint8_t *frame)
{
	int x, y;
	uint8_t *ptr = frame;

	/* fake Z-modem ZRQINIT signature */
	CPRINTF("#IGNORE for ZModem\r**\030B00");
	msleep(2000); /* let the download program start */
	/* Print 8-bpp PGM ASCII header */
	CPRINTF("P2\n%d %d\n255\n", FP_SENSOR_RES_X, FP_SENSOR_RES_Y);

	for (y = 0; y < FP_SENSOR_RES_Y; y++) {
		watchdog_reload();
		for (x = 0; x < FP_SENSOR_RES_X; x++, ptr++)
			CPRINTF("%d ", *ptr);
		CPRINTF("\n");
		cflush();
	}

	CPRINTF("\x04"); /* End Of Transmission */
}

static enum ec_error_list fp_console_action(uint32_t mode)
{
	int tries = 200;
	uint32_t mode_output = 0;
	int rc = 0;

	if (!(sensor_mode & FP_MODE_RESET_SENSOR))
		CPRINTS("Waiting for finger ...");

	rc = fp_set_sensor_mode(mode, &mode_output);

	if (rc != EC_RES_SUCCESS) {
		/*
		 * EC host command errors do not directly map to console command
		 * errors.
		 */
		return EC_ERROR_UNKNOWN;
	}

	while (tries--) {
		if (!(sensor_mode & FP_MODE_ANY_CAPTURE)) {
			CPRINTS("done (events:%x)", (int)fp_events);
			return EC_SUCCESS;
		}
		usleep(100 * MSEC);
	}
	return EC_ERROR_TIMEOUT;
}

static int command_fpcapture(int argc, const char **argv)
{
	int capture_type = FP_CAPTURE_SIMPLE_IMAGE;
	uint32_t mode;
	enum ec_error_list rc;

	/*
	 * TODO(b/142944002): Remove this redundant check for system_is_locked
	 * once we have unit-tests/integration-tests in place.
	 */
	if (system_is_locked())
		return EC_ERROR_ACCESS_DENIED;

	if (argc >= 2) {
		char *e;

		capture_type = strtoi(argv[1], &e, 0);
		if (*e || capture_type < 0)
			return EC_ERROR_PARAM1;
	}
	mode = FP_MODE_CAPTURE | ((capture_type << FP_MODE_CAPTURE_TYPE_SHIFT) &
				  FP_MODE_CAPTURE_TYPE_MASK);

	rc = fp_console_action(mode);
	if (rc == EC_SUCCESS)
		upload_pgm_image(fp_buffer + FP_SENSOR_IMAGE_OFFSET);

	return rc;
}
DECLARE_CONSOLE_COMMAND_FLAGS(fpcapture, command_fpcapture, NULL,
			      "Capture fingerprint in PGM format",
			      CMD_FLAG_RESTRICTED);

static int command_fpenroll(int argc, const char **argv)
{
	enum ec_error_list rc;
	int percent = 0;
	uint32_t event;
	static const char *const enroll_str[] = { "OK", "Low Quality",
						  "Immobile", "Low Coverage" };

	/*
	 * TODO(b/142944002): Remove this redundant check for system_is_locked
	 * once we have unit-tests/integration-tests in place.
	 */
	if (system_is_locked())
		return EC_ERROR_ACCESS_DENIED;

	do {
		int tries = 1000;

		rc = fp_console_action(FP_MODE_ENROLL_SESSION |
				       FP_MODE_ENROLL_IMAGE);
		if (rc != EC_SUCCESS)
			break;
		event = atomic_clear(&fp_events);
		percent = EC_MKBP_FP_ENROLL_PROGRESS(event);
		CPRINTS("Enroll capture: %s (%d%%)",
			enroll_str[EC_MKBP_FP_ERRCODE(event) & 3], percent);
		/* wait for finger release between captures */
		sensor_mode = FP_MODE_ENROLL_SESSION | FP_MODE_FINGER_UP;
		task_set_event(TASK_ID_FPSENSOR, TASK_EVENT_UPDATE_CONFIG);
		while (tries-- && sensor_mode & FP_MODE_FINGER_UP)
			usleep(20 * MSEC);
	} while (percent < 100);
	sensor_mode = 0; /* reset FP_MODE_ENROLL_SESSION */
	task_set_event(TASK_ID_FPSENSOR, TASK_EVENT_UPDATE_CONFIG);

	return rc;
}
DECLARE_CONSOLE_COMMAND_FLAGS(fpenroll, command_fpenroll, NULL,
			      "Enroll a new fingerprint", CMD_FLAG_RESTRICTED);

static int command_fpmatch(int argc, const char **argv)
{
	enum ec_error_list rc = fp_console_action(FP_MODE_MATCH);
	uint32_t event = atomic_clear(&fp_events);

	if (rc == EC_SUCCESS && event & EC_MKBP_FP_MATCH) {
		uint32_t match_errcode = EC_MKBP_FP_ERRCODE(event);

		CPRINTS("Match: %s (%d)",
			fp_match_success(match_errcode) ? "YES" : "NO",
			match_errcode);
	}

	return rc;
}
DECLARE_CONSOLE_COMMAND(fpmatch, command_fpmatch, NULL,
			"Run match algorithm against finger");

static int command_fpclear(int argc, const char **argv)
{
	/*
	 * We intentionally run this on the fp_task so that we use the
	 * same code path as host commands.
	 */
	enum ec_error_list rc = fp_console_action(FP_MODE_RESET_SENSOR);

	if (rc < 0)
		CPRINTS("Failed to clear fingerprint context: %d", rc);

	atomic_clear(&fp_events);

	return rc;
}
DECLARE_CONSOLE_COMMAND(fpclear, command_fpclear, NULL,
			"Clear fingerprint sensor context");

static int command_fpmaintenance(int argc, const char **argv)
{
#ifdef HAVE_FP_PRIVATE_DRIVER
	return fp_maintenance();
#else
	return EC_SUCCESS;
#endif /* #ifdef HAVE_FP_PRIVATE_DRIVER */
}
DECLARE_CONSOLE_COMMAND(fpmaintenance, command_fpmaintenance, NULL,
			"Run fingerprint sensor maintenance");

#endif /* CONFIG_CMD_FPSENSOR_DEBUG */
