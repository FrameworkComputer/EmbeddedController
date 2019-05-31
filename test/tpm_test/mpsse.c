/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Based on Craig Heffner's version of Dec 27 2011, published on
 * https://github.com/devttys0/libmpsse
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if LIBFTDI1 == 1
#include <libftdi1/ftdi.h>
#else
#include <ftdi.h>
#endif

#include "mpsse.h"
#include "support.h"

/* FTDI interfaces */
enum interface {
	IFACE_ANY = INTERFACE_ANY,
	IFACE_A = INTERFACE_A,
	IFACE_B = INTERFACE_B,
	IFACE_C = INTERFACE_C,
	IFACE_D = INTERFACE_D
};

enum mpsse_commands {
	INVALID_COMMAND = 0xAB,
	ENABLE_ADAPTIVE_CLOCK = 0x96,
	DISABLE_ADAPTIVE_CLOCK = 0x97,
	ENABLE_3_PHASE_CLOCK = 0x8C,
	DISABLE_3_PHASE_CLOCK = 0x8D,
	TCK_X5 = 0x8A,
	TCK_D5 = 0x8B,
	CLOCK_N_CYCLES = 0x8E,
	CLOCK_N8_CYCLES = 0x8F,
	PULSE_CLOCK_IO_HIGH = 0x94,
	PULSE_CLOCK_IO_LOW = 0x95,
	CLOCK_N8_CYCLES_IO_HIGH = 0x9C,
	CLOCK_N8_CYCLES_IO_LOW = 0x9D,
	TRISTATE_IO = 0x9E,
};

/* Common clock rates */
enum clock_rates {
	ONE_HUNDRED_KHZ = 100000,
	FOUR_HUNDRED_KHZ = 400000,
	ONE_MHZ = 1000000,
	TWO_MHZ = 2000000,
	FIVE_MHZ = 5000000,
	SIX_MHZ = 6000000,
	TEN_MHZ = 10000000,
	TWELVE_MHZ = 12000000,
	FIFTEEN_MHZ = 15000000,
	THIRTY_MHZ = 30000000,
	SIXTY_MHZ = 60000000
};

#define NULL_CONTEXT_ERROR_MSG	"NULL MPSSE context pointer!"
#define SPI_TRANSFER_SIZE	512
#define SPI_RW_SIZE		(63 * 1024)
#define SETUP_DELAY		25000
#define LATENCY_MS		2
#define USB_TIMEOUT		120000
#define CHUNK_SIZE		65535
#define MAX_SETUP_COMMANDS	10

/* SK and CS are high, GPIO1 is reset on the FPGA hookup, all others low */
#define DEFAULT_PORT            (SK | CS | GPIO1)
/* SK/DO/CS and GPIOs are outputs, DI is an input */
#define DEFAULT_TRIS            (SK | DO | CS | GPIO0 | GPIO1 | GPIO2 | GPIO3)

static struct vid_pid {
	int vid;
	int pid;
	char *description;
	int use_B;
} supported_devices[] = {
	{
	0x0403, 0x6010, "FT2232 Future Technology Devices International, Ltd",
	1},
	{
	0x0403, 0x6011, "FT4232 Future Technology Devices International, Ltd"},
	{
	0x0403, 0x6014,
		    "FT232H Future Technology Devices International, Ltd"},
	/* These devices are based on FT2232 chips, but have not been tested. */
	{
	0x0403, 0x8878, "Bus Blaster v2 (channel A)"}, {
	0x0403, 0x8879, "Bus Blaster v2 (channel B)"}, {
	0x0403, 0xBDC8, "Turtelizer JTAG/RS232 Adapter A"}, {
	0x0403, 0xCFF8, "Amontec JTAGkey"}, {
	0x0403, 0x8A98, "TIAO Multi Protocol Adapter"}, {
	0x15BA, 0x0003, "Olimex Ltd. OpenOCD JTAG"}, {
	0x15BA, 0x0004, "Olimex Ltd. OpenOCD JTAG TINY"}, {
	0x18d1, 0x0304, "Google UltraDebug", 1}, {
	0, 0, NULL}
};

/*
 * Enables or disables flushing of the FTDI chip's RX buffers after each read
 * operation. Flushing is disable by default.
 *
 * @mpsse - MPSSE context pointer.
 * @tf    - Set to 1 to enable flushing, or 0 to disable flushing.
 *
 * Returns void.
 */
static void FlushAfterRead(struct mpsse_context *mpsse, int tf)
{
	mpsse->flush_after_read = tf;
}

