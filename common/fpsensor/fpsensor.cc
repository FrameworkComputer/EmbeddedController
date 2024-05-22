/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "assert.h"
#include "atomic.h"
#include "clock.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "crypto/cleanse_wrapper.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_crypto.h"
#include "fpsensor/fpsensor_detect.h"
#include "fpsensor/fpsensor_modes.h"
#include "fpsensor/fpsensor_state.h"
#include "fpsensor/fpsensor_template_state.h"
#include "fpsensor/fpsensor_utils.h"
#include "gpio.h"
#include "host_command.h"
#include "link_defs.h"
#include "mkbp_event.h"
#include "openssl/mem.h"
#include "scoped_fast_cpu.h"
#include "sha256.h"
#include "spi.h"
#include "system.h"
#include "task.h"
#include "trng.h"
#include "util.h"
#include "watchdog.h"

#include <array>
#include <variant>

#ifdef CONFIG_ZEPHYR
#include <zephyr/shell/shell.h>
#endif

#if !defined(CONFIG_RNG)
#error "fpsensor requires RNG"
#endif

#if defined(SECTION_IS_RO)
#error "fpsensor code should not be in RO image."
#endif

/* Ready to encrypt a template. */
static timestamp_t encryption_deadline;

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
extern "C" void fps_event(enum gpio_signal signal)
{
	task_set_event(TASK_ID_FPSENSOR, TASK_EVENT_SENSOR_IRQ);
}

static void send_mkbp_event(uint32_t event)
{
	atomic_or(&global_context.fp_events, event);
	mkbp_send_event(EC_MKBP_EVENT_FINGERPRINT);
}

/*
 * Returns true if the mode is one that yields a frame in which
 * all bytes should be returned over EC_CMD_FP_FRAME.
 *
 * Other captures modes (simple, pattern0, pattern1, and reset_test) are
 * only interested in the height*width*bpp image bytes that are offset inside
 * the frame.
 *
 * These modes correspond to using the ectool fpframe "raw" modifier.
 */
static inline int is_raw_capture(uint32_t mode)
{
	int capture_type = FP_CAPTURE_TYPE(mode);

	return (capture_type == FP_CAPTURE_VENDOR_FORMAT ||
		capture_type == FP_CAPTURE_QUALITY_TEST);
}

#ifdef HAVE_FP_PRIVATE_DRIVER
/*
 * Returns true if the mode is a test capture that does not require finger
 * touch.
 */
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

	if (global_context.template_newly_enrolled != FP_NO_SUCH_TEMPLATE)
		CPRINTS("Warning: previously enrolled template has not been "
			"read yet.");

	/* begin/continue enrollment */
	CPRINTS("[%d]Enrolling ...", global_context.templ_valid);
	res = fp_finger_enroll(fp_buffer, &percent);
	CPRINTS("[%d]Enroll =>%d (%d%%)", global_context.templ_valid, res,
		percent);
	if (res < 0)
		return EC_MKBP_FP_ENROLL |
		       EC_MKBP_FP_ERRCODE(EC_MKBP_FP_ERR_ENROLL_INTERNAL);
	global_context.templ_dirty |= BIT(global_context.templ_valid);
	if (percent == 100) {
		res = fp_enrollment_finish(
			fp_template[global_context.templ_valid]);
		if (res) {
			res = EC_MKBP_FP_ERR_ENROLL_INTERNAL;
		} else {
			global_context.template_newly_enrolled =
				global_context.templ_valid;
			fp_enable_positive_match_secret(
				global_context.templ_valid,
				&global_context.positive_match_secret_state);
			fp_init_decrypted_template_state_with_user_id(
				global_context.templ_valid);
			global_context.templ_valid++;
		}
		global_context.sensor_mode &= ~FP_MODE_ENROLL_SESSION;
		enroll_session &= ~FP_MODE_ENROLL_SESSION;
	}
	return EC_MKBP_FP_ENROLL | EC_MKBP_FP_ERRCODE(res) |
	       (percent << EC_MKBP_FP_ENROLL_PROGRESS_OFFSET);
}

