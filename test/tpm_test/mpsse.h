/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Based on Craig Heffner's version of Dec 27 2011, published on
 * https://github.com/devttys0/libmpsse
 */

#ifndef __EC_TEST_TPM_TEST_MPSSE_H
#define __EC_TEST_TPM_TEST_MPSSE_H

#define MPSSE_OK		0
#define MPSSE_FAIL		-1

#define MSB			0x00
#define LSB			0x08

enum gpio_pins {
	GPIOL0 = 0,
	GPIOL1 = 1,
	GPIOL2 = 2,
	GPIOL3 = 3,
	GPIOH0 = 4,
	GPIOH1 = 5,
	GPIOH2 = 6,
	GPIOH3 = 7,
	GPIOH4 = 8,
	GPIOH5 = 9,
	GPIOH6 = 10,
	GPIOH7 = 11
};

struct mpsse_context;

int Write(struct mpsse_context *mpsse, char *data, int size);
int Stop(struct mpsse_context *mpsse);
char *Transfer(struct mpsse_context *mpsse, char *data, int size);
char *Read(struct mpsse_context *mpsse, int size);
struct mpsse_context *MPSSE(int freq, int endianness, const char *serial);
void Close(struct mpsse_context *mpsse);
int PinHigh(struct mpsse_context *mpsse, int pin);
int PinLow(struct mpsse_context *mpsse, int pin);
int Start(struct mpsse_context *mpsse);

#endif  /* ! __EC_TEST_TPM_TEST_MPSSE_H */