/*
 * Enable / disable internal loopback.
 *
 * @mpsse  - MPSSE context pointer.
 * @enable - Zero to disable loopback, 1 to enable loopback.
 *
 * Returns MPSSE_OK on success.
 * Returns MPSSE_FAIL on failure.
 */
static int SetLoopback(struct mpsse_context *mpsse, int enable)
{
	unsigned char buf[1] = { 0 };
	int retval = MPSSE_FAIL;

	if (is_valid_context(mpsse)) {
		if (enable)
			buf[0] = LOOPBACK_START;
		else
			buf[0] = LOOPBACK_END;

		retval = raw_write(mpsse, buf, 1);
	}

	return retval;
}

/*
 * Sets the appropriate divisor for the desired clock frequency.
 *
 * @mpsse - MPSSE context pointer.
 * @freq  - Desired clock frequency in hertz.
 *
 * Returns MPSSE_OK on success.
 * Returns MPSSE_FAIL on failure.
 */
static int SetClock(struct mpsse_context *mpsse, uint32_t freq)
{
	int retval = MPSSE_FAIL;
	uint32_t system_clock = 0;
	uint16_t divisor = 0;
	unsigned char buf[CMD_SIZE] = { 0 };

	/*
	 * Do not call is_valid_context() here, as the FTDI chip may not be
	 * completely configured when SetClock is called
	 */
	if (!mpsse)
		return retval;

	if (freq > SIX_MHZ) {
		buf[0] = TCK_X5;
		system_clock = SIXTY_MHZ;
	} else {
		buf[0] = TCK_D5;
		system_clock = TWELVE_MHZ;
	}

	if (raw_write(mpsse, buf, 1) == MPSSE_OK) {
		if (freq <= 0)
			divisor = 0xFFFF;
		else
			divisor = freq2div(system_clock, freq);

		buf[0] = TCK_DIVISOR;
		buf[1] = (divisor & 0xFF);
		buf[2] = ((divisor >> 8) & 0xFF);

		if (raw_write(mpsse, buf, 3) == MPSSE_OK) {
			mpsse->clock = div2freq(system_clock, divisor);
			retval = MPSSE_OK;
		}
	}

	return retval;
}

/*
 * Sets the appropriate transmit and receive commands based on the requested
 * mode and byte order.
 *
 * @mpsse     - MPSSE context pointer.
 * @endianness - MPSSE_MSB or MPSSE_LSB.
 *
 * Returns MPSSE_OK on success.
 * Returns MPSSE_FAIL on failure.
 */
static int SetMode(struct mpsse_context *mpsse, int endianness)
{
	int retval = MPSSE_OK, i = 0, setup_commands_size = 0;
	unsigned char buf[CMD_SIZE] = { 0 };
	unsigned char setup_commands[CMD_SIZE * MAX_SETUP_COMMANDS] = { 0 };

	/*
	 * Do not call is_valid_context() here, as the FTDI chip may not be
	 * completely configured when SetMode is called
	 */
	if (!mpsse)
		return MPSSE_FAIL;

	/* Read and write commands need to include endianness */
	mpsse->tx = MPSSE_DO_WRITE | endianness;
	mpsse->rx = MPSSE_DO_READ | endianness;
	mpsse->txrx = MPSSE_DO_WRITE | MPSSE_DO_READ | endianness;

	/*
	 * Clock, data out, chip select pins are outputs; all others are
	 * inputs.
	 */
	mpsse->tris = DEFAULT_TRIS;

	/* Clock and chip select pins idle high; all others are low */
	mpsse->pidle = mpsse->pstart = mpsse->pstop = DEFAULT_PORT;

	/* During reads and writes the chip select pin is brought low */
	mpsse->pstart &= ~CS;

	/* Disable FTDI internal loopback */
	SetLoopback(mpsse, 0);

	/* Ensure adaptive clock is disabled */
	setup_commands[setup_commands_size++] = DISABLE_ADAPTIVE_CLOCK;

	switch (mpsse->mode) {
	case SPI0:
		/* SPI mode 0 clock idles low */
		mpsse->pidle &= ~SK;
		mpsse->pstart &= ~SK;
		mpsse->pstop &= ~SK;

		/*
		 * SPI mode 0 propogates data on the falling edge and read
		 * data on the rising edge of the clock
		 */
		mpsse->tx |= MPSSE_WRITE_NEG;
		mpsse->rx &= ~MPSSE_READ_NEG;
		mpsse->txrx |= MPSSE_WRITE_NEG;
		mpsse->txrx &= ~MPSSE_READ_NEG;
		break;
	default:
		fprintf(stderr, "%s:%d attempt to set an unsupported mode %d\n",
			__func__, __LINE__, mpsse->mode);
		retval = MPSSE_FAIL;
	}

	/* Send any setup commands to the chip */
	if ((retval == MPSSE_OK) && (setup_commands_size > 0))
		retval = raw_write(mpsse, setup_commands, setup_commands_size);

	if (retval == MPSSE_OK) {
		/* Set the idle pin states */
		set_bits_low(mpsse, mpsse->pidle);

		/* All GPIO pins are outputs, set low */
		mpsse->trish = 0xFF;
		mpsse->gpioh = 0x00;

		buf[i++] = SET_BITS_HIGH;
		buf[i++] = mpsse->gpioh;
		buf[i++] = mpsse->trish;

		retval = raw_write(mpsse, buf, i);
	}

	return retval;
}

