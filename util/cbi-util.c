/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Cros Board Info utility
 */

#include "crc8.h"
#include "cros_board_info.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <compile_time_macros.h>
#include <dirent.h>
#include <getopt.h>
#include <unistd.h>

#define ARGS_MASK_BOARD_VERSION BIT(0)
#define ARGS_MASK_FILENAME BIT(1)
#define ARGS_MASK_SIZE BIT(2)
#define ARGS_MASK_SKU_ID BIT(3)

/* TODO: Set it by macro */
const char cmd_name[] = "cbi-util";

/* Command line options */
enum {
	OPT_FILENAME,
	OPT_BOARD_VERSION,
	OPT_OEM_ID,
	OPT_SKU_ID,
	OPT_DRAM_PART_NUM,
	OPT_OEM_NAME,
	OPT_MODEL_ID,
	OPT_FW_CONFIG,
	OPT_PCB_SUPPLIER,
	OPT_SSFC,
	OPT_REWORK_ID,
	OPT_FACTORY_CALIBRATION_DATA,
	OPT_SIZE,
	OPT_ERASE_BYTE,
	OPT_SHOW_ALL,
	OPT_HELP,
};

static const struct option opts_create[] = {
	{ "file", 1, 0, OPT_FILENAME },
	{ "board_version", 1, 0, OPT_BOARD_VERSION },
	{ "oem_id", 1, 0, OPT_OEM_ID },
	{ "sku_id", 1, 0, OPT_SKU_ID },
	{ "dram_part_num", 1, 0, OPT_DRAM_PART_NUM },
	{ "oem_name", 1, 0, OPT_OEM_NAME },
	{ "model_id", 1, 0, OPT_MODEL_ID },
	{ "fw_config", 1, 0, OPT_FW_CONFIG },
	{ "pcb_supplier", 1, 0, OPT_PCB_SUPPLIER },
	{ "ssfc", 1, 0, OPT_SSFC },
	{ "rework_id", 1, 0, OPT_REWORK_ID },
	{ "factory_calibration_data", 1, 0, OPT_FACTORY_CALIBRATION_DATA },
	{ "size", 1, 0, OPT_SIZE },
	{ "erase_byte", 1, 0, OPT_ERASE_BYTE },
	{ NULL, 0, 0, 0 }
};

static const struct option opts_show[] = { { "file", 1, 0, OPT_FILENAME },
					   { "all", 0, 0, OPT_SHOW_ALL },
					   { NULL, 0, 0, 0 } };

#define CBI_FIELD_NAME_ENTRY(tag) [CBI_TAG_##tag] = STRINGIFY(tag)

static const char *field_name[] = {
	CBI_FIELD_NAME_ENTRY(BOARD_VERSION),
	CBI_FIELD_NAME_ENTRY(OEM_ID),
	CBI_FIELD_NAME_ENTRY(SKU_ID),
	CBI_FIELD_NAME_ENTRY(DRAM_PART_NUM),
	CBI_FIELD_NAME_ENTRY(OEM_NAME),
	CBI_FIELD_NAME_ENTRY(MODEL_ID),
	CBI_FIELD_NAME_ENTRY(FW_CONFIG),
	CBI_FIELD_NAME_ENTRY(PCB_SUPPLIER),
	CBI_FIELD_NAME_ENTRY(SSFC),
	CBI_FIELD_NAME_ENTRY(REWORK_ID),
	CBI_FIELD_NAME_ENTRY(FACTORY_CALIBRATION_DATA),
	CBI_FIELD_NAME_ENTRY(COMMON_CONTROL),
	CBI_FIELD_NAME_ENTRY(BATTERY_CONFIG),
};
BUILD_ASSERT(ARRAY_SIZE(field_name) == CBI_TAG_COUNT);

const char help_create[] =
	"\n"
	"'%s create [ARGS]' creates an EEPROM image file.\n"
	"Required ARGS are:\n"
	"  --file <file>              Path to output file\n"
	"  --board_version <value>    Board version\n"
	"  --sku_id <value>           SKU ID\n"
	"  --size <size>              Size of output file in bytes\n"
	"\n"
	"Optional ARGS are:\n"
	"  --dram_part_num <string>   DRAM PART NUM\n"
	"  --oem_id <value>           OEM ID\n"
	"  --oem_name <string>        OEM NAME\n"
	"  --erase_byte <uint8>       Byte used for empty space. Default:0xff\n"
	"  --format_version <uint16>  Data format version\n"
	"  --model_id <value>         Model ID\n"
	"  --fw_config <value>        Firmware configuration bit-field\n"
	"  --pcb_supplier <value>     PCB supplier\n"
	"  --ssfc <value>             Second Source Factory Cache bit-field\n"
	"  --rework_id <lvalue>       REWORK_ID\n"
	"  --factory_calibration_data <value>    Factory calibration data\n"
	"\n"
	"<value> must be a positive integer <= 0XFFFFFFFF, <lvalue> must be a\n"
	"  positive integer <= 0xFFFFFFFFFFFFFFFF and field size can be\n"
	"  optionally specified by <value:size> notation: e.g. 0xabcd:4.\n"
	"<size> must be a positive integer <= 0XFFFF.\n"
	"<string> is a string\n"
	"\n";

