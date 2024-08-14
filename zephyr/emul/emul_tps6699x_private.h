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

/* Helper function to convert TI task names to UINT32*/
#define TASK_TO_UINT32(a, b, c, d) \
	((uint32_t)(a | (b << 8) | (c << 16) | (d << 24)))

enum tps6699x_command_task {
	/* Command complete: Not a real command. The TPS6699x clears the command
	 * register when a command completes.
	 */
	COMMAND_TASK_COMPLETE = 0,
	/* Invalid command */
	COMMAND_TASK_NO_COMMAND = TASK_TO_UINT32('!', 'C', 'M', 'D'),
	/* Cold reset request */
	COMMAND_TASK_GAID = TASK_TO_UINT32('G', 'A', 'I', 'D'),
	/* Simulate port disconnect */
	COMMAND_TASK_DISC = TASK_TO_UINT32('D', 'I', 'S', 'C'),
	/* PD PR_Swap to Sink */
	COMMAND_TASK_SWSK = TASK_TO_UINT32('S', 'W', 'S', 'k'),
	/* PD PR_Swap to Source */
	COMMAND_TASK_SWSR = TASK_TO_UINT32('S', 'W', 'S', 'r'),
	/* PD DR_Swap to DFP */
	COMMAND_TASK_SWDF = TASK_TO_UINT32('S', 'W', 'D', 'F'),
	/* PD DR_Swap to UFP */
	COMMAND_TASK_SWUF = TASK_TO_UINT32('S', 'W', 'U', 'F'),
	/* PD Get Sink Capabilities */
	COMMAND_TASK_GSKC = TASK_TO_UINT32('G', 'S', 'k', 'C'),
	/* PD Get Source Capabilities */
	COMMAND_TASK_GSRC = TASK_TO_UINT32('G', 'S', 'r', 'C'),
	/* PD Get Port Partner Information */
	COMMAND_TASK_GPPI = TASK_TO_UINT32('G', 'P', 'P', 'I'),
	/* PD Send Source Capabilities */
	COMMAND_TASK_SSRC = TASK_TO_UINT32('S', 'S', 'r', 'C'),
	/* PD Data Reset */
	COMMAND_TASK_DRST = TASK_TO_UINT32('D', 'R', 'S', 'T'),
	/* Message Buffer Read */
	COMMAND_TASK_MBRD = TASK_TO_UINT32('M', 'B', 'R', 'd'),
	/* Send Alert Message */
	COMMAND_TASK_ALRT = TASK_TO_UINT32('A', 'L', 'R', 'T'),
	/* Send EPR Mode Message */
	COMMAND_TASK_EPRM = TASK_TO_UINT32('E', 'P', 'R', 'm'),
	/* PD Send Enter Mode */
	COMMAND_TASK_AMEN = TASK_TO_UINT32('A', 'M', 'E', 'n'),
	/* PD Send Exit Mode */
	COMMAND_TASK_AMEX = TASK_TO_UINT32('A', 'M', 'E', 'x'),
	/* PD Start Alternate Mode Discovery */
	COMMAND_TASK_AMDS = TASK_TO_UINT32('A', 'M', 'D', 's'),
	/* Get Custom Discovered Modes */
	COMMAND_TASK_GCDM = TASK_TO_UINT32('G', 'C', 'd', 'm'),
	/* PD Send VDM */
	COMMAND_TASK_VDMS = TASK_TO_UINT32('V', 'D', 'M', 's'),
	/* System ready to enter sink power */
	COMMAND_TASK_SRDY = TASK_TO_UINT32('S', 'R', 'D', 'Y'),
	/* SRDY reset */
	COMMAND_TASK_SRYR = TASK_TO_UINT32('S', 'R', 'Y', 'R'),
	/* Power Register Read */
	COMMAND_TASK_PPRD = TASK_TO_UINT32('P', 'P', 'R', 'd'),
	/* Power Register Write */
	COMMAND_TASK_PPWR = TASK_TO_UINT32('P', 'P', 'W', 'r'),
	/* Firmware update tasks */
	COMMAND_TASK_TFUS = TASK_TO_UINT32('T', 'F', 'U', 's'),
	COMMAND_TASK_TFUC = TASK_TO_UINT32('T', 'F', 'U', 'c'),
	COMMAND_TASK_TFUD = TASK_TO_UINT32('T', 'F', 'U', 'd'),
	COMMAND_TASK_TFUE = TASK_TO_UINT32('T', 'F', 'U', 'e'),
	COMMAND_TASK_TFUI = TASK_TO_UINT32('T', 'F', 'U', 'i'),
	COMMAND_TASK_TFUQ = TASK_TO_UINT32('T', 'F', 'U', 'q'),
	/* Abort current task */
	COMMAND_TASK_ABRT = TASK_TO_UINT32('A', 'B', 'R', 'T'),
	/*Auto Negotiate Sink Update */
	COMMAND_TASK_ANEG = TASK_TO_UINT32('A', 'N', 'e', 'g'),
	/* Clear Dead Battery Flag */
	COMMAND_TASK_DBFG = TASK_TO_UINT32('D', 'B', 'f', 'g'),
	/* Error handling for I2C3m transactions */
	COMMAND_TASK_MUXR = TASK_TO_UINT32('M', 'u', 'x', 'R'),
	/* Trigger an Input GPIO Event */
	COMMAND_TASK_TRIG = TASK_TO_UINT32('T', 'r', 'i', 'g'),
	/* I2C read transaction */
	COMMAND_TASK_I2CR = TASK_TO_UINT32('I', '2', 'C', 'r'),
	/* I2C write transaction */
	COMMAND_TASK_I2CW = TASK_TO_UINT32('I', '2', 'C', 'w'),
	/* UCSI tasks */
	COMMAND_TASK_UCSI = TASK_TO_UINT32('U', 'C', 'S', 'I')
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