/*
 * Open device by VID/PID/index
 *
 * @vid         - Device vendor ID.
 * @pid         - Device product ID.
 * @freq        - Clock frequency to use for the specified mode.
 * @endianness   - Specifies how data is clocked in/out (MSB, LSB).
 * @interface   - FTDI interface to use (IFACE_A - IFACE_D).
 * @description - Device product description (set to NULL if not needed).
 * @serial      - Device serial number (set to NULL if not needed).
 * @index       - Device index (set to 0 if not needed).
 *
 * Returns a pointer to an MPSSE context structure.
 * On success, mpsse->open will be set to 1.
 * On failure, mpsse->open will be set to 0.
 */
static struct mpsse_context *OpenIndex(int vid,
				       int pid,
				       int freq,
				       int endianness,
				       int interface,
				       const char *description,
				       const char *serial, int index)
{
	int status = 0;
	struct mpsse_context *mpsse = NULL;
	enum modes mode = SPI0;	/* Let's use this mode at all times. */

	mpsse = malloc(sizeof(struct mpsse_context));
	if (!mpsse)
		return NULL;

	memset(mpsse, 0, sizeof(struct mpsse_context));

	/* Legacy; flushing is no longer needed, so disable it by default. */
	FlushAfterRead(mpsse, 0);

	/* ftdilib initialization */
	if (ftdi_init(&mpsse->ftdi)) {
		fprintf(stderr, "%s:%d failed to initialize FTDI\n",
			__func__, __LINE__);
		free(mpsse);
		return NULL;
	}

	mpsse->ftdi_initialized = 1;

	/* Set the FTDI interface  */
	ftdi_set_interface(&mpsse->ftdi, interface);

	/* Try opening the specified device */
	if (ftdi_usb_open_desc_index
	    (&mpsse->ftdi, vid, pid, description, serial, index)) {
		Close(mpsse);
		return NULL;
	}

	mpsse->mode = mode;
	mpsse->vid = vid;
	mpsse->pid = pid;
	mpsse->status = STOPPED;
	mpsse->endianness = endianness;
	mpsse->xsize = SPI_RW_SIZE;

	status |= ftdi_usb_reset(&mpsse->ftdi);
	status |= ftdi_set_latency_timer(&mpsse->ftdi, LATENCY_MS);
	status |= ftdi_write_data_set_chunksize(&mpsse->ftdi, CHUNK_SIZE);
	status |= ftdi_read_data_set_chunksize(&mpsse->ftdi, CHUNK_SIZE);
	status |= ftdi_set_bitmode(&mpsse->ftdi, 0, BITMODE_RESET);

	if (status) {
		fprintf(stderr,
			"%s:%d failed setting basic config for %4.4x:%4.4x\n",
			__func__, __LINE__, vid, pid);
		Close(mpsse);
		return NULL;
	}
	/* Set the read and write timeout periods */
	set_timeouts(mpsse, USB_TIMEOUT);

	ftdi_set_bitmode(&mpsse->ftdi, 0, BITMODE_MPSSE);

	if ((SetClock(mpsse, freq) != MPSSE_OK)
	    || (SetMode(mpsse, endianness) != MPSSE_OK)) {
		fprintf(stderr,
			"%s:%d failed setting clock/mode for %4.4x:%4.4x\n",
			__func__, __LINE__, vid, pid);
		Close(mpsse);
		return NULL;
	}

	mpsse->open = 1;

	/* Give the chip a few mS to initialize */
	usleep(SETUP_DELAY);

	/*
	 * Not all FTDI chips support all the commands that SetMode may have
	 * sent. This clears out any errors from unsupported commands that
	 * might have been sent during set up.
	 */
	ftdi_usb_purge_buffers(&mpsse->ftdi);

	return mpsse;
}

