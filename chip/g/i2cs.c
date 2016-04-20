/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This is a driver for the I2C Slave controller (i2cs) of the g chip.
 *
 * The controller is has two register files, 64 bytes each, one for storing
 * data received from the master, and one for storing data to be transmitted
 * to the master. Both files are accessed only as 4 byte entities, so the
 * driver must provide adaptation to concatenate messages with sizes not
 * divisible by 4.
 *
 * The file holding data written by the master has associated with it a
 * register showing where the controller accessed the file last, which tells
 * the driver how many bytes written by the master are there.
 *
 * The file holding data to be read by the master has a register associtated
 * with it showing where was the latest BIT the controller transmitted.
 *
 * The controller can generate interrupts on three different conditions:
 *  - beginning of a read cycle
 *  - end of a read cycle
 *  - end of a write cycle
 *
 * Since this driver's major role is to serve as a TPM interface, it is safe
 * to assume that the master will always write first, even when it needs to
 * read data from the device.
 *
 * Each write or read access will be started by the master writing the two
 * byte address of the TPM register to access.
 *
 * If the master needs to read this register, the originating write
 * transaction will be limited to a two bytes payload, a read transaction
 * would follow immediately.
 *
 * If the master needs to write this register, the data to be written will be
 * included in the same i2c transaction immediately following the two byte
 * address.
 *
 * This protocol allows to keep the driver simple: the only interrupt the
 * driver enables is the 'end a write cycle'. The number of bytes received
 * from the master gives this driver a hint as of what the master intention
 * is, to read or to write.
 *
 * In both cases the same callback function is called. On write accesses the
 * callback function converts the data as necessary and passes it to the TPM.
 * On read accesses the callback function retrieves data from the TPM and puts
 * it into the read register file to be available to the master to retrieve in
 * the following read access. In both cases the callback function completes
 * processing on the invoking interrupt context.
 *
 * The driver API consists of two functions, one to register the callback to
 * process interrupts, another one - to add a byte to the master read register
 * file. See the accompanying .h file for details.
 *
 * TODO:
 * - figure out flow control - clock stretching can be challenging with this
 *   controller.
 * - transferring data exceeding 64 bytes in size (accessing TPM FIFO).
 * - detect and revover overflow/underflow situations
 */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "i2cs.h"
#include "pmu.h"
#include "registers.h"
#include "task.h"

#define REGISTER_FILE_SIZE (1 << 6) /* 64 bytes. */
#define REGISTER_FILE_MASK (REGISTER_FILE_SIZE - 1)

/* Console output macros */
#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTF(format, args...) cprints(CC_I2C, format, ## args)

/* Pointer to the function to invoke on the write complete interrupts. */
static wr_complete_handler_f write_complete_handler_;

/* A buffer to normalize the received data to pass it to the user. */
static uint8_t i2cs_buffer[64];

/*
 * Pointer where the CPU stopped retrieving the write data sent by the master
 * last time the write access was processed.
 */
static uint16_t last_write_pointer;

/*
 * Pointer where the CPU stopped writing data for the master to read last time
 * the read data was prepared.
 */
static uint16_t last_read_pointer;

static void i2cs_init(void)
{
	/* First decide if i2c is even needed for this platform. */
	/* if (i2cs is not needed) return; */
	return; /* Let's not do anything yet. */

	pmu_clock_en(PERIPH_I2CS);

	/*
	 * i2cs function has been already configured in the gpio.inc table,
	 * here just enable pull ups on both signals. TODO(vbendeb): consider
	 * adjusting pull strength.
	 */
	GWRITE_FIELD(PINMUX, DIOB0_CTL, PU, 1);
	GWRITE_FIELD(PINMUX, DIOB1_CTL, PU, 1);

	GWRITE_FIELD(I2CS, INT_ENABLE, INTR_WRITE_COMPLETE, 1);

	/* Slave address is hardcoded to 0x50. */
	GWRITE(I2CS, SLAVE_DEVADDRVAL, 0x50);
}
DECLARE_HOOK(HOOK_INIT, i2cs_init, HOOK_PRIO_DEFAULT);

/* Process the 'end of a write cycle' interrupt. */
static void _i2cs_write_complete_int(void)
{
	if (write_complete_handler_) {
		uint16_t bytes_written;
		uint16_t bytes_processed;
		uint32_t word_in_value;

		/* How many bytes has the master just written. */
		bytes_written = ((uint16_t)GREAD(I2CS, WRITE_PTR) -
				 last_write_pointer) & REGISTER_FILE_MASK;

		/* How many have been processed yet. */
		bytes_processed = 0;

		/* Make sure we start with something. */
		if (last_write_pointer & 3)
			word_in_value = *(GREG32_ADDR(I2CS, WRITE_BUFFER0) +
					  (last_write_pointer >> 2));
		while (bytes_written != bytes_processed) {
			/*
			 * This loop iterates over bytes retrieved from the
			 * master write register file in 4 byte entities. Each
			 * time the ever incrementing last_write_pointer is
			 * aligned at 4 bytes, a new value needs to be
			 * retrieved from the next register, indexed by
			 * last_write_pointer/4.
			 */

			if (!(last_write_pointer & 3))
				/* Time to get a new value. */
				word_in_value = *(GREG32_ADDR(
					I2CS, WRITE_BUFFER0) +
						  (last_write_pointer >> 2));

			/* Save the next byte in the adaptation buffer. */
			i2cs_buffer[bytes_processed] =
				word_in_value >> (8 * (last_write_pointer & 3));

			/* The pointer wraps at the register file size. */
			last_write_pointer = (last_write_pointer + 1) &
				REGISTER_FILE_MASK;
			bytes_processed++;
		}

		/* Invoke the callback to process the message. */
		write_complete_handler_(i2cs_buffer, bytes_processed);
	}

	/* Reset the IRQ condition. */
	GWRITE_FIELD(I2CS, INT_STATE, INTR_WRITE_COMPLETE, 1);
}
DECLARE_IRQ(GC_IRQNUM_I2CS0_INTR_WRITE_COMPLETE_INT,
	    _i2cs_write_complete_int, 1);

void i2cs_post_read_data(uint8_t byte_to_read)
{
	volatile uint32_t *value_addr;
	uint32_t word_out_value;
	uint32_t shift;

	/*
	 * Find out which register of the register file the byte needs to go
	 * to.
	 */
	value_addr = GREG32_ADDR(I2CS, READ_BUFFER0) + (last_read_pointer >> 2);

	/* Read-modify-write the register adding the new byte there. */
	word_out_value = *value_addr;
	shift = (last_read_pointer & 3) * 8;
	word_out_value = (word_out_value & ~(0xff << shift)) |
		(((uint32_t)byte_to_read) << shift);
	*value_addr = word_out_value;
	last_read_pointer = (last_read_pointer + 1) & REGISTER_FILE_MASK;
}

int i2cs_register_write_complete_handler(wr_complete_handler_f wc_handler)
{
	if (write_complete_handler_)
		return -1;

	write_complete_handler_ = wc_handler;
	task_enable_irq(GC_IRQNUM_I2CS0_INTR_WRITE_COMPLETE_INT);

	return 0;
}
