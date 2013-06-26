/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_MISC_UTIL_H
#define __CROS_EC_MISC_UTIL_H

/* Don't use a macro where an inline will do... */
static inline int MIN(int a, int b) { return a < b ? a : b; }

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

#endif
