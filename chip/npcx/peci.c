/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PECI interface for Chrome EC */

#include "chipset.h"
#include "clock.h"
#include "clock_chip.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "peci.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "temp_sensor.h"
#include "util.h"


/* Initial PECI baud rate */
#define PECI_BAUD_RATE        750000

#define TEMP_AVG_LENGTH       4  /* Should be power of 2 */


/* PECI Time-out */
#define PECI_DONE_TIMEOUT_US  (100*MSEC)
/* Task Event for PECI */
#define TASK_EVENT_PECI_DONE  TASK_EVENT_CUSTOM(1<<26)

#define NULL_PENDING_TASK_ID  0xFFFFFFFF
#define PECI_MAX_FIFO_SIZE    16
#define PROC_SOCKET           0x30
/* PECI Command Code */
enum peci_command_t {
	PECI_COMMAND_PING               = 0x00,
	PECI_COMMAND_GET_DIB            = 0xF7,
	PECI_COMMAND_GET_TEMP           = 0x01,
	PECI_COMMAND_RD_PKG_CFG         = 0xA1,
	PECI_COMMAND_WR_PKG_CFG         = 0xA5,
	PECI_COMMAND_RD_IAMSR           = 0xB1,
	PECI_COMMAND_RD_PCI_CFG         = 0x61,
	PECI_COMMAND_RD_PCI_CFG_LOCAL   = 0xE1,
	PECI_COMMAND_WR_PCI_CFG_LOCAL   = 0xE5,
	PECI_COMMAND_NONE               = 0xFF
};

#define PECI_COMMAND_GET_TEMP_WR_LENS 0x00
#define PECI_COMMAND_GET_TEMP_RD_LENS 0x02

/* PECI Domain Number */
static int temp_vals[TEMP_AVG_LENGTH];
static int temp_idx;
static uint8_t peci_sts;
/* For PECI Done interrupt usage */
static int peci_pending_task_id;

/*****************************************************************************/
/* Internal functions */

/**
 * This routine initiates the parameters of a PECI transaction
 *
 * @param   wr_length How many byte of *wr_data went to be send
 * @param   rd_length How many byte went to received (not include FCS)
 * @param   cmd_code  Command code
 * @param   *wr_data  Buffer pointer of write data
 * @return  TASK_EVENT_PECI_DONE that mean slave had a response
 */
static uint32_t peci_trans(
		uint8_t             wr_length,
		uint8_t             rd_length,
		enum peci_command_t cmd_code,
		uint8_t            *wr_data
)
{
	uint32_t events;
	/* Ensure no PECI transaction is in progress */
	if (IS_BIT_SET(NPCX_PECI_CTL_STS, NPCX_PECI_CTL_STS_START_BUSY)) {
		/*
		 * PECI transaction is in progress -
		 * can not initiate a new one
		 */
		return 0;
	}
	/* Set basic transaction parameters */
	NPCX_PECI_ADDR = PROC_SOCKET;
	NPCX_PECI_CMD = cmd_code;
	/* Aviod over space */
	if (rd_length > PECI_MAX_FIFO_SIZE)
		rd_length = PECI_MAX_FIFO_SIZE;
	/* Read-Length */
	NPCX_PECI_RD_LENGTH = rd_length;
	if (wr_length > PECI_MAX_FIFO_SIZE)
		wr_length = PECI_MAX_FIFO_SIZE;
	/* copy of data */
	for (events = 0; events < wr_length; events++)
		NPCX_PECI_DATA_OUT(events) = wr_data[events];
	/* Write-Length */
	if (cmd_code != PECI_COMMAND_PING) {
		if ((cmd_code == PECI_COMMAND_WR_PKG_CFG) ||
				(cmd_code == PECI_COMMAND_WR_PCI_CFG_LOCAL)) {
			/*CMD+AWFCS*/
			NPCX_PECI_WR_LENGTH = wr_length + 2;
			/* Enable AWFCS */
			SET_BIT(NPCX_PECI_CTL_STS, NPCX_PECI_CTL_STS_AWFCS_EN);
		} else {
			/*CMD*/
			NPCX_PECI_WR_LENGTH = wr_length + 1;
			/* Enable AWFCS */
			CLEAR_BIT(NPCX_PECI_CTL_STS,
					NPCX_PECI_CTL_STS_AWFCS_EN);
		}
	}

	/* Start the PECI transaction */
	SET_BIT(NPCX_PECI_CTL_STS, NPCX_PECI_CTL_STS_START_BUSY);

	/* It should be using a interrupt , don't waste cpu computing power */
	peci_pending_task_id = task_get_current();
	return task_wait_event_mask(TASK_EVENT_PECI_DONE,
					PECI_DONE_TIMEOUT_US);

}

/**
 * PECI transaction error status.
 *
 * @return  Bit3 - CRC error Bit4 - ABRT error
 */
static uint8_t peci_check_error_state(void)
{
	return peci_sts;
}

/*****************************************************************************/
/* PECI drivers */
int peci_get_cpu_temp(void)
{
	uint32_t events;
	int16_t cpu_temp = -1;

	/* Start PECI trans */
	events = peci_trans(PECI_COMMAND_GET_TEMP_WR_LENS,
				PECI_COMMAND_GET_TEMP_RD_LENS,
				PECI_COMMAND_GET_TEMP, NULL);
	/* if return DONE , that mean slave had a PECI response */
	if ((events & TASK_EVENT_PECI_DONE) == TASK_EVENT_PECI_DONE) {
		/* check CRC & ABRT */
		events = peci_check_error_state();
		if (events) {
			;
		} else {
			uint16_t *ptr;
			ptr = (uint16_t *)&cpu_temp;
			ptr[0] = (NPCX_PECI_DATA_IN(1) << 8) |
				 (NPCX_PECI_DATA_IN(0) << 0);
		}
	}
	return (int)cpu_temp;
}

