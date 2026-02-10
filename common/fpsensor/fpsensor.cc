/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "assert.h"
#include "atomic_bit.h"
#include "clock.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "crypto/cleanse_wrapper.h"
#include "ec_commands.h"
#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_auth_commands.h"
#include "fpsensor/fpsensor_console.h"
#include "fpsensor/fpsensor_crypto.h"
#include "fpsensor/fpsensor_detect.h"
#include "fpsensor/fpsensor_modes.h"
#include "fpsensor/fpsensor_state.h"
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

#ifdef HAVE_FP_PRIVATE_DRIVER

/*
 * contains the bit FP_MODE_ENROLL_SESSION if a finger enrollment is on-going.
 * It is used to detect the ENROLL_SESSION transition when sensor_mode is
 * updated by the host.
 */
static uint32_t enroll_session;

static uint32_t fp_process_enroll(void)
{
	int percent = 0;

	if (global_context.template_newly_enrolled != FP_NO_SUCH_TEMPLATE)
		CPRINTS("Warning: previously enrolled template has not been "
			"read yet.");

	/* begin/continue enrollment */
	CPRINTS("[%d]Enrolling ...", global_context.templ_valid);
	int res = fp_finger_enroll(fp_buffer, &percent);
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

			/*
			 * In the classic flow we immediately use this template
			 * for matching (templ_valid is incremented).
			 *
			 * In the Fingerprint Auth flow we must reliably inform
			 * Trusted Application that the template was enrolled.
			 * To achieve this, we will not use this template for
			 * matching, until Trusted Application sends us a signed
			 * confirmation.
			 */
			if (!fingerprint_auth_enabled()) {
				global_context.templ_valid++;
			}
		}
		global_context.sensor_mode &= ~FP_MODE_ENROLL_SESSION;
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
	fp_disable_positive_match_secret(
		&global_context.positive_match_secret_state);

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
	enum fp_capture_type capture_type =
		FP_CAPTURE_TYPE(global_context.sensor_mode);
	global_context.current_capture_type = FP_CAPTURE_TYPE_INVALID;

	CPRINTS("Capturing ...");
	int res = fp_acquire_image(fp_buffer, capture_type);
	capture_time_us = time_since32(t0);
	if (res) {
		timestamps_invalid |= FPSTATS_CAPTURE_INV;
		return;
	}

	global_context.current_capture_type = capture_type;
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

	global_context.sensor_mode &=
		~(FP_MODE_ANY_CAPTURE | FP_MODE_CAPTURE_TYPE_MASK);
	overall_time_us = time_since32(overall_t0);
	send_mkbp_event(evt);
}
#endif /* HAVE_FP_PRIVATE_DRIVER */

static enum ec_error_list encrypt_template(uint16_t fgr);
static enum ec_status fp_commit_template(std::span<const uint8_t> context);

