/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Based on Craig Heffner's version of Dec 27 2011, published on
 * https://github.com/devttys0/libmpsse
 */

#ifndef __EC_TEST_TPM_TEST_SUPPORT_H
#define __EC_TEST_TPM_TEST_SUPPORT_H

#include "mpsse.h"

#define CMD_SIZE		3
#define NUM_GPIOL_PINS		4
#define NUM_GPIO_PINS		12
#define LOW			0
#define HIGH			1

/* Supported MPSSE modes */
enum modes {
	SPI0 = 1,
	SPI1 = 2,
	SPI2 = 3,
	SPI3 = 4,
	I2C = 5,
	GPIO = 6,
	BITBANG = 7,
};

enum low_bits_status {
	STARTED,
	STOPPED
};

enum pins {
	SK = 1,
	DO = 2,
	DI = 4,
	CS = 8,
	GPIO0 = 16,
	GPIO1 = 32,
	GPIO2 = 64,
	GPIO3 = 128
};

struct mpsse_context {
	char *description;
	struct ftdi_context ftdi;
	enum modes mode;
	enum low_bits_status status;
	int flush_after_read;
	int vid;
	int pid;
	int clock;
	int xsize;
	int open;
	int ftdi_initialized;
	int endianness;
	uint8_t tris;
	uint8_t pstart;
	uint8_t pstop;
	uint8_t pidle;
	uint8_t gpioh;
	uint8_t trish;
	uint8_t bitbang;
	uint8_t tx;
	uint8_t rx;
	uint8_t txrx;
	uint8_t tack;
	uint8_t rack;
};

int raw_write(struct mpsse_context *mpsse, unsigned char *buf, int size);
int raw_read(struct mpsse_context *mpsse, unsigned char *buf, int size);
void set_timeouts(struct mpsse_context *mpsse, int timeout);
uint16_t freq2div(uint32_t system_clock, uint32_t freq);
uint32_t div2freq(uint32_t system_clock, uint16_t div);
unsigned char *build_block_buffer(struct mpsse_context *mpsse,
				  uint8_t cmd,
				  unsigned char *data, int size, int *buf_size);
int set_bits_high(struct mpsse_context *mpsse, int port);
int set_bits_low(struct mpsse_context *mpsse, int port);
int gpio_write(struct mpsse_context *mpsse, int pin, int direction);
int is_valid_context(struct mpsse_context *mpsse);

#endif  /* ! __EC_TEST_TPM_TEST_SUPPORT_H */
