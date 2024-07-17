/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EMUL_TPS6699X_PRIVATE_H_
#define __EMUL_TPS6699X_PRIVATE_H_

#include "drivers/ucsi_v3.h"
#include "emul/emul_tps6699x.h"

#include <stdint.h>

#include <zephyr/drivers/gpio.h>

enum tps6699x_reg_offset {
	/* TODO(b/345292002): Fill out */
	TPS6699X_REG_COMMAND_I2C1 = 0x8,
	TPS6699X_REG_DATA_I2C1 = 0x9,
	TPS6699X_NUM_REG = 0xa4,
};

/* This recapitulates enum command_task from tps6699x_reg.h, but in so doing, it
 * avoids sharing implementation between the driver and the emulator used to
 * test it.
 */
enum tps6699x_command_task {
	/* Command complete: Not a real command. The TPS6699x clears the command
	 * register when a command completes.
	 */
	COMMAND_TASK_COMPLETE = 0,
	/* Invalid command */
	COMMAND_TASK_NO_COMMAND = 0x444d4321,
	/* Cold reset request */
	COMMAND_TASK_GAID = 0x44494147,
	/* Simulate port disconnect */
	COMMAND_TASK_DISC = 0x43534944,
	/* PD PR_Swap to Sink */
	COMMAND_TASK_SWSK = 0x6b535753,
	/* PD PR_Swap to Source */
	COMMAND_TASK_SWSR = 0x72535753,
	/* PD DR_Swap to DFP */
	COMMAND_TASK_SWDF,
	/* PD DR_Swap to UFP */
	COMMAND_TASK_SWUF,
	/* PD Get Sink Capabilities */
	COMMAND_TASK_GSKC,
	/* PD Get Source Capabilities */
	COMMAND_TASK_GSRC,
	/* PD Get Port Partner Information */
	COMMAND_TASK_GPPI,
	/* PD Send Source Capabilities */
	COMMAND_TASK_SSRC,
	/* PD Data Reset */
	COMMAND_TASK_DRST,
	/* Message Buffer Read */
	COMMAND_TASK_MBRD,
	/* Send Alert Message */
	COMMAND_TASK_ALRT,
	/* PD Send Enter Mode */
	COMMAND_TASK_AMEN,
	/* PD Send Exit Mode */
	COMMAND_TASK_AMEX,
	/* PD Start Alternate Mode Discovery */
	COMMAND_TASK_AMDS,
	/* Get Custom Discovered Modes */
	COMMAND_TASK_GCDM,
	/* PD Send VDM */
	COMMAND_TASK_VDMS,
	/* System ready to enter sink power */
	COMMAND_TASK_SRDY = 0x59445253,
	/* SRDY reset */
	COMMAND_TASK_SRYR = 0x52595253,
	/* Firmware update tasks */
	COMMAND_TASK_TFUS = 0x73554654,
	COMMAND_TASK_TFUC = 0x63554654,
	COMMAND_TASK_TFUD = 0x64554654,
	COMMAND_TASK_TFUE = 0x65554654,
	COMMAND_TASK_TFUI = 0x69554654,
	COMMAND_TASK_TFUQ = 0x71554654,
	/* Abort current task */
	COMMAND_TASK_ABRT,
	/*Auto Negotiate Sink Update */
	COMMAND_TASK_ANEG,
	/* Clear Dead Battery Flag */
	COMMAND_TASK_DBFG,
	/* Error handling for I2C3m transactions */
	COMMAND_TASK_MUXR,
	/* Trigger an Input GPIO Event */
	COMMAND_TASK_TRIG,
	/* I2C read transaction */
	COMMAND_TASK_I2CR,
	/* I2C write transaction */
	COMMAND_TASK_I2CW,
	/* UCSI tasks */
	COMMAND_TASK_UCSI = 0x49534355,
};

/* Results of a task, indicated by the PDC in byte 1 of the relevant DATAX
 * register after a command completes. See TPS6699x TRM May 2023, table 10-1
 * Standard Task Response.
 */
enum tps6699x_command_result {
	COMMAND_RESULT_SUCCESS = 0,
	COMMAND_RESULT_TIMEOUT = 1,
	COMMAND_RESULT_REJECTED = 2,
	COMMAND_RESULT_RX_LOCKED = 4,
};

#endif /* __EMUL_TPS6699X_PRIVATE_H_ */
