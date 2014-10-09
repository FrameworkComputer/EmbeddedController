/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "compile_time_macros.h"
#include "ec_commands.h"
#include "lb_common.h"
#include "lightbar.h"

static const char usage[] =
	"\n"
	"Usage:  %s [OPTIONS] [INFILE [OUTFILE]]\n"
	"\n"
	"This compiles or decompiles the lightbar programmable bytecode.\n"
	"\n"
	"Options:\n"
	"  -d         Decode binary to ascii\n"
	"  -v         Decode output should be verbose\n"
	"\n";

/* globals */
static int hit_errors;
static int opt_verbose;
static int is_jump_target[EC_LB_PROG_LEN];	/* does program jump here? */
static int is_instruction[EC_LB_PROG_LEN];	/* instruction or operand? */
static char *label[EC_LB_PROG_LEN];		/* labels we've seen */
static char *reloc_label[EC_LB_PROG_LEN];	/* put label target here */

static void Error(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	fprintf(stderr, "ERROR: ");
	vfprintf(stderr, format, ap);
	va_end(ap);
	hit_errors++;
}

static void Warning(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	fprintf(stderr, "Warning: ");
	vfprintf(stderr, format, ap);
	va_end(ap);
}

/* The longest line should have a label, an opcode, and the max operands */
#define LB_PROG_MAX_OPERANDS 4
#define MAX_WORDS (2 + LB_PROG_MAX_OPERANDS)

struct safe_lightbar_program {
	struct lightbar_program p;
	uint8_t zeros[LB_PROG_MAX_OPERANDS];
} __packed;

#define OP(NAME, BYTES, MNEMONIC) NAME,
#include "lightbar_opcode_list.h"
enum lightbyte_opcode {
	LIGHTBAR_OPCODE_TABLE
	MAX_OPCODE
};
#undef OP

#define OP(NAME, BYTES, MNEMONIC) BYTES,
#include "lightbar_opcode_list.h"
static const int num_operands[] = {
	LIGHTBAR_OPCODE_TABLE
};
#undef OP

#define OP(NAME, BYTES, MNEMONIC) MNEMONIC,
#include "lightbar_opcode_list.h"
static const char const *opcode_sym[] = {
	LIGHTBAR_OPCODE_TABLE
};
#undef OP

static const char const *control_sym[] = {
	"beg", "end", "phase", "<invalid>"
};
static const char const *color_sym[] = {
	"r", "g", "b", "<invalid>"
};

static void read_binary(FILE *fp, struct safe_lightbar_program *prog)
{
	int got;

	memset(prog, 0, sizeof(*prog));

	/* Read up to one more byte than we need, so we know if it's too big */
	got = fread(prog->p.data, 1, EC_LB_PROG_LEN + 1, fp);
	if (got < 1) {
		Error("Unable to read any input: ");
		if (feof(fp))
			fprintf(stderr, "EOF\n");
		else if (ferror(fp))
			fprintf(stderr, "%s\n", strerror(errno));
		else
			fprintf(stderr, "no idea why.\n");
	} else if (got > EC_LB_PROG_LEN) {
		Warning("Truncating input at %d bytes\n", EC_LB_PROG_LEN);
		prog->zeros[0] = 0;
		got = EC_LB_PROG_LEN;
	} else {
		prog->p.size = got;
	}
}

