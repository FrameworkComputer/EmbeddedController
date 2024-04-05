/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "compile_time_macros.h"

#ifdef CONFIG_ZEPHYR
#include <zephyr/shell/shell.h>
#endif

extern "C" {
#include "atomic.h"
#include "clock.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
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

#include "fpsensor/fpsensor.h"
#include "fpsensor/fpsensor_crypto.h"
#include "fpsensor/fpsensor_detect.h"
#include "fpsensor/fpsensor_state.h"
#include "fpsensor/fpsensor_utils.h"

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
	crec_msleep(2000); /* let the download program start */
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
		crec_usleep(100 * MSEC);
	}
	return EC_ERROR_TIMEOUT;
}

static int command_fpcapture(int argc, const char **argv)
{
	int capture_type = FP_CAPTURE_SIMPLE_IMAGE;
	uint32_t mode;
	enum ec_error_list rc;

#ifdef CONFIG_ZEPHYR
	if (system_is_locked())
		return EC_ERROR_ACCESS_DENIED;
#endif

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

/* Transfer a chunk of the image from the host to the FPMCU
 *
 * Command format:
 *  fpupload <offset> <hex encoded pixel string>
 *
 * To limit the size of the commands, only a chunk of the image is sent for
 * each command invocation.
 */
static int command_fpupload(int argc, const char **argv)
{
	const char *pixels_str;
	uint8_t *dest;
	int offset;

	if (argc != 3)
		return EC_ERROR_PARAM1;
	if (system_is_locked())
		return EC_ERROR_ACCESS_DENIED;
	offset = atoi(argv[1]);
	if (offset < 0)
		return EC_ERROR_PARAM1;
	dest = fp_buffer + FP_SENSOR_IMAGE_OFFSET + offset;

	pixels_str = argv[2];
	while (*pixels_str) {
		if (dest >= fp_buffer + FP_SENSOR_IMAGE_SIZE)
			return EC_ERROR_PARAM1;
		char hex_str[] = { pixels_str[0], pixels_str[1], '\0' };
		*dest = static_cast<uint8_t>(strtol(hex_str, NULL, 16));
		pixels_str += 2;
		++dest;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fpupload, command_fpupload, NULL,
			"Copy fp image onto fpmcu fpsensor buffer");

/* Transfer an image from the FPMCU to the host
 *
 * Command format:
 *  fpdownload
 *
 * This is useful to verify the data was transferred correctly. Note that it
 * requires the terminal to be configured as explained in the comment above
 * upload_pgm_image().
 */
static int command_fpdownload(int argc, const char **argv)
{
	if (system_is_locked())
		return EC_ERROR_ACCESS_DENIED;

	upload_pgm_image(fp_buffer + FP_SENSOR_IMAGE_OFFSET);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fpdownload, command_fpdownload, NULL,
			"Copy fp image from fpmcu fpsensor buffer");

static int command_fpenroll(int argc, const char **argv)
{
	enum ec_error_list rc;
	int percent = 0;
	uint32_t event;
	static const char *const enroll_str[] = { "OK", "Low Quality",
						  "Immobile", "Low Coverage" };

#ifdef CONFIG_ZEPHYR
	if (system_is_locked())
		return EC_ERROR_ACCESS_DENIED;
#endif

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
			crec_usleep(20 * MSEC);
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

	if (rc != EC_SUCCESS)
		CPRINTS("Failed to clear fingerprint context: %d", rc);

	atomic_clear(&fp_events);

	return rc;
}
DECLARE_CONSOLE_COMMAND(fpclear, command_fpclear, NULL,
			"Clear fingerprint sensor context");

static int command_fpmaintenance(int argc, const char **argv)
{
#ifdef HAVE_FP_PRIVATE_DRIVER
	uint32_t mode_output = 0;
	int rc = 0;

	rc = fp_set_sensor_mode(FP_MODE_SENSOR_MAINTENANCE, &mode_output);

	if (rc != EC_RES_SUCCESS) {
		/*
		 * EC host command errors do not directly map to console command
		 * errors.
		 */
		return EC_ERROR_UNKNOWN;
	}

	/* Block console until maintenance is finished. */
	while (sensor_mode & FP_MODE_SENSOR_MAINTENANCE) {
		crec_usleep(100 * MSEC);
	}
#endif /* #ifdef HAVE_FP_PRIVATE_DRIVER */

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fpmaintenance, command_fpmaintenance, NULL,
			"Run fingerprint sensor maintenance");

#endif /* CONFIG_CMD_FPSENSOR_DEBUG */