extern "C" void fp_task(void)
{
	int timeout_us = -1;

	CPRINTS("FP_SENSOR_SEL: %s",
		fp_sensor_type_to_str(fpsensor_detect_get_type()));

#ifdef HAVE_FP_PRIVATE_DRIVER
	/* Reset and initialize the sensor IC */
	fp_sensor_init();

	global_context.fp_frame_size_cache.populate_cache(sizeof(fp_buffer));

	while (1) {
		enum finger_state st = FINGER_NONE;

		/* Wait for a sensor IRQ or a new mode configuration */
		uint32_t evt = task_wait_event(timeout_us);

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
				int ret = 0;
				if (mode & FP_MODE_ENROLL_SESSION) {
					ret = fp_enrollment_begin();
					if (ret)
						global_context.sensor_mode &=
							~FP_MODE_ENROLL_SESSION;
				} else {
					fp_enrollment_finish(nullptr);
				}
				enroll_session =
					ret ? 0 :
					      (mode & FP_MODE_ENROLL_SESSION);
			}
			if (!is_finger_needed(mode)) {
				enum fp_capture_type capture_type =
					FP_CAPTURE_TYPE(mode);
				global_context.current_capture_type =
					FP_CAPTURE_TYPE_INVALID;
				if (!fp_acquire_image(fp_buffer,
						      capture_type)) {
					global_context.current_capture_type =
						capture_type;
				}
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
			} else if (mode & FP_MODE_ENCRYPT_TEMPLATE) {
				global_context.fp_encryption_status &=
					~FP_ENCRYPTED_TEMPLATE_READY;
				if (global_context.template_encrypted_id !=
				    FP_NO_SUCH_TEMPLATE) {
					ScopedFastCpu fast_cpu;

					if (encrypt_template(
						    global_context
							    .template_encrypted_id) ==
					    EC_SUCCESS) {
						global_context
							.fp_encryption_status |=
							FP_ENCRYPTED_TEMPLATE_READY;
					}
				}
				global_context.sensor_mode &=
					~FP_MODE_ENCRYPT_TEMPLATE;
			} else if (mode & FP_MODE_DECRYPT_TEMPLATE) {
				ScopedFastCpu fast_cpu;

				enum ec_status res = fp_commit_template(
					global_context.user_id);
				atomic_set(&global_context
						    .template_decryption_result,
					   res);
				global_context.sensor_mode &=
					~FP_MODE_DECRYPT_TEMPLATE;
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
	struct ec_response_fp_info_v2 *r =
		static_cast<ec_response_fp_info_v2 *>(args->response);
	const size_t response_size =
		sizeof(struct ec_response_fp_info_v2) +
		FP_MAX_CAPTURE_TYPES * sizeof(struct fp_image_frame_params);

	if (response_size > args->response_max) {
		return EC_RES_OVERFLOW;
	}

	/*
	 * The sensor may have fewer than FP_MAX_CAPTURE_TYPES number of
	 * captures, in which case the buffer will only be partially written. We
	 * want to make sure the rest of the buffer does not leak any data.
	 */
	memset(r, 0, response_size);

#ifdef HAVE_FP_PRIVATE_DRIVER
	if (fp_sensor_get_info(r, response_size) < 0)
#endif
		return EC_RES_UNAVAILABLE;

	r->template_info.template_size = FP_ALGORITHM_ENCRYPTED_TEMPLATE_SIZE;
	r->template_info.template_max = FP_MAX_FINGER_COUNT;
	r->template_info.template_valid = global_context.templ_valid;
	r->template_info.template_dirty = global_context.templ_dirty;
	r->template_info.template_version = FP_TEMPLATE_FORMAT_VERSION;

	args->response_size = response_size;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_INFO, fp_command_info, EC_VER_MASK(2));

BUILD_ASSERT(FP_CONTEXT_NONCE_BYTES == 12);

static enum ec_error_list encrypt_template(uint16_t fgr)
{
	enum ec_error_list ret;

	/* Encrypted template is after the metadata. */
	std::span templ = fp_enc_buffer.fp_template;
	/* Positive match salt is after the template. */
	std::span positive_match_salt = fp_enc_buffer.positive_match_salt;
	std::span encrypted_template_and_positive_match_salt(
		templ.data(),
		templ.size_bytes() + positive_match_salt.size_bytes());

	fp_enc_buffer = {};

	if (fgr >= FP_MAX_FINGER_COUNT)
		return EC_ERROR_INVAL;

	if (fgr >= global_context.templ_valid)
		return EC_ERROR_UNAVAILABLE;

	/*
	 * The beginning of the buffer contains nonce, encryption_salt
	 * and tag.
	 */
	struct ec_fp_template_encryption_metadata *enc_info =
		&fp_enc_buffer.metadata;
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
		global_context.template_newly_enrolled = FP_NO_SUCH_TEMPLATE;
		trng_init();
		trng_rand_bytes(global_context.fp_positive_match_salt[fgr],
				FP_POSITIVE_MATCH_SALT_BYTES);
		trng_exit();
	}

	FpEncryptionKey key;
	ret = derive_encryption_key(key, enc_info->encryption_salt,
				    global_context.user_id,
				    global_context.tpm_seed);
	if (ret != EC_SUCCESS) {
		CPRINTS("fgr%d: Failed to derive key", fgr);
		return EC_ERROR_UNAVAILABLE;
	}

	/*
	 * Copy the payload to |fp_enc_buffer| where it will be
	 * encrypted in-place.
	 */
	std::ranges::copy(fp_template[fgr], templ.begin());
	std::ranges::copy(global_context.fp_positive_match_salt[fgr],
			  positive_match_salt.begin());

	/* Encrypt the secret blob in-place. */
	ret = aes_128_gcm_encrypt(key,
				  encrypted_template_and_positive_match_salt,
				  encrypted_template_and_positive_match_salt,
				  enc_info->nonce, enc_info->tag);
	if (ret != EC_SUCCESS) {
		OPENSSL_cleanse(&fp_enc_buffer, sizeof(fp_enc_buffer));
		CPRINTS("fgr%d: Failed to encrypt template", fgr);
		return EC_ERROR_UNAVAILABLE;
	}

	global_context.templ_dirty &= ~BIT(fgr);

	return EC_SUCCESS;
}

