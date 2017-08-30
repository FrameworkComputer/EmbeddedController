/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <openssl/sha.h>

#include "config.h"

static void print_hex(FILE *out, uint8_t *digest, int len, int last)
{
	int i;

	fputs("{ ", out);
	for (i = 0; i < len; i++)
		fprintf(out, "0x%02x, ", digest[i]);

	fprintf(out, "}%c\n", last ? ';' : ',');
}

/* Output blank hashes */
static int hash_fw_blank(FILE *hashes)
{
	uint8_t digest[SHA256_DIGEST_LENGTH] = { 0 };
	int len;

	fprintf(hashes, "const uint8_t touchpad_fw_hashes[%d][%d] = {\n",
		CONFIG_TOUCHPAD_VIRTUAL_SIZE / CONFIG_UPDATE_PDU_SIZE,
		SHA256_DIGEST_LENGTH);
	for (len = 0; len < CONFIG_TOUCHPAD_VIRTUAL_SIZE;
				len += CONFIG_UPDATE_PDU_SIZE) {
		print_hex(hashes, digest, sizeof(digest), 0);
	}
	fputs("};\n", hashes);

	fprintf(hashes, "const uint8_t touchpad_fw_full_hash[%d] =\n\t",
		SHA256_DIGEST_LENGTH);
	print_hex(hashes, digest, SHA256_DIGEST_LENGTH, 1);

	return 0;
}

static int hash_fw(FILE *tp_fw, FILE *hashes)
{
	uint8_t buffer[CONFIG_UPDATE_PDU_SIZE];
	int len = 0;
	int rb;
	SHA256_CTX ctx;
	SHA256_CTX ctx_all;
	uint8_t digest[SHA256_DIGEST_LENGTH];

	SHA256_Init(&ctx_all);
	fprintf(hashes, "const uint8_t touchpad_fw_hashes[%d][%d] = {\n",
		CONFIG_TOUCHPAD_VIRTUAL_SIZE / CONFIG_UPDATE_PDU_SIZE,
		SHA256_DIGEST_LENGTH);
	while (1) {
		rb = fread(buffer, 1, sizeof(buffer), tp_fw);
		len += rb;

		if (rb == 0)
			break;

		/* Calculate hash for the block. */
		SHA256_Init(&ctx);
		SHA256_Update(&ctx, buffer, rb);
		SHA256_Final(digest, &ctx);

		SHA256_Update(&ctx_all, buffer, rb);

		print_hex(hashes, digest, sizeof(digest), 0);

		if (rb < sizeof(buffer))
			break;
	}
	fputs("};\n", hashes);

	SHA256_Final(digest, &ctx_all);
	fprintf(hashes, "const uint8_t touchpad_fw_full_hash[%d] =\n\t",
		SHA256_DIGEST_LENGTH);
	print_hex(hashes, digest, SHA256_DIGEST_LENGTH, 1);

	if (!feof(tp_fw) || ferror(tp_fw)) {
		warn("Error reading input file");
		return 1;
	}

	if (len != CONFIG_TOUCHPAD_VIRTUAL_SIZE) {
		warnx("Incorrect TP FW size (%d vs %d)", len,
			CONFIG_TOUCHPAD_VIRTUAL_SIZE);
		return 1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	int nopt;
	int ret;
	const char *out = NULL;
	char *tp_fw_name = NULL;
	FILE *tp_fw = NULL;
	FILE *hashes;
	const char short_opt[] = "f:ho:";
	const struct option long_opts[] = {
		{ "firmware", 1, NULL, 'f' },
		{ "help", 0, NULL, 'h' },
		{ "out", 1, NULL, 'o' },
		{ NULL }
	};
	const char usage[] = "USAGE: %s -f <touchpad FW> -o <output file>\n";

	while ((nopt = getopt_long(argc, argv, short_opt,
						long_opts, NULL)) != -1) {
		switch (nopt) {
		case 'f': /* -f or --firmware */
			tp_fw_name = optarg;
			break;

		case 'h': /* -h or --help */
			fprintf(stdout, usage, argv[0]);
			return 0;

		case 'o': /* -o or --out */
			out = optarg;
			break;

		default: /* Invalid parameter. */
			fprintf(stderr, usage, argv[0]);
			return 1;
		}
	};

	if (out == NULL)
		return 1;

	hashes = fopen(out, "we");
	if (!hashes)
		err(1, "Cannot open output file");

	fputs("#include <stdint.h>\n\n", hashes);
	if (tp_fw_name) {
		tp_fw = fopen(tp_fw_name, "re");

		if (!tp_fw) {
			warn("Cannot open firmware");
			ret = 1;
			goto out;
		}

		ret = hash_fw(tp_fw, hashes);

		fclose(tp_fw);
	} else {
		printf("No touchpad FW provided, outputting blank hashes.\n");
		ret = hash_fw_blank(hashes);
	}

out:
	fclose(hashes);

	/* In case of failure, remove output file. */
	if (ret != 0)
		unlink(out);

	return ret;
}