static uint32_t val32(uint8_t *ptr)
{
	uint32_t val;
	val = (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
	return val;
}

static int is_jump(uint8_t op)
{
	/* TODO: probably should be a field in the opcode list */
	return op >= JUMP && op <= JUMP_IF_CHARGING;
}

static void print_led_set(FILE *fp, uint8_t led)
{
	int i, first = 1;

	fprintf(fp, "{");
	for (i = 0; i < NUM_LEDS; i++)
		if (led & (1 << i)) {
			if (!first)
				fprintf(fp, ",");
			fprintf(fp, "%d", i);
			first = 0;
		}
	fprintf(fp, "}");
}

/* returns number of operands consumed */
static int print_op(FILE *fp, uint8_t addr, uint8_t cmd, uint8_t *arg)
{
	uint8_t led, color, control;
	int i, operands;

	operands = num_operands[cmd];

	/* assume valid instruction for now */
	is_instruction[addr] = 1;

	if (opt_verbose) {
		fprintf(fp, "%02x:  %02x", addr, cmd);
		for (i = 0; i < LB_PROG_MAX_OPERANDS; i++)
			if (i < operands)
				fprintf(fp, " %02x", arg[i]);
			else
				fprintf(fp, "   ");
		fprintf(fp, "\t");
	}
	if (is_jump_target[addr])
		fprintf(fp, "L00%02x:", addr);
	fprintf(fp, "\t");

	if (cmd < MAX_OPCODE)
		fprintf(fp, "%s", opcode_sym[cmd]);

	switch (cmd) {
	case JUMP:
	case JUMP_IF_CHARGING:
		fprintf(fp, "\tL00%02x\n", arg[0]);
		break;
	case JUMP_BATTERY:
		fprintf(fp, "\tL00%02x L00%02x\n", arg[0], arg[1]);
		break;
	case SET_WAIT_DELAY:
	case SET_RAMP_DELAY:
		fprintf(fp, "\t%d\n", val32(arg));
		break;
	case SET_BRIGHTNESS:
		fprintf(fp, "\t%d\n", arg[0]);
		break;
	case SET_COLOR_SINGLE:
		led = arg[0] >> 4;
		control = (arg[0] >> 2) & 0x03;
		color = arg[0] & 0x03;
		fprintf(fp, "\t");

		print_led_set(fp, led);
		fprintf(fp, ".%s", control_sym[control]);
		fprintf(fp, ".%s", color_sym[color]);
		fprintf(fp, "\t0x%02x\n", arg[1]);
		break;
	case SET_COLOR_RGB:
		led = arg[0] >> 4;
		control = (arg[0] >> 2) & 0x03;
		fprintf(fp, "\t");

		print_led_set(fp, led);
		fprintf(fp, ".%s", control_sym[control]);
		fprintf(fp, "\t0x%02x 0x%02x 0x%02x\n", arg[1], arg[2], arg[3]);
		break;
	case ON:
	case OFF:
	case WAIT:
	case GET_COLORS:
	case SWAP_COLORS:
	case RAMP_ONCE:
	case CYCLE_ONCE:
	case CYCLE:
	case HALT:
		fprintf(fp, "\n");
		break;
	default:
		fprintf(fp, "-- invalid opcode 0x%02x --\n", cmd);
		is_instruction[addr] = 0;
		hit_errors++;
	}

	return operands;
}

static void set_jump_target(uint8_t targ)
{
	if (targ >= EC_LB_PROG_LEN) {
		Warning("program jumps to 0x%02x, "
			"which out of bounds\n", targ);
		return;
	}
	is_jump_target[targ] = 1;
}

static void disassemble_prog(FILE *fp, struct safe_lightbar_program *prog)
{
	int i;
	uint8_t *ptr, op;

	/* Scan the program once to identify all the jump targets,
	 * so we can print the labels when we encounter them. */
	for (i = 0; i < prog->p.size; i++) {
		ptr = &prog->p.data[i];
		op = *ptr;
		if (is_jump(op))
			set_jump_target(ptr[1]);
		if (op == JUMP_BATTERY)
			set_jump_target(ptr[2]);
		i += num_operands[op];
	}

	/* Now disassemble */
	for (i = 0; i < prog->p.size; i++) {
		ptr = &prog->p.data[i];
		i += print_op(fp, i, *ptr, ptr + 1);
	}

	/* Finally, make sure the program doesn't jump to any location other
	 * than a valid instruction */
	for (i = 0; i < EC_LB_PROG_LEN; i++)
		if (is_jump_target[i] && !is_instruction[i]) {
			Warning("program jumps to 0x%02x, "
				"which is not a valid instruction\n", i);
		}
}

/* We'll split each line into an array of these. */
struct parse_s {
	char *word;
	int is_num;
	uint32_t val;
};

/* Fills in struct, returns number of words found. Note that pointers are only
 * copied. The strings they point to are not duplicated.  */
static int split_line(char *buf, char *delim, struct parse_s *elt, int max)
{
	char *w, *ptr, *buf_savetok;
	int i;
	char *e = 0;

	memset(elt, 0, max * sizeof(*elt));

	for (ptr = buf, i = 0;
	     i < max && (w = strtok_r(ptr, delim, &buf_savetok)) != 0;
	     ptr = 0, i++) {
		elt[i].word = w;
		elt[i].val = (uint32_t)strtoul(w, &e, 0);
		if (!e || !*e)
			elt[i].is_num = 1;

	}

	return i;
}

/* Decode led set. Return 0 if bogus, 1 if okay. */
static int is_led_set(char *buf, uint8_t *valp)
{
	uint8_t led = 0;
	unsigned long int next_led;
	char *ptr;

	if (!buf)
		return 0;

	if (*buf != '{')
		return 0;

	buf++;
	for (;;) {
		next_led = strtoul(buf, &ptr, 0);
		if (buf == ptr) {
			if (buf[0] == '}' && buf[1] == 0) {
				*valp = led;
				return 1;
			} else
				return 0;
		}

		if (next_led >= NUM_LEDS)
			return 0;

		led |= 1 << next_led;

		buf = ptr;
		if (*buf == ',')
			buf++;
	}
}

/* Decode color arg based on expected control param sections.
 * Return 0 if bogus, 1 if okay.
 */
static int is_color_arg(char *buf, int expected, uint32_t *valp)
{
	struct parse_s token[MAX_WORDS];
	uint8_t led, control, color;
	int i;

	if (!buf)
		return 0;

	/* There should be three terms, separated with '.' */
	i = split_line(buf, ".", token, MAX_WORDS);
	if (i != expected)
		return 0;

	if (!is_led_set(token[0].word, &led)) {
		Error("Invalid LED set \"%s\"\n", token[0].word);
		return 0;
	}

	for (i = 0; i < LB_CONT_MAX; i++)
		if (!strcmp(token[1].word, control_sym[i])) {
			control = i;
			break;
		}
	if (i >= LB_CONT_MAX)
		return 0;

	if (expected == 3) {
		for (i = 0; i < ARRAY_SIZE(color_sym); i++)
			if (!strcmp(token[2].word, color_sym[i])) {
				color = i;
				break;
			}
		if (i >= ARRAY_SIZE(color_sym))
			return 0;
	} else
		color = 0;


	*valp = ((led & 0xF) << 4) | ((control & 0x3) << 2) | (color & 0x3);
	return 1;
}

static void fixup_symbols(struct safe_lightbar_program *prog)
{
	int i, j;

	for (i = 0; i < EC_LB_PROG_LEN; i++) {
		if (reloc_label[i]) {
			/* Looking for reloc label */
			for (j = 0; j < EC_LB_PROG_LEN; j++) {
				if (label[j] && !strcmp(label[j],
							reloc_label[i])) {
					prog->p.data[i] = j;
					break;
				}
			}
			if (j >= EC_LB_PROG_LEN)
				Error("Can't find label %s from line %d\n", j);
		}
	}
}


static void compile(FILE *fp, struct safe_lightbar_program *prog)
{
	char buf[128];
	struct parse_s token[MAX_WORDS];
	char *s;
	int line = 0, chopping = 0;
	uint8_t addr = 0;
	int opcode;
	int wnum, wordcnt;
	int i;

	while (fgets(buf, sizeof(buf), fp)) {

		/* We truncate lines that are too long */
		s = strchr(buf, '\n');
		if (chopping) {
			if (s)
				chopping = 0;
			continue;
		}

		/* Got something to look at */
		line++;
		if (!s) {
			chopping = 1;
			Warning("truncating line %d\n", line);
		}

		/* Ignore comments */
		s = strchr(buf, '#');
		if (s)
			*s = '\0';

		wordcnt = split_line(buf, " \t\n", token, MAX_WORDS);
		if (!wordcnt)
			continue;

		wnum = 0;

		/* A label must be the first word, ends with a ':' (no spaces
		 * before it), and doesn't start with a ':' */
		s = strchr(token[0].word, ':');
		if (s && s[1] == '\0' && s != token[0].word) {
			*s = '\0';
			label[addr] = strdup(token[0].word);
			wnum++;
		}

		/* How about an opcode? */
		for (opcode = 0; opcode < MAX_OPCODE; opcode++)
			if (!strcasecmp(token[wnum].word, opcode_sym[opcode]))
				break;

		if (opcode >= MAX_OPCODE) {
			Error("Unrecognized opcode \"%s\""
			      " at line %d\n", token[wnum].word, line);
			continue;
		}

		/* Do we even have a place to write this opcode? */
		if (addr >= EC_LB_PROG_LEN) {
			Error("out of program space at line %d\n", line);
			break;
		}

		/* Got an opcode. Save it! */
		prog->p.data[addr++] = opcode;
		wnum++;

		/* Now we need operands. */
		switch (opcode) {
		case JUMP:
		case JUMP_IF_CHARGING:
			/* a label */
			if (token[wnum].word)
				reloc_label[addr++] = strdup(token[wnum].word);
			else
				Error("Missing jump target at line %d\n", line);
			break;
		case JUMP_BATTERY:
			/* two labels*/
			if (token[wnum].word)
				reloc_label[addr++] = strdup(token[wnum].word);
			else {
				Error("Missing first jump target "
				      "at line %d\n", line);
				break;
			}
			wnum++;
			if (token[wnum].word)
				reloc_label[addr++] = strdup(token[wnum].word);
			else
				Error("Missing second jump target "
				      "at line %d\n", line);
			break;

		case SET_BRIGHTNESS:
			/* one 8-bit arg */
			if (token[wnum].is_num)
				prog->p.data[addr++] = token[wnum].val;
			else
				Error("Missing/invalid arg at line %d\n", line);
			break;

		case SET_WAIT_DELAY:
		case SET_RAMP_DELAY:
			/* one 32-bit arg */
			if (token[wnum].is_num) {
				prog->p.data[addr++] =
					(token[wnum].val >> 24) & 0xff;
				prog->p.data[addr++] =
					(token[wnum].val >> 16) & 0xff;
				prog->p.data[addr++] =
					(token[wnum].val >> 8) & 0xff;
				prog->p.data[addr++] =
					token[wnum].val & 0xff;
			} else {
				Error("Missing/invalid arg at line %d\n", line);
			}
			break;

		case SET_COLOR_SINGLE:
			/* one magic word, then one more 8-bit arg */
			i = is_color_arg(token[wnum].word, 3, &token[wnum].val);
			if (!i) {
				Error("Missing/invalid arg at line %d\n", line);
				break;
			}
			/* save the magic number */
			prog->p.data[addr++] = token[wnum++].val;
			/* and the color immediate */
			if (token[wnum].is_num) {
				prog->p.data[addr++] =
					token[wnum++].val;
			} else {
				Error("Missing/Invalid arg "
				      "at line %d\n", line);
				break;
			}
			break;
		case SET_COLOR_RGB:
			/* one magic word, then three more 8-bit args */
			i = is_color_arg(token[wnum].word, 2, &token[wnum].val);
			if (!i) {
				Error("Missing/invalid arg at line %d\n", line);
				break;
			}
			/* save the magic number */
			prog->p.data[addr++] = token[wnum++].val;
			/* and the color immediates */
			for (i = 0; i < 3; i++) {
				if (token[wnum].is_num) {
					prog->p.data[addr++] =
						token[wnum++].val;
				} else {
					Error("Missing/Invalid arg "
					      "at line %d\n", line);
					break;
				}
			}
			break;

		default:
			/* No args needed */
			break;
		}

		/* Did we run past the end? */
		if (addr > EC_LB_PROG_LEN) {
			Error("out of program space at line %d\n", line);
			break;
		}
	}
	if (ferror(fp))
		Error("problem while reading input: %s\n", strerror(errno));

	if (!hit_errors)
		fixup_symbols(prog);

	if (!hit_errors)
		prog->p.size = addr;

	if (!prog->p.size)
		Error("input file produced no output bytes\n");
}

int main(int argc, char *argv[])
{
	struct safe_lightbar_program safe_prog;
	int opt_decode = 0;
	int c;
	int errorcnt = 0;
	char *infile, *outfile;
	FILE *ifp, *ofp;

	char *progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	opterr = 0;                     /* quiet, you */
	while ((c = getopt(argc, argv, ":dv")) != -1) {
		switch (c) {
		case 'd':
			opt_decode = 1;
			break;
		case 'v':
			opt_verbose = 1;
			break;

		case '?':
			fprintf(stderr, "%s: unrecognized switch: -%c\n",
				progname, optopt);
			errorcnt++;
			break;
		case ':':
			fprintf(stderr, "%s: missing argument to -%c\n",
				progname, optopt);
			errorcnt++;
			break;
		default:
			errorcnt++;
			break;
		}
	}

	if (errorcnt) {
		fprintf(stderr, "\nUsage:  %s [options] ...\n\n", progname);
		exit(1);
	}

	if (argc - optind > 0) {
		infile = argv[optind];
		ifp = fopen(infile, "rb");
		if (!ifp) {
			fprintf(stderr,
				"%s: Unable to open %s for reading: %s\n",
				progname, infile, strerror(errno));
			exit(1);
		}
	} else {
		infile = "stdin";
		ifp = stdin;
	}

	if (argc - optind > 1) {
		outfile = argv[optind + 1];
		ofp = fopen(outfile, "wb");
		if (!ofp) {
			fprintf(stderr,
				"%s: Unable to open %s for writing: %s\n",
				progname, outfile, strerror(errno));
			exit(1);
		}
	} else {
		outfile = "stdout";
		ofp = stdout;
	}

	if (opt_decode) {
		read_binary(ifp, &safe_prog);
		fclose(ifp);
		if (hit_errors)
			return 1;
		fprintf(ofp, "# %s\n", infile);
		disassemble_prog(ofp, &safe_prog);
		fclose(ofp);
	} else {
		memset(&safe_prog, 0, sizeof(safe_prog));
		compile(ifp, &safe_prog);
		fclose(ifp);
		if (!hit_errors) {
			if (1 != fwrite(safe_prog.p.data,
					safe_prog.p.size, 1, ofp))
				Error("%s: Unable to write to %s: %s\n",
				      progname, outfile, strerror(errno));
			else
				fprintf(stderr, "0x%02x bytes written to %s\n",
					safe_prog.p.size, outfile);
		}
		fclose(ofp);
	}

	return hit_errors;
}
