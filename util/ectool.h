/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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
 *
 * @param argc	Number of arguments (excluding 'ectool')
 * @param argv	List of arguments
 * @return 0 if ok, -1 on error
 */
int cmd_keyscan(int argc, char *argv[]);
