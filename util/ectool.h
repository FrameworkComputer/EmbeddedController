/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ectool_pdc.h"

/** @brief A handler for an `ectool` command.  */
struct command {
	/** The name of the command. */
	const char *name;

	/**
	 * The function to handle the command.
	 *
	 * @param argc The length of `argv`
	 * @param argv The arguments passed, including the command itself but
	 *             not 'ectool'.
	 * @return 0 if successful, or a negative `enum ec_status` value.
	 */
	int (*handler)(int argc, char *argv[]);

	/** The help text for this command, as
	 * "arguments\n\tShort description."
	 */
	const char *help;
};

/**
 * Test low-level key scanning
 *
 * ectool keyscan <beat_us> <filename>
 *
 * <beat_us> is the length of a beat in microseconds. This indicates the
 * typing speed. Typically we scan at 10ms in the EC, so the beat period
 * will typically be 1-5ms, with the scan changing only every 20-30ms at
 * most.
 * <filename> specifies a file containing keys that are depressed on each
 * beat in the following format:
 *
 *   <beat> <keys_pressed>
 *
 * <beat> is a beat number (0, 1, 2). The timestamp of this event will
 * be <start_time> + <beat> * <beat_us>.
 * <keys_pressed> is a (possibly empty) list of ASCII keys
 *
 * The key matrix is read from the fdt.
 */
int cmd_keyscan(int argc, char *argv[]);

/* ASCII mode for printing, default off */
extern int ascii_mode;

int cmd_i2c_protect(int argc, char *argv[]);
int cmd_i2c_read(int argc, char *argv[]);
int cmd_i2c_speed(int argc, char *argv[]);
int cmd_i2c_write(int argc, char *argv[]);
int cmd_i2c_xfer(int argc, char *argv[]);
