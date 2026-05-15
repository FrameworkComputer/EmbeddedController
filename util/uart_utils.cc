/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Helper routines for interfacing with COM ports from  EC vendor
 * programming tools.
 */

#include "uart_utils.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

int uart_flush(int fd)
{
	fd_set read_fds;
	struct timeval timeout;
	char buf[256];
	int cc;
	int discard_bytes = 0;

	/* First ask the kernel to drop any data. */
	tcflush(fd, TCIOFLUSH);

	/*
	 * For devices where the above is not properly implemented, we
	 * additionally attempt to manually drain any buffered data below, by
	 * repeatedly reading and discarding, for as long as more data remains
	 * available.  We could have used a zero timeout, but instead chose a
	 * very short timeout of 1ms, just to be sure that the kernel actually
	 * queries the USB device, rather than maybe instantly replying if no
	 * data is buffered in the kernel driver.
	 */
	for (;;) {
		FD_ZERO(&read_fds);
		FD_SET(fd, &read_fds);
		timeout.tv_sec = 0;
		timeout.tv_usec = 1000;
		/*
		 * Ask the operating system to wait up to 1ms for data to become
		 * available to read from the serial port.
		 */
		cc = select(fd + 1, &read_fds, NULL, NULL, &timeout);
		if (cc < 0) {
			fprintf(stderr, "Error from select(): %s\n",
				strerror(errno));
			break;
		}
		if (!cc || !FD_ISSET(fd, &read_fds)) {
			/* No more data immediately available */
			break;
		}
		/*
		 * Select indicated that data is available to read, get whatever
		 * we can, discard it, and then go back and ask if there is
		 * more.
		 */
		cc = read(fd, buf, sizeof(buf));
		if (cc < 0) {
			fprintf(stderr, "Error reading serial data: %s\n",
				strerror(errno));
			break;
		} else if (cc == 0) {
			/* We already checked above that there is data in the
			 * serial port. If we don't get any bytes from read(),
			 * then EOF has been reached indicating the device
			 * disconnected.
			 */
			break;
		} else {
			discard_bytes += cc;
		}
	}

	return discard_bytes;
}
