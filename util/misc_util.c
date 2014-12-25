/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm-host.h"
#include "misc_util.h"

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
	if (*size > 0x100000) {
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

	if (rv == -EECRESULT - EC_RES_INVALID_VERSION) {
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
