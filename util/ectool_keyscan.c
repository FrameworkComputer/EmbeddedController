/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <endian.h>

#include "comm-host.h"
#include "keyboard_config.h"
#include "ectool.h"

enum {
	/* Alloc this many more scans when needed */
	KEYSCAN_ALLOC_STEP	= 64,
	KEYSCAN_MAX_TESTS	= 10,	/* Maximum number of tests supported */
	KEYSCAN_MAX_INPUT_LEN	= 20,	/* Maximum characters we can receive */
};

/* A single entry of the key matrix */
struct matrix_entry {
	int row;	/* key matrix row */
	int col;	/* key matrix column */
	int keycode;	/* corresponding linux key code */
};

struct keyscan_test_item {
	uint32_t beat;			/* Beat number */
	uint8_t scan[KEYBOARD_COLS_MAX];	/* Scan data */
};

/* A single test, consisting of a list of key scans and expected ascii input */
struct keyscan_test {
	char *name;		/* name of test */
	char *expect;		/* resulting input we expect to see */
	int item_count;		/* number of items in data */
	int item_alloced;	/* number of items alloced in data */
	struct keyscan_test_item *items;	/* key data for EC */
};

/* A list of tests that we can run */
struct keyscan_info {
	unsigned int beat_us;	/* length of each beat in microseconds */
	struct keyscan_test tests[KEYSCAN_MAX_TESTS];	/* the tests */
	int test_count;		/* number of tests */
	struct matrix_entry *matrix;	/* the key matrix info */
	int matrix_count;	/* number of keys in matrix */
};

/**
 * Read the key matrix from the device tree
 *
 * Keymap entries in the fdt take the form of 0xRRCCKKKK where
 * RR=Row CC=Column KKKK=Key Code
 *
 * @param keyscan	keyscan information
 * @param path		path to device tree file containing data
 * @return 0 if ok, -1 on error
 */
static int keyscan_read_fdt_matrix(struct keyscan_info *keyscan,
				   const char *path)
{
	struct stat buf;
	uint32_t word;
	int upto;
	FILE *f;
	int err;

	/* Allocate memory for key matrix */
	if (stat(path, &buf)) {
		fprintf(stderr, "Cannot stat key matrix file '%s'\n", path);
		return -1;
	}
	keyscan->matrix_count = buf.st_size / 4;
	keyscan->matrix = calloc(keyscan->matrix_count,
				 sizeof(*keyscan->matrix));
	if (!keyscan->matrix) {
		fprintf(stderr, "Out of memory for key matrix\n");
		return -1;
	}

	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "Cannot open key matrix file '%s'\n", path);
		return -1;
	}

	/* Now read the data */
	upto = err = 0;
	while (fread(&word, 1, sizeof(word), f) == sizeof(word)) {
		struct matrix_entry *matrix = &keyscan->matrix[upto++];

		word = be32toh(word);
		matrix->row = word >> 24;
		matrix->col = (word >> 16) & 0xff;
		matrix->keycode = word & 0xffff;

		/* Hard-code some limits for now */
		if (matrix->row >= KEYBOARD_ROWS ||
		    matrix->col >= KEYBOARD_COLS_MAX) {
			fprintf(stderr, "Matrix pos out of range (%d,%d)\n",
				matrix->row, matrix->col);
			fclose(f);
			return -1;
		}
	}
	fclose(f);
	if (!err && upto != keyscan->matrix_count) {
		fprintf(stderr, "Read mismatch from matrix file '%s'\n", path);
		err = -1;
	}

	return err;
}

/*
 * translate Linux's KEY_... values into ascii. We change space into 0xfe
 * since we use the numeric value (&32) for space. That avoids ambiguity
 * when we see a space in a key sequence file.
 */
