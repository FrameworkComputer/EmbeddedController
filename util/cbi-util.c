/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Cros Board Info utility
 */

#include <errno.h>
#include <dirent.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "cros_board_info.h"
#include "crc8.h"

#define REQUIRED_MASK_BOARD_VERSION	(1 << 0)
#define REQUIRED_MASK_OEM_ID		(1 << 1)
#define REQUIRED_MASK_SKU_ID		(1 << 2)
#define REQUIRED_MASK_SIZE		(1 << 3)
#define REQUIRED_MASK_FILENAME		(1 << 4)
#define REQUIRED_MASK_CREATE	(REQUIRED_MASK_BOARD_VERSION | \
				 REQUIRED_MASK_OEM_ID | \
				 REQUIRED_MASK_SKU_ID | \
				 REQUIRED_MASK_SIZE | \
				 REQUIRED_MASK_FILENAME)
#define REQUIRED_MASK_SHOW	(REQUIRED_MASK_FILENAME)

struct board_info {
	uint16_t version;
	uint8_t oem_id;
	uint8_t sku_id;
} __attribute__((packed));

/* Command line options */
enum {
	/* mode options */
	OPT_MODE_NONE,
	OPT_MODE_CREATE,
	OPT_MODE_SHOW,
	OPT_BOARD_VERSION,
	OPT_OEM_ID,
	OPT_SKU_ID,
	OPT_SIZE,
	OPT_ERASE_BYTE,
	OPT_SHOW_ALL,
	OPT_HELP,
};

static const struct option long_opts[] = {
	{"create", 1, 0, OPT_MODE_CREATE},
	{"show", 1, 0, OPT_MODE_SHOW},
	{"board_version", 1, 0, OPT_BOARD_VERSION},
	{"oem_id", 1, 0, OPT_OEM_ID},
	{"sku_id", 1, 0, OPT_SKU_ID},
	{"size", 1, 0, OPT_SIZE},
	{"erase_byte", 1, 0, OPT_ERASE_BYTE},
	{"all", 0, 0, OPT_SHOW_ALL},
	{"help", 0, 0, OPT_HELP},
	{NULL, 0, 0, 0}
};

static int write_file(const char *filename, const char *buf, int size)
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

