/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define LIGHTBAR "/sys/devices/virtual/chromeos/cros_ec/lightbar"

int main(int argc, char **argv)
{
	int major, minor, fd_v;
	int i, tries, fd_s, fd_l;
	char buf[80];
	int ret = 1;

	/* Check version */
	fd_v = open(LIGHTBAR "/version", O_RDONLY);
	if (fd_v < 0) {
		perror("can't open version file");
		goto out;
	}
	ret = read(fd_v, buf, sizeof(buf) - 1);
	if (ret <= 0) {
		perror("can't read version");
		close(fd_v);
		goto out;
	}
	buf[ret] = '\0';
	close(fd_v);

	errno = 0;
	/* Expect "MAJOR MINOR" */
	if (2 != sscanf(buf, "%d %d", &major, &minor)) {
		if (errno)
			perror("can't parse version string");
		else
			fprintf(stderr, "can't parse version string\n");
		goto out;
	}
	/* Pixel is "0 0". Minor change will be compatible, Major may not */
	if (major != 0) {
		fprintf(stderr, "Don't know how to handle version %d.%d\n",
			major, minor);
		goto out;
	}

	/* Take over lightbar sequencing. */
	fd_s = open(LIGHTBAR "/sequence", O_RDWR | O_SYNC);
	if (fd_s < 0) {
		perror("can't open sequence control");
		goto out;
	}

	/* NOTE: Cooperative locking only. Rude programs may not play nice. */
	if (flock(fd_s, LOCK_EX | LOCK_NB) < 0) {
		perror("can't lock sequence control");
		goto out_close;
	}

	/*
	 * If power events are changing the sequence our request to stop may
	 * be missed, so try a few times before giving up.
	 *
	 * Note that every write to a control file should be prefaced with an
	 * lseek() to the beginning. sysfs files don't work quite like normal
	 * files.
	 */
	tries = 3;
	while (1) {
		lseek(fd_s, 0, SEEK_SET);
		if (read(fd_s, buf, sizeof(buf)) <= 0) {
			perror("can't read sequence control");
			goto out_unlock;
		}

		if (!strncasecmp(buf, "stop", 4))
			break;

		if (!tries--) {
			fprintf(stderr, "couldn't get EC to stop\n");
			goto out_unlock;
		}

		lseek(fd_s, 0, SEEK_SET);
		strcpy(buf, "stop");
		if (write(fd_s, buf, strlen(buf) + 1) <= 0) {
			perror("can't write sequence control");
			goto out_unlock;
		}
	}

	/* Turn the brightness all the way up */
	fd_l = open(LIGHTBAR "/brightness", O_WRONLY | O_SYNC);
	if (fd_l < 0) {
		perror("can't open brightness control");
		goto out_run;
	}
	strcpy(buf, "255");
	if (write(fd_l, buf, strlen(buf) + 1) < 0) {
		perror("can't write brightness control");
		goto out_led;
	}
	close(fd_l);

	/* Now let's drive the colors. */
	fd_l = open(LIGHTBAR "/led_rgb", O_WRONLY | O_SYNC);
	if (fd_l < 0) {
		perror("can't open led control");
		goto out_run;
	}

	/* Cycle through some colors. We can update multiple LEDs at once,
	 * but there's a limit on how often we can send commands to the
	 * lightbar. Going too fast will block, although buffering combined
	 * with lseek() may just cause data to be lost. Read "/interval_msec"
	 * to see what the limit is. The default is 50msec (20Hz).
	 */
	for (i = 0; i < 256; i += 4) {
		sprintf(buf, "0 %d %d %d 1 %d %d %d 2 %d %d %d 3 %d %d %d",
			i, 0, 0,
			0, 0, i,
			255-i, 255, 0,
			0, 255, 255-i);
		lseek(fd_l, 0, SEEK_SET);
		if (write(fd_l, buf, strlen(buf) + 1) < 0)
			perror("write to led control");

		usleep(100000);
	}

	/* all white */
	strcpy(buf, "4 255 255 255");
	lseek(fd_l, 0, SEEK_SET);
	if (write(fd_l, buf, strlen(buf) + 1) < 0)
		perror("write to led control");

	usleep(400000);

	/* Done. */
	ret = 0;
out_led:
	close(fd_l);
out_run:
	/* Let EC drive lightbar again */
	strcpy(buf, "run");
	lseek(fd_s, 0, SEEK_SET);
	if (write(fd_s, buf, strlen(buf) + 1) < 0)
		perror("write to sequence control");
out_unlock:
	flock(fd_s, LOCK_UN);
out_close:
	close(fd_s);
out:
	return ret;
}