int peci_temp_sensor_get_val(int idx, int *temp_ptr)
{
	int sum = 0;
	int success_cnt = 0;
	int i;

	if (!chipset_in_state(CHIPSET_STATE_ON))
		return EC_ERROR_NOT_POWERED;

	for (i = 0; i < TEMP_AVG_LENGTH; ++i) {
		if (temp_vals[i] >= 0) {
			success_cnt++;
			sum += temp_vals[i];
		}
	}

	/*
	 * Require at least two valid samples. When the AP transitions into S0,
	 * it is possible, depending on the timing of the PECI sample, to read
	 * an invalid temperature. This is very rare, but when it does happen
	 * the temperature returned is CONFIG_PECI_TJMAX. Requiring two valid
	 * samples here assures us that one bad maximum temperature reading
	 * when entering S0 won't cause us to trigger an over temperature.
	 */
	if (success_cnt < 2)
		return EC_ERROR_UNKNOWN;

	*temp_ptr = sum / success_cnt;
	return EC_SUCCESS;
}

static void peci_temp_sensor_poll(void)
{
	int val;

	val = peci_get_cpu_temp();
	if (val != -1) {
		temp_vals[temp_idx] = val;
		temp_idx = (temp_idx + 1) & (TEMP_AVG_LENGTH - 1);
	}
}
DECLARE_HOOK(HOOK_TICK, peci_temp_sensor_poll, HOOK_PRIO_TEMP_SENSOR);

static void peci_freq_changed(void)
{
	/* PECI is under APB2 */
	int freq = clock_get_freq();
	int baud = 0xF;

	/* Disable polling while reconfiguring */
	NPCX_PECI_CTL_STS = 0;

	/*
	 * Set the maximum bit rate used by the PECI module during both
	 * Address Timing Negotiation and Data Timing Negotiation.
	 * The resulting maximum bit rate MAX_BIT_RATE in decimal is
	 * according to the following formula:
	 *
	 * MAX_BIT_RATE [d] = (freq / (4 * baudrate)) - 1
	 * Maximum bit rate should not extend the field's boundaries.
	 */
	if (freq != 0) {
		baud = (uint8_t)(freq  / (4 * PECI_BAUD_RATE)) - 1;
		/* Set maximum PECI baud rate (bit0 - bit4) */
		if (baud > 0x1F)
			baud = 0x1F;
	}
	/* Enhanced High-Speed */
	if (baud >= 7) {
		CLEAR_BIT(NPCX_PECI_RATE, 6);
		CLEAR_BIT(NPCX_PECI_CFG, 3);
	} else {
		SET_BIT(NPCX_PECI_RATE, 6);
		SET_BIT(NPCX_PECI_CFG, 3);
	}
	/* Setting Rate */
	NPCX_PECI_RATE = baud;
}
DECLARE_HOOK(HOOK_FREQ_CHANGE, peci_freq_changed, HOOK_PRIO_DEFAULT);

static void peci_init(void)
{
	int i;

	/* make sure PECI_DATA function pin enable */
	CLEAR_BIT(NPCX_DEVALT(0x0A), 6);
	/* Set initial clock frequency */
	peci_freq_changed();
	/* Initialize temperature reading buffer to a sane value. */
	for (i = 0; i < TEMP_AVG_LENGTH; ++i)
		temp_vals[i] = 300; /* 27 C */

	/* init Pending task id */
	peci_pending_task_id = NULL_PENDING_TASK_ID;
	/* Enable PECI Done interrupt */
	SET_BIT(NPCX_PECI_CTL_STS, NPCX_PECI_CTL_STS_DONE_EN);

	task_enable_irq(NPCX_IRQ_PECI);
}
DECLARE_HOOK(HOOK_INIT, peci_init, HOOK_PRIO_DEFAULT);

/* If received a PECI DONE interrupt, post the event to PECI task */
void peci_done_interrupt(void){
	if (peci_pending_task_id != NULL_PENDING_TASK_ID)
		task_set_event(peci_pending_task_id, TASK_EVENT_PECI_DONE, 0);
	peci_sts = NPCX_PECI_CTL_STS & 0x18;
	/* no matter what, clear status bit again */
	SET_BIT(NPCX_PECI_CTL_STS, NPCX_PECI_CTL_STS_DONE);
	SET_BIT(NPCX_PECI_CTL_STS, NPCX_PECI_CTL_STS_CRC_ERR);
	SET_BIT(NPCX_PECI_CTL_STS, NPCX_PECI_CTL_STS_ABRT_ERR);
}
DECLARE_IRQ(NPCX_IRQ_PECI, peci_done_interrupt, 2);

/*****************************************************************************/
/* Console commands */

static int command_peci_temp(int argc, char **argv)
{
	int t = peci_get_cpu_temp();
	if (t == -1) {
		ccprintf("PECI response timeout\n");
		return EC_ERROR_UNKNOWN;
	}
	ccprintf("CPU temp = %d K = %d\n", t, K_TO_C(t));
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pecitemp, command_peci_temp,
		NULL,
		"Print CPU temperature",
		NULL);