const char help_show[] =
	"\n"
	"'%s show [ARGS]' shows data in an EEPROM image file.\n"
	"Required ARGS are:\n"
	"  --file <file>               Path to input file\n"
	"Optional ARGS are:\n"
	"  --all                       Dump all information\n"
	"It also validates the contents against the checksum and\n"
	"returns non-zero if validation fails.\n"
	"\n";

struct integer_field {
	uint32_t val;
	int size;
};

struct long_integer_field {
	uint64_t val;
	int size;
};

static void print_help_create(void)
{
	printf(help_create, cmd_name);
}

static void print_help_show(void)
{
	printf(help_show, cmd_name);
}

static void print_help(void)
{
	printf("\nUsage: %s <create|show> [ARGS]\n"
	       "\n"
	       "Utility for CBI:Cros Board Info images.\n",
	       cmd_name);
	print_help_create();
	print_help_show();
}

static int write_file(const char *filename, const uint8_t *buf, int size)
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
		fprintf(stderr, "Unable to read from %s\n", filename);
		fclose(f);
		free(buf);
		return NULL;
	}

	fclose(f);

	*size_ptr = size;
	return buf;
}

static int estimate_field_size(uint32_t value)
{
	if (value <= UINT8_MAX)
		return 1;
	if (value <= UINT16_MAX)
		return 2;
	return 4;
}

static int parse_integer_field(const char *arg, struct integer_field *f)
{
	uint64_t val;
	char *e;
	const char *ch;

	val = strtoull(arg, &e, 0);
	if (val > UINT32_MAX || !*arg || (e && *e && *e != ':')) {
		fprintf(stderr, "Invalid integer value\n");
		return -1;
	}
	f->val = val;

	ch = strchr(arg, ':');
	if (ch) {
		ch++;
		val = strtoull(ch, &e, 0);
		if (val < 1 || 4 < val || !*ch || (e && *e)) {
			fprintf(stderr, "Invalid size suffix\n");
			return -1;
		}
		f->size = val;
	} else {
		f->size = estimate_field_size(f->val);
	}

	if (f->val > (1ull << f->size * 8)) {
		fprintf(stderr, "Value (0x%x) exceeds field size (%d)\n",
			f->val, f->size);
		return -1;
	}

	return 0;
}

static int parse_uint64_field(const char *arg, struct long_integer_field *f)
{
	uint64_t val;
	char *e;
	const char *ch;

	val = strtoul(arg, &e, 0);
	/* strtoul sets an errno for invalid input. If the value read is out of
	 * range of representable values by an unsigned long int, the function
	 * returns ULONG_MAX or ULONG_MIN and the errno is set to ERANGE.
	 */
	if (errno == ERANGE || !*arg || (e && *e && *e != ':')) {
		fprintf(stderr, "Invalid integer value\n");
		return -1;
	}
	f->val = val;

	ch = strchr(arg, ':');
	if (ch) {
		ch++;
		val = strtoul(ch, &e, 0);
		if (val < 1 || 8 < val || !*ch || (e && *e)) {
			fprintf(stderr, "Invalid size suffix\n");
			return -1;
		}
		f->size = val;
	} else {
		if (f->val < UINT32_MAX)
			f->size = estimate_field_size(f->val);
		else
			f->size = 8; /* assign default long int size */
	}

	if (f->size < 8 && (f->val > (1ull << f->size * 8))) {
		fprintf(stderr, "Value (0x%llx) exceeds field size (%d)\n",
			(unsigned long long)f->val, f->size);
		return -1;
	}

	return 0;
}

