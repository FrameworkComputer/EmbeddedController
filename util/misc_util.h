/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __UTIL_MISC_UTIL_H
#define __UTIL_MISC_UTIL_H

#include <stddef.h>
#include <stdint.h>

#define GENERIC_MAX(x, y) ((x) > (y) ? (x) : (y))
#define GENERIC_MIN(x, y) ((x) < (y) ? (x) : (y))
#ifndef MAX
#define MAX(a, b)                            \
	({                                   \
		__typeof__(a) temp_a = (a);  \
		__typeof__(b) temp_b = (b);  \
                                             \
		GENERIC_MAX(temp_a, temp_b); \
	})
#endif
#ifndef MIN
#define MIN(a, b)                            \
	({                                   \
		__typeof__(a) temp_a = (a);  \
		__typeof__(b) temp_b = (b);  \
                                             \
		GENERIC_MIN(temp_a, temp_b); \
	})
#endif

/**
 * Write a buffer to the file.
 *
 * @param filename	Target filename
 * @param buf		Buffer to write
 * @param size		Size of buffer in bytes
 * @return non-zero if error
 */
int write_file(const char *filename, const char *buf, int size);

/**
 * Read a file into a newly-allocated buffer.
 *
 * @param filename	Source filename
 * @param size		Size of data in bytes will be stored here on success.
 * @return A newly allocated buffer with the data, which must be freed with
 *         free() by the caller, or NULL if error.
 */
char *read_file(const char *filename, int *size);

/**
 * Check if a string contains only printable characters.
 *
 * @param buf		Null-terminated string to check
 * @return non-zero if buf contains only printable characters; zero if not.
 */
int is_string_printable(const char *buf);

/**
 * Get the versions of the command supported by the EC.
 *
 * @param cmd		Command
 * @param pmask		Destination for version mask; will be set to 0 on
 *			error.
 * @return 0 if success, <0 if error
 */
int ec_get_cmd_versions(int cmd, uint32_t *pmask);

/**
 * Return non-zero if the EC supports the command and version
 *
 * @param cmd		Command to check
 * @param ver		Version to check
 * @return non-zero if command version supported; 0 if not.
 */
int ec_cmd_version_supported(int cmd, int ver);

/**
 * @brief Gets the highest version number of a command supported by the EC
 *
 * @param cmd The command to query support for
 * @param ver Output parameter to write max version to.
 *
 * @return 0 on success
 * @return -EC_RES_INVALID_PARAM if ver is NULL
 * @return -EC_RES_INVALID_COMMAND if no version of this command is supported
 *         or if command does not exist.
 */
int ec_get_highest_supported_cmd_version(int cmd, int *ver);

/**
 * Return 1 is the current kernel version is greater or equal to
 * <major>.<minor>.<sublevel>
 */
int kernel_version_ge(int major, int minor, int sublevel);

/**
 * Prints data in hexdump canonical format.
 *
 * @param data Buffer of data to print
 * @param len Length of data to print
 * @param offset_start Starting offset added to the printed offset.
 *     This only affects how the offset is printed, it does not affect
 *     what data is printed.
 */
void hexdump_canonical(const uint8_t *data, size_t len, uint32_t offset_start);

#endif
