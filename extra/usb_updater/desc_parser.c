/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ctype.h>
#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "desc_parser.h"

static FILE *hash_file_;
static int line_count_;
static int section_count_;

/*
 * This is used to verify consistency of the description database, namely that
 * all hash sections include the same number of hash variants.
 */
static size_t variant_count;

/* Size of the retrieved string or negative OS error value. */
static ssize_t get_next_line(char *next_line, size_t line_size)
{
	size_t index = 0;

	while (fgets(next_line + index, line_size - index, hash_file_)) {
		line_count_++;

		if (next_line[index] == '#')
			continue; /* Skip the comment */

		if (next_line[index] == '\n') {
			/*
			 * This is an empty line, return all collected data,
			 * pontintially an array of size zero if this is a
			 * repeated empty line.
			 */
			next_line[index] = '\0';
			return index;
		}

		/* Make sure next string overwrites this string's newline. */
		index += strlen(next_line + index) - 1;

		if (index >= (line_size - 1)) {
			fprintf(stderr, "%s: Input overflow in line %d\n",
				__func__, line_count_);
			return -EOVERFLOW;
		}
	}

	if (index) {
		/*
		 * This must be the last line in the file with no empty line
		 * after it. Drop the closing newline, if it is there.
		 */
		if (next_line[index] == '\n')
			next_line[index--] = '\0';

		return index;
	}
	return errno ? -errno : -ENODATA;
}

static int get_next_token(char *input, size_t expected_size, char **output)
{
	char *next_colon;

	next_colon = strchr(input, ':');
	if (next_colon)
		*next_colon = '\0';
	if (!next_colon || (expected_size &&
			    strlen(input) != expected_size)) {
		fprintf(stderr, "Invalid entry in section %d\n",
			section_count_);
		return -EINVAL;
	}

	*output = next_colon + 1;
	return 0;
}

static int get_hex_value(char *input, char **output)
{
	char *e;
	long int value;

	if (strchr(input, ':'))
		get_next_token(input, 0, output);
	else
		*output = NULL;

	value = strtol(input, &e, 16);
	if ((e && *e) || (strlen(input) > 8)) {
		fprintf(stderr, "Invalid hex value %s in section %d\n",
			input, section_count_);
		return -EINVAL;
	}

	return value;
}

static int parse_range(char *next_line,
		       size_t line_len,
		       struct addr_range *parsed_range)
{
	char *line_cursor;
	char *next_token;
	int is_a_hash_range;
	struct result_node *node;
	int value;

	section_count_++;
	line_cursor = next_line;

	/* Range type. */
	if (get_next_token(line_cursor, 1, &next_token))
		return -EINVAL;

	switch (*line_cursor) {
	case 'a':
		parsed_range->range_type = AP_RANGE;
		break;
	case 'e':
		parsed_range->range_type = EC_RANGE;
		break;
	case 'g':
		parsed_range->range_type = EC_GANG_RANGE;
		break;
	default:
		fprintf(stderr, "Invalid range type %c in section %d\n",
			*line_cursor, section_count_);
		return -EINVAL;
	}
	line_cursor = next_token;

	/* Hash or dump? */
	if (get_next_token(line_cursor, 1, &next_token))
		return -EINVAL;

	switch (*line_cursor) {
	case 'd':
		is_a_hash_range = 0;
		break;
	case 'h':
		is_a_hash_range = 1;
		break;
	default:
		fprintf(stderr, "Invalid entry kind %c in section %d\n",
			*line_cursor, section_count_);
		return -EINVAL;
	}
	line_cursor = next_token;

	/* Range base address. */
	value = get_hex_value(line_cursor, &next_token);
	if (value < 0)
		return -EINVAL;
	parsed_range->base_addr = value;

	/* Range size. */
	line_cursor = next_token;
	value = get_hex_value(line_cursor, &next_token);
	if (value < 0)
		return -EINVAL;
	parsed_range->range_size = value;

	if (!next_token && is_a_hash_range) {
		fprintf(stderr, "Missing hash in section %d\n", section_count_);
		return -EINVAL;
	}

	if (next_token && !is_a_hash_range) {
		fprintf(stderr, "Unexpected data in section %d\n",
			section_count_);
		return -EINVAL;
	}

	parsed_range->variant_count = 0;
	if (!is_a_hash_range)
		return 0; /* No more input for dump ranges. */

