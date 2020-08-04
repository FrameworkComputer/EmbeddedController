/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/160330682): Eliminate or reduce size of BMI260 initialization file.
 *
 * Once the BMI260 initialization file is moved to the kernel's rootfs, this
 * entire file can be deleted.
 */

/*
 * This utility is used to generate a compressed version of the BMI260
 * configuration file.
 *
 * This uses a very simple, but lightweight compression algorithm to detect
 * duplicated 32-bit words in the configuration data.
 *
 * Compression scheme:
 *	Repeated 32-bit words are replaced by a 16-bit key, 16-bit count, and
 *	the 32-bit data word. All values stored big-endian.
 *
 *	For example, if the uncompressed file had the following data words:
 *		0x89ABCDEF 0x89ABCDEF 0x89ABCDEF
 *
 *	This is represented compressed as (key 0xE9EA):
 *		0xE9EA0003 0x89ABCDEF
 *
 *	Key value (0xE9EA) chosen as it wasn't found in the BMI configuration
 *	data.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "endian.h"

/*
 * This key is chosen because it isn't used by the BMI260 config file.
 */
#define COMPRESS_KEY	0xE9EA

static int word_compress(uint32_t *buffer, int word_length,
			 const char *outfilename)
{
	FILE *file_p;
	const uint16_t key = COMPRESS_KEY;
	uint16_t out16;
	uint16_t in16;
	unsigned int outsize;
	uint32_t prev_word;
	int repeat_count;
	int i;

	file_p = fopen(outfilename, "wb+");
	if (!file_p) {
		fprintf(stderr, "Failed to open output file %s\n", outfilename);
		return -1;
	}

	prev_word = ~buffer[0];
	repeat_count = 1;
	outsize = 0;

	for (i = 0; i < word_length; i++) {
		in16 = *(uint16_t *)&buffer[i];
		in16 = be16toh(in16);

		if (in16 == COMPRESS_KEY) {
			fprintf(stderr, "ERROR: input data contains "
				"compression key value 0x%04x\n",
				COMPRESS_KEY);
			fprintf(stderr, "Compression of input data not "
				"supported.\n");
			outsize = -1;
			goto early_exit;
		}

		if (buffer[i] == prev_word && (repeat_count < 255)) {
			repeat_count++;
		} else {
			if (repeat_count > 2) {
				printf("Offset 0x%08lx: Write repeated "
					"signature: 0x%04x 0x%04x 0x%08x\n",
					ftell(file_p), key, repeat_count,
					prev_word);
				out16 = htobe16(key);
				fwrite(&out16, sizeof(out16), 1, file_p);
				out16 = htobe16(repeat_count);
				fwrite(&out16, sizeof(out16), 1, file_p);
				outsize += 2 * sizeof(out16);

				/*
				 * Write out original data as bytes
				 * to preserve original order.
				 */
				fwrite(&prev_word, 1, sizeof(prev_word),
				       file_p);
				outsize += sizeof(prev_word);
			} else if (i != 0) {
				do {
					fwrite(&prev_word, 1, sizeof(prev_word),
					       file_p);
					outsize += sizeof(prev_word);
				} while (--repeat_count > 0);
			}

			prev_word = buffer[i];
			repeat_count = 1;
		}
	}

	/* Write the last word or repeated words */
	if (repeat_count > 2) {
		printf("Offset 0x%08lx: Write repeated signature: "
		       "0x%04x 0x%04x 0x%08x\n",
		       ftell(file_p), key, repeat_count, prev_word);
		fwrite(&out16, sizeof(out16), 1, file_p);
		out16 = htobe16(repeat_count);
		fwrite(&out16, sizeof(out16), 1, file_p);
		outsize += 2 * sizeof(out16);

		fwrite(&prev_word, 1, sizeof(prev_word), file_p);
		outsize += sizeof(prev_word);
	} else if (i != 0) {
		do {
			fwrite(&prev_word, 1, sizeof(prev_word), file_p);
			outsize += sizeof(prev_word);
		} while (--repeat_count > 0);
	}

	if (outsize != ftell(file_p)) {
		fprintf(stderr, "Compression failed, write %d bytes, but file "
			"size is only %ld bytes\n",
			outsize, ftell(file_p));
		outsize = -1;
		goto early_exit;
	}

early_exit:
	fclose(file_p);

	return outsize;
}

