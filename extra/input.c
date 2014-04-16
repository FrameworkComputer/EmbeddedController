/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "simulation.h"

void *entry_input(void *ptr)
{
	char *got, buf[80];
	char *str, *word, *saveptr;
	int argc;
	char *argv[40];
	int ret;

	do {
		printf("lightbar%% ");
		got = fgets(buf, sizeof(buf), stdin);
		if (got) {
			argc = 0;
			argv[argc++] = "lightbar";
			word = str = buf;
			while (word && argc < ARRAY_SIZE(argv)) {
				word = strtok_r(str, " \t\r\n", &saveptr);
				if (word)
					argv[argc++] = word;
				str = 0;
			}
			argv[argc] = 0;
			ret = fake_consolecmd_lightbar(argc, argv);
			if (ret)
				printf("ERROR %d\n", ret);
		}

	} while (got);

	exit(0);

	return 0;
}