/*
 * Opens and initializes the first FTDI device found.
 *
 * @freq      - Clock frequency to use for the specified mode.
 * @endianness - Specifies how data is clocked in/out (MSB, LSB).
 * @serial    - Serial number of the USB device (NULL if not needed).
 *
 * Returns a pointer to an MPSSE context structure.
 * On success, mpsse->open will be set to 1.
 * On failure, mpsse->open will be set to 0.
 */
struct mpsse_context *MPSSE(int freq, int endianness, const char *serial)
{
	int i = 0;
	struct mpsse_context *mpsse = NULL;

	for (i = 0; supported_devices[i].vid != 0; i++) {
		mpsse = OpenIndex(supported_devices[i].vid,
				  supported_devices[i].pid, freq, endianness,
				  supported_devices[i].use_B ?
					IFACE_B : IFACE_A,
				  NULL, serial, 0);
		if (!mpsse)
			continue;

		if (mpsse->open) {
			mpsse->description = supported_devices[i].description;
			break;
		}
		/*
		 * If there is another device still left to try, free
		 * the context pointer and try again
		 */
		if (supported_devices[i + 1].vid != 0) {
			Close(mpsse);
			mpsse = NULL;
		}
	}

	return mpsse;
}

/*
 * Closes the device, deinitializes libftdi, and frees the MPSSE context
 * pointer.
 *
 * @mpsse - MPSSE context pointer.
 *
 * Returns void.
 */

void Close(struct mpsse_context *mpsse)
{
	if (!mpsse)
		return;

	if (mpsse->open) {
		ftdi_usb_close(&mpsse->ftdi);
		ftdi_set_bitmode(&mpsse->ftdi, 0, BITMODE_RESET);
	}

	if (mpsse->ftdi_initialized)
		ftdi_deinit(&mpsse->ftdi);

	free(mpsse);
}

/*
 * Retrieves the last error string from libftdi.
 *
 * @mpsse - MPSSE context pointer.
 *
 * Returns a pointer to the last error string.
 */
const char *ErrorString(struct mpsse_context *mpsse)
{
	if (mpsse)
		return ftdi_get_error_string(&mpsse->ftdi);

	return NULL_CONTEXT_ERROR_MSG;
}

/*
 * Send data start condition.
 *
 * @mpsse - MPSSE context pointer.
 *
 * Returns MPSSE_OK on success.
 * Returns MPSSE_FAIL on failure.
 */
int Start(struct mpsse_context *mpsse)
{
	int status;

	if (!is_valid_context(mpsse)) {
		mpsse->status = STOPPED;
		return MPSSE_FAIL;
	}

	/* Set the start condition */
	status = set_bits_low(mpsse, mpsse->pstart);

	if (status == MPSSE_OK)
		mpsse->status = STARTED;

	return status;
}

/*
 * Send data out via the selected serial protocol.
 *
 * @mpsse - MPSSE context pointer.
 * @data  - Buffer of data to send.
 * @size  - Size of data.
 *
 * Returns MPSSE_OK on success.
 * Returns MPSSE_FAIL on failure.
 */
int Write(struct mpsse_context *mpsse, char *data, int size)
{
	int n = 0;

	if (!is_valid_context(mpsse))
		return MPSSE_FAIL;

	if (!mpsse->mode)
		return MPSSE_FAIL;

	while (n < size) {
		unsigned char *buf;
		int retval, buf_size, txsize;

		txsize = size - n;
		if (txsize > mpsse->xsize)
			txsize = mpsse->xsize;

		buf = build_block_buffer(mpsse, mpsse->tx,
					 (unsigned char *)(data + n),
					 txsize, &buf_size);
		if (!buf)
			return MPSSE_FAIL;

		retval = raw_write(mpsse, buf, buf_size);
		n += txsize;
		free(buf);

		if (retval != MPSSE_OK)
			return retval;

	}

	return MPSSE_OK;
}