static enum ec_status get_frame(uint32_t offset, uint32_t size, uint8_t *output)
{
	enum ec_error_list ret;

	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	if (global_context.current_capture_type == FP_CAPTURE_TYPE_INVALID) {
		return EC_RES_INVALID_PARAM;
	}

	/*
	 * Checks if the capture type is one where we only care about
	 * the embedded/offset image bytes, like simple, pattern0,
	 * pattern1, and reset_test.
	 */
	if (skip_image_offset(global_context.current_capture_type))
		offset += FP_SENSOR_IMAGE_OFFSET;

	uint32_t current_frame_size =
		global_context.fp_frame_size_cache.get_frame_size(
			global_context.current_capture_type);

	if (current_frame_size > sizeof(fp_buffer)) {
		return EC_RES_INVALID_PARAM;
	}

	ret = validate_fp_buffer_offset(current_frame_size, offset, size);
	if (ret != EC_SUCCESS)
		return EC_RES_INVALID_PARAM;

	memcpy(output, fp_buffer + offset, size);

	return EC_RES_SUCCESS;
}

/* TODO(b/471160577): Remove FP_FRAME v0 after migration is completed. */
static enum ec_status fp_command_frame_v0(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const struct ec_params_fp_frame *>(args->params);
	void *out = args->response;
	uint16_t idx = FP_FRAME_GET_BUFFER_INDEX(params->offset);
	uint32_t offset = params->offset & FP_FRAME_OFFSET_MASK;
	uint32_t size = params->size;
	enum ec_error_list ret;

	if (size > args->response_max)
		return EC_RES_INVALID_PARAM;

	if (idx == FP_FRAME_INDEX_RAW_IMAGE) {
		/* The host requested a frame. */
		enum ec_status ret = get_frame(offset, size, (uint8_t *)out);
		if (ret != EC_RES_SUCCESS) {
			return ret;
		}

		args->response_size = size;
		return EC_RES_SUCCESS;
	}

	/* The host requested a template. */

	/* Encryption or decryption is in progress. */
	if (global_context.sensor_mode & FP_MODES_CRYPTO_IN_PROGRESS) {
		return EC_RES_BUSY;
	}

	/* Templates are numbered from 1 in this host request. */
	uint16_t fgr = idx - FP_FRAME_INDEX_TEMPLATE;

	if (fgr >= FP_MAX_FINGER_COUNT)
		return EC_RES_INVALID_PARAM;
	if (fgr >= global_context.templ_valid)
		return EC_RES_UNAVAILABLE;
	ret = validate_fp_buffer_offset(sizeof(fp_enc_buffer), offset, size);
	if (ret != EC_SUCCESS)
		return EC_RES_INVALID_PARAM;

	if (!offset) {
		/* Host has requested the first chunk, do the encryption. */
		timestamp_t now = get_time();

		/* b/114160734: Not more than 1 encrypted message per second. */
		if (!timestamp_expired(encryption_deadline, &now))
			return EC_RES_BUSY;
		encryption_deadline.val = now.val + (1 * SECOND);

		ScopedFastCpu fast_cpu;

		if (encrypt_template(fgr) != EC_SUCCESS) {
			return EC_RES_UNAVAILABLE;
		}
	}
	memcpy(out, reinterpret_cast<uint8_t *>(&fp_enc_buffer) + offset, size);
	args->response_size = size;

	return EC_RES_SUCCESS;
}