static int cmd_create(int argc, char **argv)
{
	uint8_t *cbi;
	struct board_info {
		struct integer_field ver;
		struct integer_field oem;
		struct integer_field sku;
		struct integer_field model;
		struct integer_field fw_config;
		struct integer_field pcb_supplier;
		struct integer_field ssfc;
		struct long_integer_field rework;
		struct integer_field factory_calibration_data;
		const char *dram_part_num;
		const char *oem_name;
	} bi;
	struct cbi_header *h;
	int rv;
	uint8_t *p;
	const char *filename;
	uint32_t set_mask = 0;
	uint16_t size;
	uint8_t erase = 0xff;
	int i;

	memset(&bi, 0, sizeof(bi));

	while ((i = getopt_long(argc, argv, "", opts_create, NULL)) != -1) {
		uint64_t val;
		char *e;
		switch (i) {
		case '?': /* Unhandled option */
			print_help_create();
			return -1;
		case OPT_HELP:
			print_help_create();
			return 0;
		case OPT_BOARD_VERSION:
			if (parse_integer_field(optarg, &bi.ver))
				return -1;
			set_mask |= ARGS_MASK_BOARD_VERSION;
			break;
		case OPT_ERASE_BYTE:
			erase = strtoull(optarg, &e, 0);
			if (!*optarg || (e && *e)) {
				fprintf(stderr, "Invalid --erase_byte\n");
				return -1;
			}
			break;
		case OPT_FILENAME:
			filename = optarg;
			set_mask |= ARGS_MASK_FILENAME;
			break;
		case OPT_OEM_ID:
			if (parse_integer_field(optarg, &bi.oem))
				return -1;
			break;
		case OPT_SIZE:
			val = strtoull(optarg, &e, 0);
			if (val > UINT16_MAX || !*optarg || (e && *e)) {
				fprintf(stderr, "Invalid --size\n");
				return -1;
			}
			size = val;
			set_mask |= ARGS_MASK_SIZE;
			break;
		case OPT_SKU_ID:
			if (parse_integer_field(optarg, &bi.sku))
				return -1;
			set_mask |= ARGS_MASK_SKU_ID;
			break;
		case OPT_DRAM_PART_NUM:
			bi.dram_part_num = optarg;
			break;
		case OPT_OEM_NAME:
			bi.oem_name = optarg;
			break;
		case OPT_MODEL_ID:
			if (parse_integer_field(optarg, &bi.model))
				return -1;
			break;
		case OPT_FW_CONFIG:
			if (parse_integer_field(optarg, &bi.fw_config))
				return -1;
			break;
		case OPT_PCB_SUPPLIER:
			if (parse_integer_field(optarg, &bi.pcb_supplier))
				return -1;
			break;
		case OPT_SSFC:
			if (parse_integer_field(optarg, &bi.ssfc))
				return -1;
			break;
		case OPT_REWORK_ID:
			if (parse_uint64_field(optarg, &bi.rework))
				return -1;
			break;
		case OPT_FACTORY_CALIBRATION_DATA:
			if (parse_integer_field(optarg,
						&bi.factory_calibration_data))
				return -1;
			break;
		}
	}

	if (set_mask != (ARGS_MASK_BOARD_VERSION | ARGS_MASK_FILENAME |
			 ARGS_MASK_SIZE | ARGS_MASK_SKU_ID)) {
		fprintf(stderr, "Missing required arguments\n");
		print_help_create();
		return -1;
	}

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
	p = cbi_set_data(p, CBI_TAG_BOARD_VERSION, &bi.ver.val, bi.ver.size);
	p = cbi_set_data(p, CBI_TAG_OEM_ID, &bi.oem.val, bi.oem.size);
	p = cbi_set_data(p, CBI_TAG_SKU_ID, &bi.sku.val, bi.sku.size);
	p = cbi_set_data(p, CBI_TAG_MODEL_ID, &bi.model.val, bi.model.size);
	p = cbi_set_data(p, CBI_TAG_FW_CONFIG, &bi.fw_config.val,
			 bi.fw_config.size);
	p = cbi_set_data(p, CBI_TAG_PCB_SUPPLIER, &bi.pcb_supplier.val,
			 bi.pcb_supplier.size);
	p = cbi_set_data(p, CBI_TAG_SSFC, &bi.ssfc.val, bi.ssfc.size);
	p = cbi_set_data(p, CBI_TAG_REWORK_ID, &bi.rework.val, bi.rework.size);
	p = cbi_set_data(p, CBI_TAG_FACTORY_CALIBRATION_DATA,
			 &bi.factory_calibration_data.val,
			 bi.factory_calibration_data.size);
	p = cbi_set_string(p, CBI_TAG_DRAM_PART_NUM, bi.dram_part_num);
	p = cbi_set_string(p, CBI_TAG_OEM_NAME, bi.oem_name);

	h->total_size = p - cbi;
	h->crc = cbi_crc8(h);

	/* Output image */
	rv = write_file(filename, cbi, size);
	free(cbi);
	if (rv) {
		fprintf(stderr, "Unable to write CBI image to %s\n", filename);
		return rv;
	}

	fprintf(stderr, "CBI image is created successfully\n");

	return 0;
}

