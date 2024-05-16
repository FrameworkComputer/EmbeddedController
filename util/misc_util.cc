/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "comm-host.h"
#include "misc_util.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/utsname.h>

int write_file(const char *filename, const char *buf, int size)
{
	FILE *f;
	int i;

	/* Write to file */
	f = fopen(filename, "wb");
	if (!f) {
		perror("Error opening output file");
		return -1;
	}
	i = fwrite(buf, 1, size, f);
	fclose(f);
	if (i != size) {
		perror("Error writing to file");
		return -1;
	}

	return 0;
}

char *read_file(const char *filename, int *size)
{
	FILE *f = fopen(filename, "rb");
	char *buf;
	int i;

	if (!f) {
		perror("Error opening input file");
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	rewind(f);
	if ((*size > 0x100000) || (*size < 0)) {
		if (*size < 0)
			perror("ftell failed");
		else
			fprintf(stderr, "File seems unreasonably large\n");
		fclose(f);
		return NULL;
	}

	buf = (char *)malloc(*size);
	if (!buf) {
		fprintf(stderr, "Unable to allocate buffer.\n");
		fclose(f);
		return NULL;
	}

	printf("Reading %d bytes from %s...\n", *size, filename);
	i = fread(buf, 1, *size, f);
	fclose(f);
	if (i != *size) {
		perror("Error reading file");
		free(buf);
		return NULL;
	}

	return buf;
}

int is_string_printable(const char *buf)
{
	while (*buf) {
		if (!isprint(*buf))
			return 0;
		buf++;
	}

	return 1;
}

/**
 * Get the versions of the command supported by the EC.
 *
 * @param cmd		Command
 * @param pmask		Destination for version mask; will be set to 0 on
 *			error.
 * @return 0 if success, <0 if error
 */
int ec_get_cmd_versions(int cmd, uint32_t *pmask)
{
	struct ec_params_get_cmd_versions_v1 pver_v1;
	struct ec_params_get_cmd_versions pver;
	struct ec_response_get_cmd_versions rver;
	int rv;

	*pmask = 0;

	pver_v1.cmd = cmd;
	rv = ec_command(EC_CMD_GET_CMD_VERSIONS, 1, &pver_v1, sizeof(pver_v1),
			&rver, sizeof(rver));

	if (rv < 0) {
		pver.cmd = cmd;
		rv = ec_command(EC_CMD_GET_CMD_VERSIONS, 0, &pver, sizeof(pver),
				&rver, sizeof(rver));
	}

	if (rv < 0)
		return rv;

	*pmask = rver.version_mask;
	return 0;
}

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
int ec_get_highest_supported_cmd_version(int cmd, int *ver)
{
	int rv;
	uint32_t mask = 0;

	if (!ver) {
		return -EC_RES_INVALID_PARAM;
	}

	rv = ec_get_cmd_versions(cmd, &mask);
	if (rv < 0) {
		return rv;
	}

	if (mask == 0) {
		/* No version of this command is supported */
		return -EC_RES_INVALID_COMMAND;
	}

	*ver = 31 - __builtin_clz(mask);

	return EC_RES_SUCCESS;
}

/**
 * Return non-zero if the EC supports the command and version
 *
 * @param cmd		Command to check
 * @param ver		Version to check
 * @return non-zero if command version supported; 0 if not.
 */
int ec_cmd_version_supported(int cmd, int ver)
{
	uint32_t mask = 0;

	if (ec_get_cmd_versions(cmd, &mask))
		return 0;

	return (mask & EC_VER_MASK(ver)) ? 1 : 0;
}

/**
 * Return 1 is the current kernel version is greater or equal to
 * <major>.<minor>.<sublevel>
 */
int kernel_version_ge(int major, int minor, int sublevel)
{
	struct utsname uts;
	int atoms, kmajor, kminor, ksublevel;

	if (uname(&uts) < 0)
		return -1;
	atoms = sscanf(uts.release, "%d.%d.%d", &kmajor, &kminor, &ksublevel);
	if (atoms < 1)
		return -1;

	if (kmajor > major)
		return 1;
	if (kmajor < major)
		return 0;

	/* kmajor == major */
	if (atoms < 2)
		return 0 == minor && 0 == sublevel;
	if (kminor > minor)
		return 1;
	if (kminor < minor)
		return 0;

	/* kminor == minor */
	if (atoms < 3)
		return 0 == sublevel;

	return ksublevel >= sublevel;
}

/**
 * Prints data in hexdump canonical format.
 *
 * @param data Buffer of data to print
 * @param len Length of data to print
 * @param offset_start Starting offset added to the displayed offset.
 *        This only affects how the offset is printed, it does not affect
 *        what data is printed.
 */
void hexdump_canonical(const uint8_t *data, size_t len, uint32_t offset_start)
{
	uint32_t i, j;

	if (!data || !len)
		return;

	for (i = 0; i < len; i += 16) {
		printf("%08x  ", i + offset_start);
		for (j = i; j < i + 16; j++) {
			if (j == i + 8)
				printf(" ");
			if (j < len)
				printf("%02x ", data[j]);
			else
				printf("   ");
		}
		printf(" |");
		for (j = i; j < i + 16 && j < len; j++)
			if (isprint(data[j]))
				printf("%c", data[j]);
			else
				printf(".");
		printf("|\n");
	}
	printf("%08x\n", i + offset_start);
}
