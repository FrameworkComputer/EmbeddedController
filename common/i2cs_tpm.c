/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2cs.h"
#include "registers.h"
#include "system.h"
#include "tpm_registers.h"

/*
 * This implements adaptaition layer between i2cs (i2c slave) port and TPM.
 *
 * The adaptation layer is stateless, it processes the i2cs "write complete"
 * interrupts on the interrupt context.
 *
 * Each "write complete" interrupt is associated with some data receved from
 * the master. If the package received from the master contains just one byte
 * payload, the value of this byte is considered the address of the TPM2
 * register to reach, read or write.
 *
 * Real TPM register addresses can be two bytes in size (even within locality
 * zero), to keep the i2c protocol simple and efficient, the real TPM register
 * addresses are re-mapped into i2c specific TPM register addresses.
 *
 * If the payload includes bytes following the address byte - those are the
 * data to be written to the addressed register. The number of bytes of data
 * could be anything between 1 and 62. The HW fifo is 64 bytes deep and that
 * means that only 63 bytes can be written without the write pointer wrapping
 * around to itself. Outside of the TPM fifo register, all other registers are
 * either 1 byte or 4 byte writes.
 *
 * The master knows how many bytes to write into FIFO or to read from it by
 * consulting the "burst size" field of the TPM status register. This happens
 * transparently for this layer.
 *
 * Data destined to and coming from the FIFO register is treated as a byte
 * stream.
 *
 * Data for and from all other registers are either 1 byte or 4 bytes as
 * specified in a register's "reg_size" field of the I2C -> TPM mapping
 * table. Multi-byte registers are received and transmitted in CPU byte order
 * which for the Cr50 is little endian.
 * TODO (scollyer crosbug.com/p/56539): Should modify the register access code
 * so that the Host can access 1-4 bytes of a given register.
 *
 * Master write accesses followed by data result in the register address
 * mapped, data converted, if necessary, and passed to the tpm register task.
 *
 * Master write accesses requesting register reads result in the register
 * address mappend and accessing the tpm task to retrieve the proper register
 * data, converting it, if necessary, and passing it to the 12cs controller to
 * make available for master read accesses.
 *
 * Again, both read and write accesses complete on the same interrupt context
 * they were invoked on.
 */

/* Console output macros */
#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)

struct i2c_tpm_reg_map {
	uint8_t   i2c_address;
	uint8_t   reg_size;
	uint16_t  tpm_address;
};
static const struct i2c_tpm_reg_map i2c_to_tpm[] = {
	{0, 1, 0},	 /* TPM Access */
	{1, 4, 0x18},	 /* TPM Status */
	{5, 0, 0x24},	 /* TPM Fifo, variable size. */
	{6, 4, 0xf00},   /* TPM DID VID */
	{0xa, 4, 0x14},  /* TPM TPM_INTF_CAPABILITY */
	{0xe, 1, 0xf04}, /* TPM RID */
	{0xf, 0, 0xf90}, /* TPM_FW_VER */
};

/* Used to track number of times i2cs hw read fifo was adjusted */
static uint32_t i2cs_fifo_adjust_count;
/* Used to track number of write mismatch errors */
static uint32_t i2cs_write_error_count;

static void process_read_access(uint16_t reg_size,
				uint16_t tpm_reg, uint8_t *data)
{
	int i;
	uint8_t reg_value[4];

	/*
	 * The master wants to read the register, read the value and pass it
	 * to the controller.
	 */
	if (reg_size == 1 || reg_size == 4) {
		/* Always read regsize number of bytes */
		tpm_register_get(tpm_reg, reg_value, reg_size);
		/*
		 * For 1 or 4 byte register reads there should not be any data
		 * buffered in the i2cs hw read fifo. This function will check
		 * the current fifo queue depth and if non-zero, will adjust the
		 * fw pointer to force it to 0.
		 */
		if (i2cs_zero_read_fifo_buffer_depth())
			/* Count each instance that fifo was adjusted */
			i2cs_fifo_adjust_count++;
		for (i = 0; i < reg_size; i++)
			i2cs_post_read_data(reg_value[i]);
		return;
	}

	/*
	 * FIFO accesses do not require endianness conversion, but to find out
	 * how many bytes to read we need to consult the burst size field of
	 * the tpm status register.
	 */
	reg_size = tpm_get_burst_size();

	/*
	 * Now, this is a hack, but we are short on SRAM, so let's reuse the
	 * receive buffer for the FIFO data sotrage. We know that the ISR has
	 * a 64 byte buffer were it moves received data.
	 */
	/* Back pointer up by one to point to beginning of buffer */
	data -= 1;
	tpm_register_get(tpm_reg, data, reg_size);
	/* Transfer TPM fifo data to the I2CS HW fifo */
	i2cs_post_read_fill_fifo(data, reg_size);
}

