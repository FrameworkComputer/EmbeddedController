/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Generate IPI tables, and inputs come from IPI_COUNT macro in board.h.
 */

#include <stdio.h>

/* Prevent from including gpio configs . */
#define __ASSEMBLER__

#include "board.h"

#define FPRINTF(format, args...) fprintf(fout, format, ## args)

int main(int argc, char **argv)
{
	FILE *fout;
	int i;

	if (argc != 2) {
		fprintf(stderr, "USAGE: %s <output>\n", argv[0]);
		return 1;
	}

	fout = fopen(argv[1], "w");

	if (!fout) {
		fprintf(stderr, "Cannot open output file %s\n", argv[1]);
		return 1;
	}

	FPRINTF("/* This is a generated file. Do not modify. */\n");
	FPRINTF("\n");
	FPRINTF("/*\n");
	FPRINTF(" * Table to hold all the IPI handler function pointer.\n");
	FPRINTF(" */\n");
	FPRINTF("table(ipi_handler_t, ipi_handler_table,\n");

	for (i = 0; i < IPI_COUNT; ++i)
		FPRINTF("ipi_x_func(handler, ipi_arguments, %d)\n", i);

	FPRINTF(");\n");

	FPRINTF("/*\n");
	FPRINTF(" * Table to hold all the wake-up bool address.\n");
	FPRINTF(" */\n");
	FPRINTF("table(int *, ipi_wakeup_table,\n");

	for (i = 0; i < IPI_COUNT; ++i)
		FPRINTF("ipi_x_var(wakeup, %d)\n", i);

	FPRINTF(");\n");

	fclose(fout);
	return 0;
}
