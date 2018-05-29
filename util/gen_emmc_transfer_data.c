/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Generate transferring data from a file. The transferring data emulates the
 * eMMC protocol.
 */

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <compile_time_macros.h>

/* eMMC transfer block size */
#define BLOCK_SIZE		512
#define BLOCK_RAW_DATA		"bootblock_raw_data"

uint16_t crc16_arg(uint8_t data, uint16_t previous_crc)
{
	unsigned int crc = previous_crc << 8;
	int i;

	crc ^= (data << 16);
	for (i = 8; i; i--) {
		if (crc & 0x800000)
			crc ^= (0x11021 << 7);
		crc <<= 1;
	}

	return (uint16_t)(crc >> 8);
}

void header_format(FILE *fin, FILE *fout)
{
	uint8_t data[BLOCK_SIZE];
	int blk, j;
	uint16_t crc16;
	size_t cnt = 0;

	fprintf(fout, "/* This file is auto-generated. Do not modify. */\n"
		"#ifndef __CROS_EC_BOOTBLOCK_DATA_H\n"
		"#define __CROS_EC_BOOTBLOCK_DATA_H\n"
		"\n"
		"#include <stdint.h>\n"
		"\n"
		);

	fprintf(fout,
		"static const uint8_t %s[] __attribute__((aligned(4))) =\n"
		"{\n"
		"\t0xff, 0x97, /* Acknowledge boot mode: 1 S=0 010 E=1 11 */\n"
		"\t0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,\n",
		BLOCK_RAW_DATA);

	for (blk = 0;; blk++) {
		crc16 = 0;
		if (fin)
			cnt = fread(data, 1, BLOCK_SIZE, fin);

		if (cnt == 0)
			break;
		else if (cnt < BLOCK_SIZE)
			memset(&data[cnt], 0xff, BLOCK_SIZE-cnt);

		fprintf(fout, "\t/* Block %d (%ld) */\n", blk, cnt);
		fprintf(fout, "\t0xff, 0xfe, /* idle, start bit. */");
		for (j = 0; j < sizeof(data); j++) {
			fprintf(fout, "%s0x%02x,",
				(j % 8) == 0 ? "\n\t" : " ", data[j]);
			crc16 = crc16_arg(data[j], crc16);
		}
		fprintf(fout, "\n");

		fprintf(fout, "\t0x%02x, 0x%02x, 0xff,"
			" /* CRC, end bit, idle */\n",
			crc16 >> 8, crc16 & 0xff);
	}

	fprintf(fout, "\t/* Last block: idle */\n");
	fprintf(fout, "\t0xff, 0xff, 0xff, 0xff\n");
	fprintf(fout, "};\n");
	fprintf(fout, "#endif /* __CROS_EC_BOOTBLOCK_DATA_H */\n");
}

int main(int argc, char **argv)
{
	int nopt;
	int ret = 0;
	const char *output_name = NULL;
	char *input_name = NULL;
	FILE *fin = NULL;
	FILE *fout = NULL;

	const char short_opts[] = "i:ho:";
	const struct option long_opts[] = {
		{ "input", 1, NULL, 'i' },
		{ "help", 0, NULL, 'h' },
		{ "out", 1, NULL, 'o' },
		{ NULL }
	};
	const char usage[] = "USAGE: %s [-i <input>] -o <output>\n";

	while ((nopt = getopt_long(argc, argv, short_opts, long_opts,
								NULL)) != -1) {
		switch (nopt) {
		case 'i': /* -i or --input*/
			input_name = optarg;
			break;
		case 'h': /* -h or --help */
			fprintf(stdout, usage, argv[0]);
			return 0;
		case 'o': /* -o or --out */
			output_name = optarg;
			break;
		default: /* Invalid parameter. */
			fprintf(stderr, usage, argv[0]);
			return 1;
		}
	}

	if (output_name == NULL) {
		fprintf(stderr, usage, argv[0]);
		return 1;
	}

	if (input_name == NULL) {
		printf("No bootblock provided, outputting default file.\n");
	} else {
		fin = fopen(input_name, "r");
		if (!fin) {
			printf("Cannot open input file: %s\n", input_name);
			ret = 1;
			goto out;
		}
	}

	fout = fopen(output_name, "w");

	if (!fout) {
		printf("Cannot open output file: %s\n", output_name);
		ret = 1;
		goto out;
	}

	header_format(fin, fout);

	fclose(fout);

out:
	if (fin)
		fclose(fin);

	return ret;
}