static const unsigned char kbd_plain_xlate[] = {
	0xff, 0x1b, '1',  '2',  '3',  '4',  '5',  '6',
	'7',  '8',  '9',  '0',  '-',  '=', '\b', '\t',	/* 0x00 - 0x0f */
	'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
	'o',  'p',  '[',  ']', '\r', 0xff,  'a',  's',  /* 0x10 - 0x1f */
	'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',
	'\'',  '`', 0xff, '\\', 'z',  'x',  'c',  'v',	/* 0x20 - 0x2f */
	'b',  'n',  'm',  ',' ,  '.', '/', 0xff, 0xff, 0xff,
	0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,	/* 0x30 - 0x3f */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  '7',
	'8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',	/* 0x40 - 0x4f */
	'2',  '3',  '0',  '.', 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,	/* 0x50 - 0x5F */
	'\r', 0xff, 0xff, 0x00
};

/**
 * Add a new key to a scan
 *
 * Given a new key, this looks it up in the matrix and adds it to the scan,
 * so that if this scan were reported by the EC, the AP would the given key.
 *
 * The format of keys is a list of ascii characters, or & followed by a numeric
 * ascii value, or * followed by a numeric keycode value. Spaces are ignored
 * (use '*32' for space).
 *
 * Examples:
 * a        - a
 * &20      - space
 * *58      - KEY_CAPSLOCK
 *
 * @param keyscan	keyscan information
 * @param keysp		point to the current key string on entry; on exit it
 *			is updated to point to just after the string, plus any
 *			following space
 * @param path		path to device tree file containing data
 * @return 0 if ok, -1 on error
 */
static int keyscan_add_to_scan(struct keyscan_info *keyscan, char **keysp,
			       uint8_t scan[])
{
	const uint8_t *pos;
	struct matrix_entry *matrix;
	int keycode = -1, i;
	char *keys = *keysp;
	int key = ' ';

	if (*keys == '&') {
		/* Numeric ascii code */
		keys++;
		key = strtol(keys, &keys, 10);
		if (!key || keys == *keysp) {
			fprintf(stderr, "Invalid numeric ascii\n");
			return -1;
		}
		if (*keys == ' ')
			keys++;
		else if (*keys) {
			fprintf(stderr, "Expect space after numeric ascii\n");
			return -1;
		}
	} else if (*keys == '*') {
		/* Numeric ascii code */
		keys++;
		keycode = strtol(keys, &keys, 10);
		if (!keycode || keys == *keysp) {
			fprintf(stderr, "Invalid numeric keycode\n");
			return -1;
		}
		if (*keys == ' ')
			keys++;
		else if (*keys) {
			fprintf(stderr, "Expect space after num. keycode\n");
			return -1;
		}
	} else {
		key = *keys++;
	}

	/* Convert keycode to key if needed */
	if (keycode == -1) {
		pos = strchr(kbd_plain_xlate, key);
		if (!pos) {
			fprintf(stderr, "Key '%c' not found in xlate table\n",
				key);
			return -1;
		}
		keycode = pos - kbd_plain_xlate;
	}

	/* Look up keycode in matrix */
	for (i = 0, matrix = keyscan->matrix; i < keyscan->matrix_count;
			i++, matrix++) {
		if (matrix->keycode == keycode) {
#ifdef DEBUG
			printf("%d: %d,%d\n", matrix->keycode, matrix->row,
			       matrix->col);
#endif
			scan[matrix->col] |= 1 << matrix->row;
			*keysp = keys;
			return 0;
		}
	}
	fprintf(stderr, "Key '%c' (keycode %d) not found in matrix\n", key,
		keycode);

	return -1;
}

/**
 * Add a new keyscan to the given test
 *
 * This processes a new keyscan, consisting of a beat number and a sequence
 * of keys.
 *
 * The format of keys is a beat number followed by a list of keys, each
 * either ascii characters, or & followed by a numeric ascii value, or *
 * followed by a numeric keycode value. Spaces are ignored (use '*32' for
 * space).
 *
 * Examples:
 * 0 abc      - beat 0, press a, b and c
 * 4 a &20    - beat 4, press a and space
 * 8 *58 &13  - beat 8, press KEY_CAPSLOCK
 *
 * @param keyscan	keyscan information
 * @param linenum	line number we are reading from (for error reporting)
 * @param test		test to add this scan to
 * @param keys		key string to process
 * @return 0 if ok, -1 on error
 */
static int keyscan_process_keys(struct keyscan_info *keyscan, int linenum,
				struct keyscan_test *test, char *keys)
{
	struct keyscan_test_item *item;
	unsigned long int beat;