static void process_write_access(uint16_t reg_size, uint16_t tpm_reg,
				 uint8_t *data, size_t i2cs_data_size)
{
	/* This is an actual write request. */

	/*
	 * If reg_size is 0, then this is a fifo register write. Send the stream
	 * down directly
	 */
	if (reg_size == 0) {
		tpm_register_put(tpm_reg, data, i2cs_data_size);
		return;
	}

	if (i2cs_data_size != reg_size) {
		i2cs_write_error_count++;
		return;
	}

	/* Write the data to the appropriate TPM register */
	tpm_register_put(tpm_reg, data, reg_size);
}

static void wr_complete_handler(void *i2cs_data, size_t i2cs_data_size)
{
	size_t i;
	uint16_t tpm_reg;
	uint8_t *data = i2cs_data;
	const struct i2c_tpm_reg_map *i2c_reg_entry = NULL;
	uint16_t reg_size;

	if (i2cs_data_size < 1) {
		/*
		 * This is a misformatted request, should never happen, just
		 * ignore it.
		 */
		CPRINTF("%s: empty receive payload\n", __func__);
		return;
	}

	/* Let's find real TPM register address. */
	for (i = 0; i < ARRAY_SIZE(i2c_to_tpm); i++)
		if (i2c_to_tpm[i].i2c_address == *data) {
			i2c_reg_entry = i2c_to_tpm + i;
			break;
		}

	if (!i2c_reg_entry) {
		CPRINTF("%s: unsupported i2c tpm address 0x%x\n",
			__func__, *data);
		return;
	}

	/*
	 * OK, we know the tpm register address. Note that only full register
	 * accesses are supported for multybyte registers,
	 * TODO (scollyer crosbug.com/p/56539): Look at modifying this so we
	 * can handle 1 - 4 byte accesses at any any I2C register address we
	 * support.
	 */
	tpm_reg = i2c_reg_entry->tpm_address;
	reg_size = i2c_reg_entry->reg_size;

	i2cs_data_size--;
	data++;

	if (!i2cs_data_size)
		process_read_access(reg_size, tpm_reg, data);
	else
		process_write_access(reg_size, tpm_reg,
				     data, i2cs_data_size);

	/*
	 * Since cr50 does not provide i2c clock stretching, we need some
	 * onther means of flow controlling the host. Let's generate a pulse
	 * on the AP interrupt line for that.
	 */
	gpio_set_level(GPIO_INT_AP_L, 0);
	gpio_set_level(GPIO_INT_AP_L, 1);
}

static void i2cs_if_stop(void)
{
	i2cs_register_write_complete_handler(NULL);
}

static void i2cs_if_start(void)
{
	i2cs_register_write_complete_handler(wr_complete_handler);
}

static void i2cs_if_register(void)
{
	if (!board_tpm_uses_i2c())
		return;

	tpm_register_interface(i2cs_if_start, i2cs_if_stop);
	i2cs_fifo_adjust_count = 0;
	i2cs_write_error_count = 0;
}
DECLARE_HOOK(HOOK_INIT, i2cs_if_register, HOOK_PRIO_LAST);

static int command_i2cs(int argc, char **argv)
{
	static uint16_t base_read_recovery_count;
	struct i2cs_status status;

	i2cs_get_status(&status);

	ccprintf("rd fifo adjust cnt = %d\n", i2cs_fifo_adjust_count);
	ccprintf("wr mismatch cnt = %d\n", i2cs_write_error_count);
	ccprintf("read recovered cnt = %d\n", status.read_recovery_count
		 - base_read_recovery_count);
	if (argc < 2)
		return EC_SUCCESS;

	if (!strcasecmp(argv[1], "reset")) {
		i2cs_fifo_adjust_count = 0;
		i2cs_write_error_count = 0;
		base_read_recovery_count = status.read_recovery_count;
		ccprintf("i2cs error counts reset\n");
	} else
		return EC_ERROR_PARAM1;

	return EC_SUCCESS;
}
DECLARE_SAFE_CONSOLE_COMMAND(i2cstpm, command_i2cs,
			     "reset",
			     "Display fifo adjust count");