/* Performs a read. For internal use only; see Read() and ReadBits(). */
static char *InternalRead(struct mpsse_context *mpsse, int size)
{
	unsigned char *buf;
	int n = 0;

	if (!is_valid_context(mpsse))
		return NULL;

	if (!mpsse->mode)
		return NULL;
	buf = malloc(size);

	if (!buf)
		return NULL;

	while (n < size) {
		int rxsize, data_size, retval;
		unsigned char *data;
		unsigned char sbuf[SPI_RW_SIZE] = { 0 };

		rxsize = size - n;
		if (rxsize > mpsse->xsize)
			rxsize = mpsse->xsize;

		data = build_block_buffer(mpsse, mpsse->rx,
					  sbuf, rxsize, &data_size);
		if (!data) {
			free(buf);
			return NULL;
		}

		retval = raw_write(mpsse, data, data_size);
		free(data);

		if (retval != MPSSE_OK) {
			free(buf);
			return NULL;
		}
		n += raw_read(mpsse, buf + n, rxsize);
	}

	return (char *)buf;
}

/*
 * Reads data over the selected serial protocol.
 *
 * @mpsse - MPSSE context pointer.
 * @size  - Number of bytes to read.
 *
 * Returns a pointer to the read data on success.
 * Returns NULL on failure.
 */
char *Read(struct mpsse_context *mpsse, int size)
{
	char *buf = NULL;

	buf = InternalRead(mpsse, size);
	return buf;
}

/*
 * Reads and writes data over the selected serial protocol (SPI only).
 *
 * @mpsse - MPSSE context pointer.
 * @data  - Buffer containing bytes to write.
 * @size  - Number of bytes to transfer.
 *
 * Returns a pointer to the read data on success.
 * Returns NULL on failure.
 */
char *Transfer(struct mpsse_context *mpsse, char *data, int size)
{
	unsigned char *txdata = NULL, *buf = NULL;
	int n = 0, data_size = 0, rxsize = 0, retval = MPSSE_OK;

	if (!is_valid_context(mpsse))
		return NULL;

	buf = malloc(size);
	if (!buf)
		return NULL;

	while (n < size) {
		/*
		 * When sending and receiving, FTDI chips don't seem to like
		 * large data blocks. Limit the size of each block to
		 * SPI_TRANSFER_SIZE
		 */
		rxsize = size - n;
		if (rxsize > SPI_TRANSFER_SIZE)
			rxsize = SPI_TRANSFER_SIZE;

		txdata = build_block_buffer(mpsse, mpsse->txrx,
					    (unsigned char *)(data + n),
					    rxsize, &data_size);
		if (!txdata) {
			retval = MPSSE_FAIL;
			break;
		}
		retval = raw_write(mpsse, txdata, data_size);
		free(txdata);

		if (retval != MPSSE_OK)
			break;

		n += raw_read(mpsse, (buf + n), rxsize);
	}

	if (retval != MPSSE_OK)
		return NULL;

	return (char *)buf;
}

/*
 * Send data stop condition.
 *
 * @mpsse - MPSSE context pointer.
 *
 * Returns MPSSE_OK on success.
 * Returns MPSSE_FAIL on failure.
 */
int Stop(struct mpsse_context *mpsse)
{
	int retval = MPSSE_OK;

	if (is_valid_context(mpsse)) {
		/* Send the stop condition */
		retval |= set_bits_low(mpsse, mpsse->pstop);

		if (retval == MPSSE_OK) {
			/* Restore the pins to their idle states */
			retval |= set_bits_low(mpsse, mpsse->pidle);
		}

		mpsse->status = STOPPED;
	} else {
		retval = MPSSE_FAIL;
		mpsse->status = STOPPED;
	}

	return retval;
}

/*
 * Sets the specified pin high.
 *
 * @mpsse - MPSSE context pointer.
 * @pin   - Pin number to set high.
 *
 * Returns MPSSE_OK on success.
 * Returns MPSSE_FAIL on failure.
 */
int PinHigh(struct mpsse_context *mpsse, int pin)
{
	int retval = MPSSE_FAIL;

	if (is_valid_context(mpsse))
		retval = gpio_write(mpsse, pin, HIGH);

	return retval;
}

/*
 * Sets the specified pin low.
 *
 * @mpsse - MPSSE context pointer.
 * @pin   - Pin number to set low.
 *
 * Returns MPSSE_OK on success.
 * Returns MPSSE_FAIL on failure.
 */
int PinLow(struct mpsse_context *mpsse, int pin)
{
	int retval = MPSSE_FAIL;

	if (is_valid_context(mpsse))
		retval = gpio_write(mpsse, pin, LOW);

	return retval;
}