	/* Allocate more items if needed */
	if (!test->item_alloced || test->item_count == test->item_alloced) {
		int size, new_size;

		size = test->item_alloced * sizeof(struct keyscan_test_item);
		new_size = size + KEYSCAN_ALLOC_STEP *
			sizeof(struct keyscan_test_item);
		test->items = realloc(test->items, new_size);
		if (!test->items) {
			fprintf(stderr, "Out of memory realloc()\n");
			return -1;
		}
		memset((char *)test->items + size, '\0', new_size - size);
		test->item_alloced += KEYSCAN_ALLOC_STEP;
		new_size = size;
	}

	/* read the beat number */
	item = &test->items[test->item_count];
	beat = strtol(keys, &keys, 10);
	item->beat = beat;

	/* Read keys and add them to our scan */
	if (*keys == ' ') {
		keys++;
		while (*keys) {
			if (keyscan_add_to_scan(keyscan, &keys, item->scan)) {
				fprintf(stderr, "Line %d: Cannot parse"
					" key input '%s'\n", linenum,
					keys);
				return -1;
			}
		}
	} else if (*keys) {
		fprintf(stderr, "Line %d: Need space after beat\n",
			linenum);
		return -1;
	}
	test->item_count++;

	return 0;
}

/* These are the commands we understand in a key sequence file */
enum keyscan_cmd {
	KEYSCAN_CMD_TEST,	/* start a new test */
	KEYSCAN_CMD_ENDTEST,	/* end a test */
	KEYSCAN_CMD_SEQ,	/* add a keyscan to a test sequence */
	KEYSCAN_CMD_EXPECT,	/* indicate what input is expected */

	KEYSCAN_CMD_COUNT
};

/* Names for each of the keyscan commands */
static const char *keyscan_cmd_name[KEYSCAN_CMD_COUNT] = {
	"test",
	"endtest",
	"seq",
	"expect",
};

/**
 * Read a command from a string and return it
 *
 * @param str	String containing command
 * @param len	Length of command string
 * @return detected command, or -1 if none
 */
static enum keyscan_cmd keyscan_read_cmd(const char *str, int len)
{
	int i;

	for (i = 0; i < KEYSCAN_CMD_COUNT; i++) {
		if (!strncmp(keyscan_cmd_name[i], str, len))
			return i;
	}

	return -1;
}

/**
 * Process a key sequence file a build up a list of tets from it
 *
 * @param f		File containing keyscan info
 * @param keyscan	keyscan information
 * @return 0 if ok, -1 on error
 */
static int keyscan_process_file(FILE *f, struct keyscan_info *keyscan)
{
	struct keyscan_test *cur_test;
	char line[256];
	char *str;
	int linenum;

	keyscan->test_count = 0;

	linenum = 0;
	cur_test = NULL;
	while (str = fgets(line, sizeof(line), f), str) {
		char *args, *end;
		int cmd, len;

		linenum++;
		len = strlen(str);

		/* chomp the trailing newline */
		if (len > 0 && str[len - 1] == '\n') {
			len--;
			str[len] = '\0';
		}

		/* deal with blank lines and comments */
		if (!len || *str == '#')
			continue;

		/* get the command */
		for (end = str; *end && *end != ' '; end++)
			;

		cmd = keyscan_read_cmd(str, end - str);
		args = end + (*end != '\0');
		switch (cmd) {
		case KEYSCAN_CMD_TEST:
			/* Start a new test */
			if (keyscan->test_count == KEYSCAN_MAX_TESTS) {
				fprintf(stderr, "KEYSCAN_MAX_TESTS "
					"exceeded\n");
				return -1;
			}
			cur_test = &keyscan->tests[keyscan->test_count];
			cur_test->name = strdup(args);
			if (!cur_test->name) {
				fprintf(stderr, "Line %d: out of memory\n",
					linenum);
			}
			break;
		case KEYSCAN_CMD_EXPECT:
			/* Get expect string */
			if (!cur_test) {
				fprintf(stderr, "Line %d: expect should be "
					"inside test\n", linenum);
				return -1;
			}
			cur_test->expect = strdup(args);
			if (!cur_test->expect) {
				fprintf(stderr, "Line %d: out of memory\n",
					linenum);
			}
			break;
		case KEYSCAN_CMD_ENDTEST:
			/* End of a test */
			keyscan->test_count++;
			cur_test = NULL;
			break;
		case KEYSCAN_CMD_SEQ:
			if (keyscan_process_keys(keyscan, linenum, cur_test,
					args)) {
				fprintf(stderr, "Line %d: Cannot parse key "
					"input '%s'\n", linenum, args);
				return -1;
			}
			break;
		default:
			fprintf(stderr, "Line %d: Uknown command '%1.*s'\n",
				linenum, (int)(end - str), str);
			return -1;
		}
	}

	return 0;
}