/*
 * We are modifying variables in the global_context from this function running
 * in the HOSTCMD task and encrypt_template() running in the FPSENSOR task,
 * without protecting these data.
 *
 * This works because:
 * - We use single-core MCUs
 * - HOSTCMD task has higher priority than FPSENSOR task.
 * - There could be only one host command running at the time.
 * - Host commands cannot be interrupted by other tasks.
 * - We are checking only one bit in global_context.sensor_mode,
 *   so we are not prone to partial (non-atomic) writes/reads.
 *
 * TODO(b/479824917): Unfortunately, lack of any synchronization is a general
 * problem with FPSENSOR architecture. All fingerprint related work should be
 * done by FPSENSOR task, HOSTCMD role should be limited to validating input
 * (if possible), posting work to FPSENSOR task and waiting for result, if
 * needed.
 */
static enum ec_status fp_command_frame_v1(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const struct ec_params_fp_frame_v1 *>(args->params);
	void *out = args->response;
	uint32_t offset = params->offset;
	uint32_t size = params->size;
	enum ec_error_list ret;
	enum ec_status status;

	if (size > args->response_max)
		return EC_RES_INVALID_PARAM;

	switch (params->cmd) {
	case FP_FRAME_GET_RAW_IMAGE:
		/* The host requested a frame. */
		status = get_frame(offset, size, (uint8_t *)out);
		if (status != EC_RES_SUCCESS) {
			return status;
		}

		args->response_size = size;
		return EC_RES_SUCCESS;
	case FP_FRAME_ENCRYPT_TEMPLATE: {
		timestamp_t now;
		uint32_t mode_output;

		/*
		 * Do not change the content of fp_enc_buffer if the encryption
		 * or decryption is in progress.
		 */
		if (global_context.sensor_mode & FP_MODES_CRYPTO_IN_PROGRESS) {
			return EC_RES_BUSY;
		}

		if (params->index >= FP_MAX_FINGER_COUNT)
			return EC_RES_INVALID_PARAM;
		if (params->index >= global_context.templ_valid)
			return EC_RES_UNAVAILABLE;

		now = get_time();

		/* b/114160734: Not more than 1 encrypted message per second. */
		if (!timestamp_expired(encryption_deadline, &now))
			return EC_RES_BUSY;
		encryption_deadline.val = now.val + (1 * SECOND);

		global_context.fp_encryption_status &=
			~FP_ENCRYPTED_TEMPLATE_READY;
		global_context.template_encrypted_id = params->index;
		status = fp_set_sensor_mode(FP_MODE_ENCRYPT_TEMPLATE,
					    &mode_output, std::nullopt);
		if (status != EC_RES_SUCCESS) {
			return EC_RES_ERROR;
		}
		break;
	}
	case FP_FRAME_GET_ENCRYPTED_TEMPLATE:
		/* Encryption or decryption is still running */
		if (global_context.sensor_mode & FP_MODES_CRYPTO_IN_PROGRESS) {
			return EC_RES_BUSY;
		}

		/*
		 * Encrypted template not available (or encryption finished
		 * with error)
		 */
		if (!(global_context.fp_encryption_status &
		      FP_ENCRYPTED_TEMPLATE_READY)) {
			return EC_RES_UNAVAILABLE;
		}

		/* Validate data request */
		ret = validate_fp_buffer_offset(sizeof(fp_enc_buffer), offset,
						size);
		if (ret != EC_SUCCESS)
			return EC_RES_INVALID_PARAM;

		/* Encryption succeeded */
		memcpy(out,
		       reinterpret_cast<uint8_t *>(&fp_enc_buffer) + offset,
		       size);
		args->response_size = size;

		break;
	}

	return EC_RES_SUCCESS;
}