static int word_decompress(uint32_t *buffer,
			   int word_length,
			   const char *outfilename)
{
	FILE *file_p;
	uint16_t *buf16;
	uint16_t key;
	uint16_t repeat_count;
	unsigned int outsize;
	uint32_t out_word;
	int i;

	file_p = fopen(outfilename, "wb+");
	if (!file_p) {
		fprintf(stderr, "Failed to open output file %s\n", outfilename);
		return -1;
	}

	repeat_count = 0;
	outsize = 0;

	for (i = 0; i < word_length; i++) {
		buf16 = (uint16_t *)&buffer[i];
		key = be16toh(*buf16);

		if (key == COMPRESS_KEY) {
			repeat_count = be16toh(*++buf16);

			if (repeat_count == 0) {
				fprintf(stderr, "Incorrect repeat count found "
					"in compressed file\n");
				outsize = -1;
				goto early_exit;
			}

			/*
			 * Note - this advances the loop to the next word
			 * in the buffer.
			 */
			if (++i >= word_length) {
				fprintf(stderr, "Unexpected file end during "
					"decompress\n");
				outsize = -1;
				goto early_exit;
			}

			out_word = buffer[i];
			while (repeat_count-- > 0) {
				fwrite(&out_word, 4, 1, file_p);
				outsize += sizeof(out_word);
			}
		} else {
			fwrite(&buffer[i], 4, 1, file_p);
			outsize += sizeof(buffer[i]);
		}
	}

early_exit:
	fclose(file_p);

	return outsize;
}

static void print_help(char *cmd_name)
{
	printf("\nUsage: %s <compress|decompress> <infile> <outfile>\n"
	       "\n"
	       "Utility to compress/decompress BMI IMU config binaries.\n",
	       cmd_name);
}

int main(int argc, char *argv[])
{
	uint32_t *buffer;
	int outsize;
	char *infilename;
	char *outfilename;
	FILE *f;
	long size;
	size_t read_words;

	if (argc < 4) {
		fprintf(stderr, "Unknown option or missing value\n");
		print_help(argv[0]);
		return EXIT_FAILURE;
	}

	infilename = argv[2];
	outfilename = argv[3];

	printf("Input (%s), output (%s)\n", infilename, outfilename);

	f = fopen(infilename, "rb");

	if (!f) {
		fprintf(stderr, "Failed to open input file %s\n", infilename);
		print_help(argv[0]);
		return EXIT_FAILURE;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	rewind(f);

	printf("Infile (%s) size %ld (bytes)\n", infilename, size);
	buffer = malloc(size);
	size /= 4;
	if (!buffer) {
		fprintf(stderr, "Failed to allocate memory. "
			"Input file size %ld bytes\n",
			size * 4);
		fclose(f);
		return EXIT_FAILURE;
	}

	read_words = fread(buffer, 4, size, f);
	if (read_words != size) {
		fprintf(stderr, "Unable to read from %s, "
			"words read %ld, needed %ld\n",
			infilename, read_words, size);
		fclose(f);
		free(buffer);
		return EXIT_FAILURE;
	}

	fclose(f);

	printf("Input file closed\n");

	if (!strncmp(argv[1], "compress", sizeof("compress"))) {
		outsize = word_compress(buffer, size, outfilename);
		if (outsize < 0) {
			free(buffer);
			return EXIT_FAILURE;
		}

		printf("Compressed file %s created - %d bytes "
			"(saves %ld bytes)\n",
			outfilename, outsize, (size * 4) - outsize);
	} else if (!strncmp(argv[1], "decompress", sizeof("decompress"))) {
		outsize = word_decompress(buffer, size, outfilename);
		if (outsize < 0) {
			free(buffer);
			return EXIT_FAILURE;
		}

		printf("Decompressed file %s created - %d bytes\n",
			outfilename, outsize);
	} else {
		fprintf(stderr, "Invalid parameter 1, "
			"must be compress/decoompress\n");
		print_help(argv[0]);
		free(buffer);
		return EXIT_FAILURE;
	}

	free(buffer);

	return EXIT_SUCCESS;
}