/**
 * Print out a list of all tests
 *
 * @param keyscan	keyscan information
 */
static void keyscan_print(struct keyscan_info *keyscan)
{
	int testnum;
	int i;

	for (testnum = 0; testnum < keyscan->test_count; testnum++) {
		struct keyscan_test *test = &keyscan->tests[testnum];

		printf("Test: %s\n", test->name);
		for (i = 0; i < test->item_count; i++) {
			struct keyscan_test_item *item;
			int j;

			item = &test->items[i];
			printf("%2d  %7d:  ", i, item->beat);
			for (j = 0; j < sizeof(item->scan); j++)
				printf("%02x ", item->scan[j]);
			printf("\n");
		}
		printf("\n");
	}
}

/**
 * Set the terminal to raw mode, or cooked
 *
 * @param tty_fd	Terminal file descriptor to change
 * @Param raw		0 for cooked, non-zero for raw
 */
static void set_to_raw(int tty_fd, int raw)
{
	struct termios tty_attr;
	int value;

	value = fcntl(tty_fd, F_GETFL);

	tcgetattr(tty_fd, &tty_attr);
	if (raw) {
		tty_attr.c_lflag &= ~ICANON;
		value |= O_NONBLOCK;
	} else {
		tty_attr.c_lflag |= ICANON;
		value &= ~O_NONBLOCK;
	}
	tcsetattr(tty_fd, TCSANOW, &tty_attr);
	fcntl(tty_fd, F_SETFL, value);
}

/**
 * Read input for a whlie until wee see no more
 *
 * @param fd		File descriptor for input
 * @param input		Place to put input string
 * @param max_len	Maximum length of input string
 * @param wait		Number of microseconds to wait for input
 */
static void keyscan_get_input(int fd, char *input, int max_len, int wait)
{
	int len;

	usleep(wait);
	input[0] = '\0';
	len = read(fd, input, max_len - 1);
	if (len > 0)
		input[len] = '\0';
}

static int keyscan_send_sequence(struct keyscan_info *keyscan,
				 struct keyscan_test *test)
{
	struct ec_params_keyscan_seq_ctrl *req;
	struct keyscan_test_item *item;
	int upto, size, rv;

	size = sizeof(*req) + sizeof(item->scan);
	req = (struct ec_params_keyscan_seq_ctrl *)malloc(size);
	if (!req) {
		fprintf(stderr, "Out of memory for message\n");
		return -1;
	}
	for (upto = rv = 0, item = test->items; rv >= 0 &&
			upto < test->item_count; upto++, item++) {
		req->cmd = EC_KEYSCAN_SEQ_ADD;
		req->add.time_us = item->beat * keyscan->beat_us;
		memcpy(req->add.scan, item->scan, sizeof(item->scan));
		rv = ec_command(EC_CMD_KEYSCAN_SEQ_CTRL, 0, req, size, NULL, 0);
	}
	free(req);
	if (rv < 0)
		return rv;

	return 0;
}

/**
 * Run a single test
 *
 * @param keyscan	keyscan information
 * @param test		test to run
 * @return 0 if test passes, -ve if some error occurred
 */