static enum ec_status fp_command_frame(struct host_cmd_handler_args *args)
{
	if (args->version == 1) {
		return fp_command_frame_v1(args);
	}

	return fp_command_frame_v0(args);
}
DECLARE_HOST_COMMAND(EC_CMD_FP_FRAME, fp_command_frame,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

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

static enum ec_status fp_commit_template(std::span<const uint8_t> context)
{
	ScopedFastCpu fast_cpu;

	uint16_t idx = global_context.templ_valid;

	if (idx >= FP_MAX_FINGER_COUNT)
		return EC_RES_OVERFLOW;

	/*
	 * The complete encrypted template has been received, start
	 * decryption.
	 */
	fp_clear_finger_context(idx);
	/*
	 * The beginning of the buffer contains nonce, encryption_salt
	 * and tag.
	 */
	struct ec_fp_template_encryption_metadata *enc_info =
		&fp_enc_buffer.metadata;
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

	FpEncryptionKey key;
	enum ec_error_list ret =
		derive_encryption_key(key, enc_info->encryption_salt, context,
				      global_context.tpm_seed);
	if (ret != EC_SUCCESS) {
		CPRINTS("fgr%d: Failed to derive key", idx);
		return EC_RES_UNAVAILABLE;
	}

	/* Decrypt the secret blob in-place. */
	ret = aes_128_gcm_decrypt(key,
				  encrypted_template_and_positive_match_salt,
				  encrypted_template_and_positive_match_salt,
				  enc_info->nonce, enc_info->tag);
	if (ret != EC_SUCCESS) {
		/* Don't leave partially decrypted data in the buffer! */
		OPENSSL_cleanse(&fp_enc_buffer, sizeof(fp_enc_buffer));
		CPRINTS("fgr%d: Failed to decipher template", idx);
		return EC_RES_UNAVAILABLE;
	}

	if (bytes_are_trivial(positive_match_salt.data(),
			      positive_match_salt.size_bytes())) {
		/* Don't leave decrypted data in the buffer! */
		OPENSSL_cleanse(&fp_enc_buffer, sizeof(fp_enc_buffer));
		CPRINTS("fgr%d: Trivial positive match salt.", idx);
		return EC_RES_INVALID_PARAM;
	}

	std::ranges::copy(templ, fp_template[idx]);
	std::ranges::copy(positive_match_salt,
			  global_context.fp_positive_match_salt[idx]);

	/* Don't leave decrypted data in the buffer! */
	OPENSSL_cleanse(&fp_enc_buffer, sizeof(fp_enc_buffer));

	global_context.templ_valid++;
	return EC_RES_SUCCESS;
}

static enum ec_status fp_command_template_v0(struct host_cmd_handler_args *args)
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

	/* Encryption or decryption is in progress. */
	if (global_context.sensor_mode & FP_MODES_CRYPTO_IN_PROGRESS) {
		return EC_RES_BUSY;
	}

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

static enum ec_status fp_command_template_v1(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const struct ec_params_fp_template_v1 *>(
			args->params);
	enum ec_status status;

	switch (params->cmd) {
	case FP_TEMPLATE_LOAD: {
		uint32_t size = params->size;
		uint32_t offset = params->offset;

		/* Encryption or decryption is in progress. */
		if (global_context.sensor_mode & FP_MODES_CRYPTO_IN_PROGRESS) {
			return EC_RES_BUSY;
		}

		/* Can we store one more template ? */
		if (global_context.templ_valid >= FP_MAX_FINGER_COUNT) {
			return EC_RES_OVERFLOW;
		}

		if (args->params_size !=
		    size + offsetof(struct ec_params_fp_template_v1, data)) {
			return EC_RES_INVALID_PARAM;
		}
		enum ec_error_list ret = validate_fp_buffer_offset(
			sizeof(fp_enc_buffer), offset, size);
		if (ret != EC_SUCCESS) {
			return EC_RES_INVALID_PARAM;
		}

		/*
		 * We are going to modify 'fp_enc_buffer', so clear
		 * FP_ENCRYPTED_TEMPLATE_READY bit and 'template_encrypted_id'.
		 */
		global_context.fp_encryption_status &=
			~FP_ENCRYPTED_TEMPLATE_READY;
		global_context.template_encrypted_id = FP_NO_SUCH_TEMPLATE;

		/* Copy part of the template to buffer. */
		memcpy(reinterpret_cast<uint8_t *>(&fp_enc_buffer) + offset,
		       params->data, size);

		return EC_RES_SUCCESS;
	}
	case FP_TEMPLATE_DECRYPT: {
		uint32_t mode_output;

		/* Encryption or decryption is in progress. */
		if (global_context.sensor_mode & FP_MODES_CRYPTO_IN_PROGRESS) {
			return EC_RES_BUSY;
		}

		/* Start template decryption. */
		status = fp_set_sensor_mode(FP_MODE_DECRYPT_TEMPLATE,
					    &mode_output, std::nullopt);
		if (status != EC_RES_SUCCESS) {
			atomic_set(&global_context.template_decryption_result,
				   EC_RES_ERROR);
			return EC_RES_ERROR;
		}

		atomic_set(&global_context.template_decryption_result,
			   EC_RES_BUSY);

		return EC_RES_SUCCESS;
	}
	case FP_TEMPLATE_GET_RESULT:
		/* Decryption is still running */
		if (global_context.sensor_mode & FP_MODE_DECRYPT_TEMPLATE) {
			return EC_RES_BUSY;
		}

		status = (enum ec_status)atomic_get(
			&global_context.template_decryption_result);

		return status;
	}

	return EC_RES_INVALID_PARAM;
}

static enum ec_status fp_command_template(struct host_cmd_handler_args *args)
{
	if (args->version == 1) {
		return fp_command_template_v1(args);
	}

	return fp_command_template_v0(args);
}
DECLARE_HOST_COMMAND(EC_CMD_FP_TEMPLATE, fp_command_template,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

static enum ec_status
fp_command_confirm_template(struct host_cmd_handler_args *args)
{
	const auto *params =
		static_cast<const struct ec_params_fp_confirm_template *>(
			args->params);
	std::span<const uint8_t, FP_MAC_LENGTH> mac{ params->mac };

	/*
	 * The context for signing/verifying messages is Android user id
	 * which is 4 byte integer.
	 */
	static_assert(global_context.user_id.size() >= sizeof(uint32_t));
	std::span<const uint8_t> context{ global_context.user_id.data(),
					  sizeof(uint32_t) };

	/* The operation is just an "enroll_finish" string */
	static constexpr uint8_t operation_str[] = { 'e', 'n', 'r', 'o', 'l',
						     'l', '_', 'f', 'i', 'n',
						     'i', 's', 'h' };
	std::span<const uint8_t> operation{ operation_str };

	/*
	 * After successful enrollment, the 'template_newly_enrolled' (new
	 * template id) must be equal to 'templ_valid'.
	 */
	if (global_context.template_newly_enrolled !=
	    global_context.templ_valid) {
		return EC_RES_ERROR;
	}

	if (validate_request(context, operation, mac) != EC_SUCCESS) {
		return EC_RES_ACCESS_DENIED;
	}

	/* Add newly enrolled template to valid templates. */
	global_context.templ_valid++;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_CONFIRM_TEMPLATE, fp_command_confirm_template,
		     EC_VER_MASK(0));