static void print_string(const uint8_t *buf, enum cbi_data_tag tag)
{
	struct cbi_data *d = cbi_find_tag(buf, tag);
	const char *name;

	if (!d)
		return;

	name = d->tag < CBI_TAG_COUNT ? field_name[d->tag] : "???";

	printf("    %s: %.*s (%u, %u)\n", name, d->size, (const char *)d->value,
	       d->tag, d->size);
}

static void print_integer(const uint8_t *buf, enum cbi_data_tag tag)
{
	uint64_t v;

	struct cbi_data *d = cbi_find_tag(buf, tag);
	const char *name;

	if (!d)
		return;

	name = d->tag < CBI_TAG_COUNT ? field_name[d->tag] : "???";

	switch (d->size) {
	case 1:
		v = *(uint8_t *)d->value;
		break;
	case 2:
		v = *(uint16_t *)d->value;
		break;
	case 4:
		v = *(uint32_t *)d->value;
		break;
	case 8:
		v = *(uint64_t *)d->value;
		break;
	default:
		printf("    %s: Integer of size %d not supported\n", name,
		       d->size);
		return;
	}
	printf("    %s: %llu (0x%llx, %u, %u)\n", name, (unsigned long long)v,
	       (unsigned long long)v, d->tag, d->size);
}

static int cmd_show(int argc, char **argv)
{
	uint8_t *buf;
	uint32_t size;
	struct cbi_header *h;
	uint32_t set_mask = 0;
	const char *filename;
	int show_all = 0;
	int i;

	while ((i = getopt_long(argc, argv, "", opts_show, NULL)) != -1) {
		switch (i) {
		case '?': /* Unhandled option */
			print_help_show();
			return -1;
		case OPT_HELP:
			print_help_show();
			return 0;
		case OPT_FILENAME:
			filename = optarg;
			set_mask |= ARGS_MASK_FILENAME;
			break;
		case OPT_SHOW_ALL:
			show_all = 1;
			break;
		}
	}

	if (set_mask != ARGS_MASK_FILENAME) {
		fprintf(stderr, "Missing required arguments\n");
		print_help_show();
		return -1;
	}

	buf = read_file(filename, &size);
	if (!buf) {
		fprintf(stderr, "Unable to read CBI image\n");
		return -1;
	}

	h = (struct cbi_header *)buf;
	printf("CBI image: %s\n", filename);

	if (memcmp(h->magic, cbi_magic, sizeof(cbi_magic))) {
		fprintf(stderr, "Invalid Magic\n");
		free(buf);
		return -1;
	}

	if (cbi_crc8(h) != h->crc) {
		fprintf(stderr, "Invalid CRC\n");
		free(buf);
		return -1;
	}

	printf("  TOTAL_SIZE: %u\n", h->total_size);
	if (show_all)
		printf("  CBI_VERSION: %u\n", h->version);
	printf("  Data Field: name: value (hex, tag, size)\n");
	print_integer(buf, CBI_TAG_BOARD_VERSION);
	print_integer(buf, CBI_TAG_OEM_ID);
	print_integer(buf, CBI_TAG_SKU_ID);
	print_integer(buf, CBI_TAG_MODEL_ID);
	print_integer(buf, CBI_TAG_FW_CONFIG);
	print_integer(buf, CBI_TAG_PCB_SUPPLIER);
	print_integer(buf, CBI_TAG_SSFC);
	print_integer(buf, CBI_TAG_REWORK_ID);
	print_integer(buf, CBI_TAG_FACTORY_CALIBRATION_DATA);
	print_string(buf, CBI_TAG_DRAM_PART_NUM);
	print_string(buf, CBI_TAG_OEM_NAME);

	free(buf);

	printf("Data validated successfully\n");
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Unknown option or missing value\n");
		print_help();
		return -1;
	}

	if (!strncmp(argv[1], "create", sizeof("create")))
		return cmd_create(--argc, ++argv);
	else if (!strncmp(argv[1], "show", sizeof("show")))
		return cmd_show(--argc, ++argv);

	fprintf(stderr, "Unknown option or missing value\n");
	print_help();

	return -1;
}