static int run_test(struct keyscan_info *keyscan, struct keyscan_test *test)
{
	struct ec_params_keyscan_seq_ctrl ctrl;
	char input[KEYSCAN_MAX_INPUT_LEN];
	struct ec_result_keyscan_seq_ctrl *resp;
	int wait_us;
	int size;
	int rv;
	int fd = 0;
	int i;

	/* First clear the sequence */
	ctrl.cmd = EC_KEYSCAN_SEQ_CLEAR;
	rv = ec_command(EC_CMD_KEYSCAN_SEQ_CTRL, 0, &ctrl, sizeof(ctrl),
			NULL, 0);
	if (rv < 0)
		return rv;

	rv = keyscan_send_sequence(keyscan, test);
	if (rv < 0)
		return rv;

	/* Start it */
	set_to_raw(fd, 1);
	ctrl.cmd = EC_KEYSCAN_SEQ_START;
	rv = ec_command(EC_CMD_KEYSCAN_SEQ_CTRL, 0, &ctrl, sizeof(ctrl),
			NULL, 0);
	if (rv < 0)
		return rv;

	/* Work out how long we need to wait */
	wait_us = 100 * 1000;	/* Wait 100ms to at least */
	if (test->item_count) {
		struct keyscan_test_item *ksi;

		ksi = &test->items[test->item_count - 1];
		wait_us += ksi->beat * keyscan->beat_us;
	}

	/* Wait for input */
	keyscan_get_input(fd, input, sizeof(input), wait_us);
	set_to_raw(fd, 0);

	/* Ask EC for results */
	size = sizeof(*resp) + test->item_count;
	resp = malloc(size);
	if (!resp) {
		fprintf(stderr, "Out of memory for results\n");
		return -1;
	}
	ctrl.cmd = EC_KEYSCAN_SEQ_COLLECT;
	ctrl.collect.start_item = 0;
	ctrl.collect.num_items = test->item_count;
	rv = ec_command(EC_CMD_KEYSCAN_SEQ_CTRL, 0, &ctrl, sizeof(ctrl),
			resp, size);
	if (rv < 0)
		return rv;

	/* Check what scans were skipped */
	for (i = 0; i < resp->collect.num_items; i++) {
		struct ec_collect_item *item;
		struct keyscan_test_item *ksi;

		item = &resp->collect.item[i];
		ksi = &test->items[i];
		if (!(item->flags & EC_KEYSCAN_SEQ_FLAG_DONE))
			printf(" [skip %d at beat %u] ", i, ksi->beat);
	}

	if (strcmp(input, test->expect)) {
		printf("Expected '%s', got '%s' ", test->expect, input);
		return -1;
	}

	return 0;
}

/**
 * Run all tests
 *
 * @param keyscan	keyscan information
 * @return 0 if ok, -1 on error
 */
static int keyscan_run_tests(struct keyscan_info *keyscan)
{
	int testnum;
	int any_err = 0;

	for (testnum = 0; testnum < keyscan->test_count; testnum++) {
		struct keyscan_test *test = &keyscan->tests[testnum];
		int err;

		fflush(stdout);
		err = run_test(keyscan, test);
		any_err |= err;
		if (err) {
			printf("%d: %s:  : FAIL\n", testnum, test->name);
		}
	}

	return any_err ? -1 : 0;
}

int cmd_keyscan(int argc, char *argv[])
{
	struct keyscan_info keyscan;
	FILE *f;
	int err;

	argc--;
	argv++;
	if (argc < 2) {
		fprintf(stderr, "Must specify beat period and filename\n");
		return -1;
	}
	memset(&keyscan, '\0', sizeof(keyscan));
	keyscan.beat_us = atoi(argv[0]);
	if (keyscan.beat_us < 100)
		fprintf(stderr, "Warning: beat period is normally > 100us\n");
	f = fopen(argv[1], "r");
	if (!f) {
		perror("Cannot open file\n");
		return -1;
	}

	/* TODO(crosbug.com/p/23826): Read key matrix from fdt */
	err = keyscan_read_fdt_matrix(&keyscan, "test/test-matrix.bin");
	if (!err)
		err = keyscan_process_file(f, &keyscan);
	if (!err)
		keyscan_print(&keyscan);
	if (!err)
		err = keyscan_run_tests(&keyscan);
	fclose(f);

	return err;
}