	node = parsed_range->variants;
	do { /* While line is not over. */
		char c;
		int i = 0;

		line_cursor = next_token;
		next_token = strchr(line_cursor, ':');
		if (next_token)
			*next_token++ = '\0';
		if (strlen(line_cursor) != (2 * sizeof(*node))) {
			fprintf(stderr,
				"Invalid hash %zd size %zd in section %d\n",
				parsed_range->variant_count + 1,
				strlen(line_cursor), section_count_);
			return -EINVAL;
		}

		while ((c = *line_cursor++) != 0) {
			uint8_t nibble;

			if (!isxdigit(c)) {
				fprintf(stderr,
					"Invalid hash %zd value in section %d\n",
					parsed_range->variant_count + 1,
					section_count_);
				return -EINVAL;
			}

			if (c <= '9')
				nibble = c - '0';
			else if (c >= 'a')
				nibble = c - 'a' + 10;
			else
				nibble = c - 'A' + 10;

			if (i & 1)
				node->expected_result[i / 2] |= nibble;
			else
				node->expected_result[i / 2] = nibble << 4;

			i++;
		}

		node++;
		parsed_range->variant_count++;

	} while (next_token);

	return 0;
}

int parser_get_next_range(struct addr_range **range)
{
	char next_line[1000]; /* Should be enough for the largest descriptor. */
	ssize_t entry_size;
	struct addr_range *new_range;
	int rv;

	/*
	 * We come here after hash descriptor database file was opened and the
	 * current board's section has been found. Just in case check if the
	 * file has been opened.
	 */
	if (!hash_file_ || !range)
		return -EIO;

	*range = NULL;
	do {
		entry_size = get_next_line(next_line, sizeof(next_line));
		if (entry_size < 0)
			return entry_size;
	} while (!entry_size); /* Skip empty lines. */

	if (entry_size == 4) /* Next board's entry must have been reached. */
		return -ENODATA;

	/* This sure will be enough to fit parsed structure contents. */
	new_range = malloc(sizeof(*new_range) + entry_size);
	if (!new_range) {
		fprintf(stderr, "Failed to allocate %zd bytes\n",
			sizeof(*new_range) + entry_size);
		return -ENOMEM;
	}

	/* This must be a new descriptor section, lets parse it. */
	rv = parse_range(next_line, entry_size, new_range);

	if (rv) {
		free(new_range);
		return rv;
	}

	if (new_range->variant_count) {
		/*
		 * A new range was found, if this is the first hash range we
		 * encountered, save its dimensions for future reference.
		 *
		 * If this is not the first one - verify that it has the same
		 * number of hash variants as all previous hash blocks.
		 */
		if (!variant_count) {
			variant_count = new_range->variant_count;
		} else if (variant_count != new_range->variant_count) {
			fprintf(stderr,
				"Unexpected number of variants in section %d\n",
				section_count_);
			free(new_range);
			return -EINVAL;
		}
	}

	*range = new_range;
	return 0;

}

int parser_find_board(const char *hash_file_name, const char *board_id)
{
	char next_line[1000]; /* Should be enough for the largest descriptor. */
	ssize_t id_len = strlen(board_id);

	if (!hash_file_) {
		hash_file_ = fopen(hash_file_name, "r");
		if (!hash_file_) {
			fprintf(stderr, "Error:%s can not open file '%s'\n",
				strerror(errno), hash_file_name);
			return errno;
		}
	}

	while (1) {
		ssize_t entry_size;

		entry_size = get_next_line(next_line, sizeof(next_line));
		if (entry_size < 0) {
			return entry_size;
		}

		if ((entry_size == id_len) &&
		    !memcmp(next_line, board_id, id_len)) {
			variant_count = 0;
			return 0;
		}
	}

	return -ENODATA;
}

void parser_done(void)
{
	if (!hash_file_)
		return;

	fclose(hash_file_);
	hash_file_ = NULL;
}

#ifdef TEST_PARSER
int main(int argc, char **argv)
{
	const char *board_name = "QZUX";
	char next_line[1000]; /* Should be enough for the largest descriptor. */
	int rv;
	int count;

	if (argc < 2) {
		fprintf(stderr, "Name of the file to parse is required.\n");
		return -1;
	}

	if (parser_find_board(argv[1], board_name)) {
		fprintf(stderr, "Board %s NOT found\n", board_name);
		return -1;
	}

	count = 0;
	do {
		struct addr_range *range;

		rv = parser_get_next_range(&range);
		count++;
		printf("Section %d, rv %d\n", count, rv);
		free(range); /* Freeing NULL is OK. */

	} while (rv != -ENODATA);

	return 0;
}
#endif
