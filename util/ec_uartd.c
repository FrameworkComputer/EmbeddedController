/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* ec_uartd.c - UART daemon for BD-ICDI-B board for EC debugging
 *
 * based on chromeos_public/src/third_party/hdctools/src/ftdiuart.c
 *
 * compile with:
 *    gcc -o ftdi_uartd ftdi_uartd.c -lftdi
 */

/* Force header files to define grantpt(), posix_openpt(), cfmakeraw() */
#define _BSD_SOURCE
#define _XOPEN_SOURCE 600
/* Force header file to declare ptsname_r(), etc. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <fcntl.h>
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include <ftdi.h>
#pragma GCC diagnostic pop
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	struct ftdi_context fcontext;
	struct termios tty_cfg;
	char ptname[PATH_MAX];
	char buf[1024];
	int fd;
	int rv;

	/* Init */
	if (ftdi_init(&fcontext) < 0) {
		fprintf(stderr, "ftdi_init failed\n");
		return 1;
	}

	/* Open interface B (UART) in the FTDI device and set 115kbaud */
	ftdi_set_interface(&fcontext, INTERFACE_B);
	rv = ftdi_usb_open(&fcontext, 0x0403, 0xbcda);
	if (rv < 0) {
		fprintf(stderr, "error opening ftdi device: %d (%s)\n",
			rv, ftdi_get_error_string(&fcontext));
		return 2;
	}
	rv = ftdi_set_baudrate(&fcontext, 115200);
	if (rv < 0) {
		fprintf(stderr, "error setting baudrate: %d (%s)\n",
			rv, ftdi_get_error_string(&fcontext));
		return 2;
	}

	/* Set DTR; this muxes RX on the ICDI board */
	ftdi_setdtr(&fcontext, 1);

	/* Open the pty */
	fd = posix_openpt(O_RDWR | O_NOCTTY);
	if (fd == -1) {
		perror("opening pty master");
		return 3;
	}
	if (grantpt(fd) == -1) {
		perror("grantpt");
		return 3;
	}
	if (unlockpt(fd) == -1) {
		perror("unlockpt");
		return 3;
	}
	if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
		perror("fcntl setfl -> nonblock");
		return 3;
	}
	if (ptsname_r(fd, ptname, PATH_MAX) != 0) {
		perror("getting name of pty");
		return 3;
	}
	fprintf(stderr, "pty name = %s\n", ptname);
	if (!isatty(fd)) {
		perror("not a TTY device\n");
		return 3;
	}
	cfmakeraw(&tty_cfg);
	tcsetattr(fd, TCSANOW, &tty_cfg);
	if (chmod(ptname, 0666) == -1) {
		perror("setting pty attributes");
		return 3;
	}

	/* Read and write data forever */
	while (1) {
		int bytes = read(fd, buf, sizeof(buf));
		if (bytes > 0) {
			rv = ftdi_write_data(&fcontext, buf, bytes);
			if (rv != bytes) {
				perror("writing to uart");
				break;
			}
		}

		usleep(1000);

		bytes = ftdi_read_data(&fcontext, buf, sizeof(buf));
		if (bytes > 0) {
			int bytes_remaining = bytes;
			while ((bytes = write(fd, buf, bytes_remaining)) > 0)
				bytes_remaining -= bytes;

			if (bytes == -1)
				perror("writing ftdi data to pty");

		} else if (bytes < 0) {
			perror("failed ftdi_read_data");
			break;
		}
	}

	/* Cleanup */
	close(fd);
	ftdi_usb_close(&fcontext);
	ftdi_deinit(&fcontext);
	return 0;
}