static uint8_t *read_file(const char *filename, uint32_t *size_ptr)
{
	FILE *f;
	uint8_t *buf;
	long size;

	*size_ptr = 0;

	f = fopen(filename, "rb");
	if (!f) {
		fprintf(stderr, "Unable to open file %s\n", filename);
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	rewind(f);

	if (size < 0 || size > UINT32_MAX) {
		fclose(f);
		return NULL;
	}

	buf = malloc(size);
	if (!buf) {
		fclose(f);
		return NULL;
	}

	if (1 != fread(buf, size, 1, f)) {
		fprintf(stderr, "Unable to read file %s\n", filename);
		fclose(f);
		free(buf);
		return NULL;
	}

	fclose(f);

	*size_ptr = size;
	return buf;
}

static int cbi_crc8(const struct cbi_header *h)
{
	return crc8((uint8_t *)&h->crc + 1,
		    h->total_size - sizeof(h->magic) - sizeof(h->crc));
}

static uint8_t *set_data(uint8_t *p, enum cbi_data_tag tag, void *buf, int size)
{
	struct cbi_data *d = (struct cbi_data *)p;
	d->tag = tag;
	d->size = size;
	memcpy(d->value, buf, size);
	p += sizeof(*d) + size;
	return p;
}

/*
 * Create a CBI blob
 */
static int do_create(const char *cbi_filename, uint32_t size, uint8_t erase,
		     struct board_info *bi)
{
	uint8_t *cbi;
	struct cbi_header *h;
	int rv;
	uint8_t *p;

	cbi = malloc(size);
	if (!cbi) {
		fprintf(stderr, "Failed to allocate memory\n");
		return -1;
	}
	memset(cbi, erase, size);

	h = (struct cbi_header *)cbi;
	memcpy(h->magic, cbi_magic, sizeof(cbi_magic));
	h->major_version = CBI_VERSION_MAJOR;
	h->minor_version = CBI_VERSION_MINOR;
	p = h->data;
	p = set_data(p, CBI_TAG_BOARD_VERSION,
		     &bi->version, sizeof(bi->version));
	p = set_data(p, CBI_TAG_OEM_ID, &bi->oem_id, sizeof(bi->oem_id));
	p = set_data(p, CBI_TAG_SKU_ID, &bi->sku_id, sizeof(bi->sku_id));
	h->total_size = p - cbi;
	h->crc = cbi_crc8(h);

	/* Output blob */
	rv = write_file(cbi_filename, cbi, size);
	if (rv) {
		fprintf(stderr, "Unable to write CBI blob\n");
		return rv;
	}

	fprintf(stderr, "CBI blob is created successfully\n");

	return 0;
}

static struct cbi_data *find_tag(const uint8_t *cbi, enum cbi_data_tag tag)
{
	struct cbi_data *d;
	const struct cbi_header *h = (const struct cbi_header *)cbi;
	const uint8_t *p;
	for (p = h->data; p + sizeof(*d) < cbi + h->total_size;) {
		d = (struct cbi_data *)p;
		if (d->tag == tag)
			return d;
		p += sizeof(*d) + d->size;
	}
	return NULL;
}

static int do_show(const char *cbi_filename, int show_all)
{
	uint8_t *buf;
	uint32_t size;
	struct cbi_header *h;
	struct cbi_data *d;

	if (!cbi_filename) {
		fprintf(stderr, "Missing arguments\n");
		return -1;
	}

	buf = read_file(cbi_filename, &size);
	if (!buf) {
		fprintf(stderr, "Unable to read CBI blob\n");
		return -1;
	}

	h = (struct cbi_header *)buf;
	printf("CBI blob: %s\n", cbi_filename);

	if (memcmp(h->magic, cbi_magic, sizeof(cbi_magic))) {
		fprintf(stderr, "Invalid Magic\n");
		return -1;
	}

	if (cbi_crc8(h) != h->crc) {
		fprintf(stderr, "Invalid CRC\n");
		return -1;
	}

	printf("  TOTAL_SIZE: %u\n", h->total_size);
	printf("  CBI_VERSION: %u\n", h->version);
	d = find_tag(buf, CBI_TAG_BOARD_VERSION);
	if (d)
		printf("  BOARD_VERSION: %u (0x%x)\n",
		       *(uint16_t *)d->value, *(uint16_t *)d->value);
	d = find_tag(buf, CBI_TAG_OEM_ID);
	if (d)
		printf("  OEM_ID: %u (0x%x)\n",
		       *(uint8_t *)d->value, *(uint8_t *)d->value);
	d = find_tag(buf, CBI_TAG_SKU_ID);
	if (d)
		printf("  SKU_ID: %u (0x%x)\n",
		       *(uint8_t *)d->value, *(uint8_t *)d->value);

	printf("Data validated successfully\n");
	return 0;
}

/* Print help and return error */
static void print_help(int argc, char *argv[])
{
	printf("\nUsage: cbi %s <--create|--show>\n"
	       "\n"
	       "Utility for managing Cros Board Info (CBIs).\n"
	       "\n"
	       "For '--create <cbi_file> [OPTIONS]', required OPTIONS are:\n"
	       "  --board_version <uint16>    Board version\n"
	       "  --oem_id <uint8>            OEM ID\n"
	       "  --sku_id <uint8>            SKU ID\n"
	       "  --size <uint16>             Size of output file\n"
	       "Optional OPTIONS are:\n"
	       "  --erase_byte <uint8>        Byte used for empty space\n"
	       "  --format_version <uint16>   Data format version\n"
	       "\n"
	       "For '--show <cbi_file> [OPTIONS]', OPTIONS are:\n"
	       "  --all                       Dump all information\n"
	       "  It also validates the contents against the checksum and\n"
	       "  returns non-zero if validation fails.\n"
	       "\n",
	       argv[0]);
}

int main(int argc, char **argv)
{
	int mode = OPT_MODE_NONE;
	const char *cbi_filename = NULL;
	struct board_info bi;
	uint32_t size = 0;
	uint8_t erase = 0xff;
	int show_all = 0;
	int parse_error = 0;
	uint32_t required_mask = 0;
	uint32_t set_mask = 0;
	uint64_t val;
	char *e;
	int i;

	memset(&bi, 0, sizeof(bi));

	while ((i = getopt_long(argc, argv, "", long_opts, NULL)) != -1) {
		switch (i) {
		case '?':
			/* Unhandled option */
			fprintf(stderr, "Unknown option or missing value\n");
			parse_error = 1;
			break;
		case OPT_HELP:
			print_help(argc, argv);
			return !!parse_error;
		case OPT_MODE_CREATE:
			mode = i;
			cbi_filename = optarg;
			required_mask = REQUIRED_MASK_CREATE;
			set_mask |= REQUIRED_MASK_FILENAME;
			break;
		case OPT_MODE_SHOW:
			mode = i;
			cbi_filename = optarg;
			required_mask = REQUIRED_MASK_SHOW;
			set_mask |= REQUIRED_MASK_FILENAME;
			break;
		case OPT_BOARD_VERSION:
			val = strtoul(optarg, &e, 0);
			if (val > USHRT_MAX || !*optarg || (e && *e)) {
				fprintf(stderr, "Invalid --board_version\n");
				parse_error = 1;
			}
			bi.version = val;
			set_mask |= REQUIRED_MASK_BOARD_VERSION;
			break;
		case OPT_OEM_ID:
			val = strtoul(optarg, &e, 0);
			if (val > UCHAR_MAX || !*optarg || (e && *e)) {
				fprintf(stderr, "Invalid --oem_id\n");
				parse_error = 1;
			}
			bi.oem_id = val;
			set_mask |= REQUIRED_MASK_OEM_ID;
			break;
		case OPT_SKU_ID:
			val = strtoul(optarg, &e, 0);
			if (val > UCHAR_MAX || !*optarg || (e && *e)) {
				fprintf(stderr, "Invalid --sku_id\n");
				parse_error = 1;
			}
			bi.sku_id = val;
			set_mask |= REQUIRED_MASK_SKU_ID;
			break;
		case OPT_SIZE:
			val = strtoul(optarg, &e, 0);
			if (val > USHRT_MAX || !*optarg || (e && *e)) {
				fprintf(stderr, "Invalid --size\n");
				parse_error = 1;
			}
			size = val;
			set_mask |= REQUIRED_MASK_SIZE;
			break;
		case OPT_ERASE_BYTE:
			erase = strtoul(optarg, &e, 0);
			if (!*optarg || (e && *e)) {
				fprintf(stderr, "Invalid --erase_byte\n");
				parse_error = 1;
			}
			break;
		case OPT_SHOW_ALL:
			show_all = 1;
			break;
		}
	}

	if (parse_error) {
		print_help(argc, argv);
		return 1;
	}

	if (set_mask != required_mask) {
		fprintf(stderr, "Missing required arguments\n");
		print_help(argc, argv);
		return 1;
	}

	switch (mode) {
	case OPT_MODE_CREATE:
		return do_create(cbi_filename, size, erase, &bi);
	case OPT_MODE_SHOW:
		return do_show(cbi_filename, show_all);
	case OPT_MODE_NONE:
	default:
		fprintf(stderr, "Must specify a mode.\n");
		print_help(argc, argv);
		return 1;
	}
}