static bool authenticate_fp_match_state(void)
{
	/* The rate limit is only meanful for the nonce context, and we don't
	 * have rate limit for the legacy FP user unlock flow. */
	if (!(global_context.fp_encryption_status &
	      FP_CONTEXT_STATUS_NONCE_CONTEXT_SET)) {
		return true;
	}

	if (!(global_context.fp_encryption_status &
	      FP_CONTEXT_TEMPLATE_UNLOCKED_SET)) {
		CPRINTS("Cannot process match without unlock template");
		return false;
	}

	if (global_context.fp_encryption_status &
	    FP_CONTEXT_STATUS_MATCH_PROCESSED_SET) {
		CPRINTS("Cannot process match twice in nonce context");
		return false;
	}

	return true;
}

static uint32_t fp_process_match(void)
{
	timestamp_t t0 = get_time();
	int res = -1;
	uint32_t updated = 0;
	int32_t fgr = FP_NO_SUCH_TEMPLATE;

	/* match finger against current templates */
	fp_disable_positive_match_secret(
		&global_context.positive_match_secret_state);

	if (!authenticate_fp_match_state()) {
		res = EC_MKBP_FP_ERR_MATCH_NO_AUTH_FAIL;
		return EC_MKBP_FP_MATCH | EC_MKBP_FP_ERRCODE(res) |
		       ((fgr << EC_MKBP_FP_MATCH_IDX_OFFSET) &
			EC_MKBP_FP_MATCH_IDX_MASK);
	}

	/* The match processed state will be used to prevent the template unlock
	 * operation after match processed in a nonce context. If we don't do
	 * that, the attacker can unlock template multiple times in a single
	 * nonce context. */
	global_context.fp_encryption_status |=
		FP_CONTEXT_STATUS_MATCH_PROCESSED_SET;

	CPRINTS("Matching/%d ...", global_context.templ_valid);
	if (global_context.templ_valid) {
		res = fp_finger_match(fp_template[0],
				      global_context.templ_valid, fp_buffer,
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
					fgr,
					&global_context
						 .positive_match_secret_state);
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
			global_context.templ_dirty |= updated;
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
	res = fp_acquire_image_with_mode(
		fp_buffer, FP_CAPTURE_TYPE(global_context.sensor_mode));
	capture_time_us = time_since32(t0);
	if (!res) {
		uint32_t evt = EC_MKBP_FP_IMAGE_READY;

#ifndef CONFIG_ZEPHYR
		/* Clean up SPI before clocking up to avoid hang on the dsb
		 * in dma_go. Ignore the return value to let the WDT reboot
		 * the MCU (and avoid getting trapped in the loop).
		 * b/112781659 */
		res = spi_transaction_flush(&spi_devices[0]);
		if (res)
			CPRINTS("Failed to flush SPI: 0x%x", res);
#endif

		/* we need CPU power to do the computations */
		ScopedFastCpu fast_cpu;

		if (global_context.sensor_mode & FP_MODE_ENROLL_IMAGE)
			evt = fp_process_enroll();
		else if (global_context.sensor_mode & FP_MODE_MATCH)
			evt = fp_process_match();

		global_context.sensor_mode &= ~FP_MODE_ANY_CAPTURE;
		overall_time_us = time_since32(overall_t0);
		send_mkbp_event(evt);
	} else {
		timestamps_invalid |= FPSTATS_CAPTURE_INV;
	}
}
#endif /* HAVE_FP_PRIVATE_DRIVER */

extern "C" void fp_task(void)
{
	int timeout_us = -1;

	CPRINTS("FP_SENSOR_SEL: %s",
		fp_sensor_type_to_str(fpsensor_detect_get_type()));

#ifdef HAVE_FP_PRIVATE_DRIVER
	/* Reset and initialize the sensor IC */
	fp_sensor_init();

	while (1) {
		uint32_t evt;
		enum finger_state st = FINGER_NONE;

		/* Wait for a sensor IRQ or a new mode configuration */
		evt = task_wait_event(timeout_us);

		if (evt & TASK_EVENT_UPDATE_CONFIG) {
			uint32_t mode = global_context.sensor_mode;
			/*
			 * TODO(b/316859625): Remove CONFIG_ZEPHYR block after
			 * migration to Zephyr is completed.
			 */
#ifdef CONFIG_ZEPHYR
			/*
			 * We are about to change sensor mode, so exit any
			 * previous states.
			 */
			fp_idle();
#else
			gpio_disable_interrupt(GPIO_FPS_INT);
#endif
			if ((mode ^ enroll_session) & FP_MODE_ENROLL_SESSION) {
				if (mode & FP_MODE_ENROLL_SESSION) {
					if (fp_enrollment_begin())
						global_context.sensor_mode &=
							~FP_MODE_ENROLL_SESSION;
				} else {
					fp_enrollment_finish(NULL);
				}
				enroll_session = global_context.sensor_mode &
						 FP_MODE_ENROLL_SESSION;
			}
			if (is_test_capture(mode)) {
				fp_acquire_image_with_mode(
					fp_buffer, FP_CAPTURE_TYPE(mode));
				global_context.sensor_mode &= ~FP_MODE_CAPTURE;
				send_mkbp_event(EC_MKBP_FP_IMAGE_READY);
				continue;
			} else if (global_context.sensor_mode &
				   FP_MODE_ANY_DETECT_FINGER) {
				/* wait for a finger on the sensor */
				fp_configure_detect();
			}
			if (global_context.sensor_mode & FP_MODE_DEEPSLEEP)
				/* Shutdown the sensor */
				fp_sensor_low_power();
			if (global_context.sensor_mode & FP_MODE_FINGER_UP)
				/* Poll the sensor to detect finger removal */
				timeout_us = FINGER_POLLING_DELAY;
			else
				timeout_us = -1;
			if (mode & FP_MODE_ANY_WAIT_IRQ) {
				/*
				 * FP_MODE_ANY_WAIT_IRQ is a subset of
				 * FP_MODE_ANY_DETECT_FINGER. In Zephyr FPMCU
				 * interrupts are enabled by the sensor driver
				 * when configuring finger detection.
				 */
#ifndef CONFIG_ZEPHYR
				gpio_clear_pending_interrupt(GPIO_FPS_INT);
				gpio_enable_interrupt(GPIO_FPS_INT);
#endif
			} else if (mode & FP_MODE_RESET_SENSOR) {
				fp_reset_and_clear_context();
				global_context.sensor_mode &=
					~FP_MODE_RESET_SENSOR;
			} else if (mode & FP_MODE_SENSOR_MAINTENANCE) {
				fp_maintenance();
				global_context.sensor_mode &=
					~FP_MODE_SENSOR_MAINTENANCE;
			} else {
				fp_sensor_low_power();
			}
		} else if (evt & (TASK_EVENT_SENSOR_IRQ | TASK_EVENT_TIMER)) {
			overall_t0 = get_time();
			timestamps_invalid = 0;
			/*
			 * TODO(b/316859625): Remove CONFIG_ZEPHYR block after
			 * migration to Zephyr is completed.
			 */
#ifdef CONFIG_ZEPHYR
			/* On timeout, put sensor into idle state. */
			if (evt & TASK_EVENT_TIMER)
				fp_idle();
#else
			gpio_disable_interrupt(GPIO_FPS_INT);
#endif
			if (global_context.sensor_mode &
			    FP_MODE_ANY_DETECT_FINGER) {
				st = fp_finger_status();
				if (st == FINGER_PRESENT &&
				    global_context.sensor_mode &
					    FP_MODE_FINGER_DOWN) {
					CPRINTS("Finger!");
					global_context.sensor_mode &=
						~FP_MODE_FINGER_DOWN;
					send_mkbp_event(EC_MKBP_FP_FINGER_DOWN);
				}
				if (st == FINGER_NONE &&
				    global_context.sensor_mode &
					    FP_MODE_FINGER_UP) {
					global_context.sensor_mode &=
						~FP_MODE_FINGER_UP;
					timeout_us = -1;
					send_mkbp_event(EC_MKBP_FP_FINGER_UP);
				}
			}

			if (st == FINGER_PRESENT &&
			    global_context.sensor_mode & FP_MODE_ANY_CAPTURE)
				fp_process_finger();

			if (global_context.sensor_mode & FP_MODE_ANY_WAIT_IRQ) {
				fp_configure_detect();

				/* In Zephyr FPMCU interrupts are enabled by the
				 * sensor driver when configuring finger
				 * detection.
				 */
#ifndef CONFIG_ZEPHYR
				gpio_clear_pending_interrupt(GPIO_FPS_INT);
				gpio_enable_interrupt(GPIO_FPS_INT);
#endif
			} else {
				/*
				 * In Zephyr FPMCU interrupts are managed by
				 * the driver.
				 */
#ifndef CONFIG_ZEPHYR
				if (evt & (TASK_EVENT_SENSOR_IRQ))
					gpio_clear_pending_interrupt(
						GPIO_FPS_INT);
#endif
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

static enum ec_status fp_command_info(struct host_cmd_handler_args *args)
{
	auto *r = static_cast<ec_response_fp_info *>(args->response);

#ifdef HAVE_FP_PRIVATE_DRIVER
	if (fp_sensor_get_info(r) < 0)
#endif
		return EC_RES_UNAVAILABLE;

	r->template_size = FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE;
	r->template_max = FP_MAX_FINGER_COUNT;
	r->template_valid = global_context.templ_valid;
	r->template_dirty = global_context.templ_dirty;
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

static enum ec_status fp_command_frame(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const struct ec_params_fp_frame *>(args->params);
	void *out = args->response;
	uint16_t idx = FP_FRAME_GET_BUFFER_INDEX(params->offset);
	uint32_t offset = params->offset & FP_FRAME_OFFSET_MASK;
	uint32_t size = params->size;
	uint16_t fgr;
	struct ec_fp_template_encryption_metadata *enc_info;
	enum ec_error_list ret;

	if (size > args->response_max)
		return EC_RES_INVALID_PARAM;

	if (idx == FP_FRAME_INDEX_RAW_IMAGE) {
		/* The host requested a frame. */
		if (system_is_locked())
			return EC_RES_ACCESS_DENIED;
		/*
		 * Checks if the capture mode is one where we only care about
		 * the embedded/offset image bytes, like simple, pattern0,
		 * pattern1, and reset_test.
		 */
		if (!is_raw_capture(global_context.sensor_mode))
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
	if (fgr >= global_context.templ_valid)
		return EC_RES_UNAVAILABLE;
	ret = validate_fp_buffer_offset(sizeof(fp_enc_buffer), offset, size);
	if (ret != EC_SUCCESS)
		return EC_RES_INVALID_PARAM;

	if (!offset) {
		ScopedFastCpu fast_cpu;

		/* Host has requested the first chunk, do the encryption. */
		timestamp_t now = get_time();

		/* Encrypted template is after the metadata. */
		std::span templ = fp_enc_buffer.fp_template;
		/* Positive match salt is after the template. */
		std::span positive_match_salt =
			fp_enc_buffer.positive_match_salt;
		std::span encrypted_template_and_positive_match_salt(
			templ.data(),
			templ.size_bytes() + positive_match_salt.size_bytes());

		/* b/114160734: Not more than 1 encrypted message per second. */
		if (!timestamp_expired(encryption_deadline, &now))
			return EC_RES_BUSY;
		encryption_deadline.val = now.val + (1 * SECOND);

		memset(&fp_enc_buffer, 0, sizeof(fp_enc_buffer));
		/*
		 * The beginning of the buffer contains nonce, encryption_salt
		 * and tag.
		 */
		enc_info = &fp_enc_buffer.metadata;
		enc_info->struct_version = FP_TEMPLATE_FORMAT_VERSION;
		trng_init();
		trng_rand_bytes(enc_info->nonce, FP_CONTEXT_NONCE_BYTES);
		trng_rand_bytes(enc_info->encryption_salt,
				FP_CONTEXT_ENCRYPTION_SALT_BYTES);
		trng_exit();

		if (fgr == global_context.template_newly_enrolled) {
			/*
			 * Newly enrolled templates need new positive match
			 * salt, new positive match secret and new validation
			 * value.
			 */
			global_context.template_newly_enrolled =
				FP_NO_SUCH_TEMPLATE;
			trng_init();
			trng_rand_bytes(
				global_context.fp_positive_match_salt[fgr],
				FP_POSITIVE_MATCH_SALT_BYTES);
			trng_exit();
		}

		FpEncryptionKey key;
		ret = derive_encryption_key(key, enc_info->encryption_salt,
					    global_context.user_id,
					    global_context.tpm_seed);
		if (ret != EC_SUCCESS) {
			CPRINTS("fgr%d: Failed to derive key", fgr);
			return EC_RES_UNAVAILABLE;
		}

		/*
		 * Copy the payload to |fp_enc_buffer| where it will be
		 * encrypted in-place.
		 */
		std::ranges::copy(fp_template[fgr], templ.begin());
		std::ranges::copy(global_context.fp_positive_match_salt[fgr],
				  positive_match_salt.begin());

		/* Encrypt the secret blob in-place. */
		ret = aes_128_gcm_encrypt(
			key, encrypted_template_and_positive_match_salt,
			encrypted_template_and_positive_match_salt,
			enc_info->nonce, enc_info->tag);
		if (ret != EC_SUCCESS) {
			CPRINTS("fgr%d: Failed to encrypt template", fgr);
			return EC_RES_UNAVAILABLE;
		}
		global_context.templ_dirty &= ~BIT(fgr);
	}
	memcpy(out, reinterpret_cast<uint8_t *>(&fp_enc_buffer) + offset, size);
	args->response_size = size;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_FRAME, fp_command_frame, EC_VER_MASK(0));

static enum ec_status fp_command_stats(struct host_cmd_handler_args *args)
{
	auto *r = static_cast<struct ec_response_fp_stats *>(args->response);

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
	r->template_matched =
		global_context.positive_match_secret_state.template_matched;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_STATS, fp_command_stats, EC_VER_MASK(0));

static enum ec_status
validate_template_format(struct ec_fp_template_encryption_metadata *enc_info)
{
	if (enc_info->struct_version != FP_TEMPLATE_FORMAT_VERSION) {
		CPRINTS("Invalid template format %d", enc_info->struct_version);
		return EC_RES_INVALID_PARAM;
	}
	return EC_RES_SUCCESS;
}

enum ec_status fp_commit_template(std::span<const uint8_t> context)
{
	ScopedFastCpu fast_cpu;

	uint16_t idx = global_context.templ_valid;
	struct ec_fp_template_encryption_metadata *enc_info;

	/*
	 * The complete encrypted template has been received, start
	 * decryption.
	 */
	fp_clear_finger_context(idx);
	/*
	 * The beginning of the buffer contains nonce, encryption_salt
	 * and tag.
	 */
	enc_info = &fp_enc_buffer.metadata;
	enum ec_status res = validate_template_format(enc_info);
	if (res != EC_RES_SUCCESS) {
		CPRINTS("fgr%d: Template format not supported", idx);
		return EC_RES_INVALID_PARAM;
	}

	/* Encrypted template is after the metadata. */
	std::span templ = fp_enc_buffer.fp_template;
	/* Positive match salt is after the template. */
	std::span positive_match_salt = fp_enc_buffer.positive_match_salt;
	std::span encrypted_template_and_positive_match_salt(
		templ.data(),
		templ.size_bytes() + positive_match_salt.size_bytes());

	enum ec_error_list ret;
	if (global_context.fp_encryption_status & FP_CONTEXT_USER_ID_SET) {
		FpEncryptionKey key;
		ret = derive_encryption_key(key, enc_info->encryption_salt,
					    context, global_context.tpm_seed);
		if (ret != EC_SUCCESS) {
			CPRINTS("fgr%d: Failed to derive key", idx);
			return EC_RES_UNAVAILABLE;
		}

		/* Decrypt the secret blob in-place. */
		ret = aes_128_gcm_decrypt(
			key, encrypted_template_and_positive_match_salt,
			encrypted_template_and_positive_match_salt,
			enc_info->nonce, enc_info->tag);
		if (ret != EC_SUCCESS) {
			CPRINTS("fgr%d: Failed to decipher template", idx);
			/* Don't leave bad data in the template buffer
			 */
			fp_clear_finger_context(idx);
			return EC_RES_UNAVAILABLE;
		}
		fp_init_decrypted_template_state_with_user_id(idx);
	} else {
		global_context.template_states[idx] =
			fp_encrypted_template_state{
				.enc_metadata = *enc_info,
			};
	}

	std::ranges::copy(templ, fp_template[idx]);
	if (bytes_are_trivial(positive_match_salt.data(),
			      positive_match_salt.size_bytes())) {
		CPRINTS("fgr%d: Trivial positive match salt.", idx);
		OPENSSL_cleanse(fp_template[idx], sizeof(fp_template[0]));
		return EC_RES_INVALID_PARAM;
	}
	std::ranges::copy(positive_match_salt,
			  global_context.fp_positive_match_salt[idx]);

	global_context.templ_valid++;
	return EC_RES_SUCCESS;
}

static enum ec_status fp_command_template(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const struct ec_params_fp_template *>(args->params);
	uint32_t size = params->size & ~FP_TEMPLATE_COMMIT;
	bool xfer_complete = params->size & FP_TEMPLATE_COMMIT;
	uint32_t offset = params->offset;
	uint16_t idx = global_context.templ_valid;

	/* Can we store one more template ? */
	if (idx >= FP_MAX_FINGER_COUNT)
		return EC_RES_OVERFLOW;

	if (args->params_size !=
	    size + offsetof(struct ec_params_fp_template, data))
		return EC_RES_INVALID_PARAM;
	enum ec_error_list ret =
		validate_fp_buffer_offset(sizeof(fp_enc_buffer), offset, size);
	if (ret != EC_SUCCESS)
		return EC_RES_INVALID_PARAM;

	memcpy(reinterpret_cast<uint8_t *>(&fp_enc_buffer) + offset,
	       params->data, size);

	if (xfer_complete) {
		return fp_commit_template(global_context.user_id);
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_TEMPLATE, fp_command_template, EC_VER_MASK(0));

static enum ec_status
fp_command_migrate_template_to_nonce_context(struct host_cmd_handler_args *args)
{
	const auto *params = static_cast<
		const ec_params_fp_migrate_template_to_nonce_context *>(
		args->params);
	uint16_t idx = global_context.templ_valid;

	/*
	 * The command is used for migrating legacy templates to be encrypted by
	 * nonce sessions. No point to call this outside a nonce context.
	 */
	if (!(global_context.fp_encryption_status &
	      FP_CONTEXT_STATUS_NONCE_CONTEXT_SET)) {
		return EC_RES_ACCESS_DENIED;
	}

	/*
	 * Migration commits a template into the nonce session, so the whole
	 * temlpate needs to be uploaded through FP_TEMPLATE command first
	 * without committing. Check whether we have space for a new template.
	 */
	if (idx >= FP_MAX_FINGER_COUNT)
		return EC_RES_OVERFLOW;

	BUILD_ASSERT(sizeof(params->userid) == SHA256_DIGEST_SIZE);
	ec_status res = fp_commit_template(
		{ reinterpret_cast<const uint8_t *>(params->userid),
		  sizeof(params->userid) });
	if (res != EC_RES_SUCCESS) {
		return res;
	}

	ScopedFastCpu fast_cpu;

	/*
	 * Make sure salt data is cleared because the new protocol doesn't trust
	 * match secrets of legacy templates. New match secret needs to be
	 * generated for them.
	 */
	memset(global_context.fp_positive_match_salt[idx], 0,
	       FP_POSITIVE_MATCH_SALT_BYTES);
	int ret = fp_enable_positive_match_secret(
		idx, &global_context.positive_match_secret_state);
	if (ret != EC_SUCCESS) {
		return EC_RES_ACCESS_DENIED;
	}

	/*
	 * Note that this operation can be think of as making template idx
	 * (the one we just committed) a freshly enrolled template. It needs to
	 * be fetched again (and encrypted differently) and its match secret
	 * needs to be freshly generated.
	 */
	global_context.templ_dirty |= BIT(idx);
	global_context.template_newly_enrolled = idx;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_MIGRATE_TEMPLATE_TO_NONCE_CONTEXT,
		     fp_command_migrate_template_to_nonce_context,
		     EC_VER_MASK(0));
