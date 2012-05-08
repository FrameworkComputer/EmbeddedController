/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <unistd.h>

#include "lightbar.h"
#include "lpc_commands.h"

/* Handy tricks */
#define BUILD_ASSERT(cond) ((void)sizeof(char[1 - 2*!(cond)]))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* Waits for the EC to be unbusy.  Returns 0 if unbusy, non-zero if
 * timeout. */
static int wait_for_ec(int status_addr, int timeout_usec)
{
	int i;
	for (i = 0; i < timeout_usec; i += 10) {
		usleep(10);  /* Delay first, in case we just sent a command */
		if (!(inb(status_addr) & EC_LPC_STATUS_BUSY_MASK))
			return 0;
	}
	return -1;  /* Timeout */
}


/* Sends a command to the EC.  Returns the command status code, or
 * -1 if other error. */
static int ec_command(int command, const void *indata, int insize,
		      void *outdata, int outsize) {
	uint8_t *d;
	int i;

	/* TODO: add command line option to use kernel command/param window */
	int cmd_addr = EC_LPC_ADDR_USER_CMD;
	int data_addr = EC_LPC_ADDR_USER_DATA;
	int param_addr = EC_LPC_ADDR_USER_PARAM;

	if (insize > EC_LPC_PARAM_SIZE || outsize > EC_LPC_PARAM_SIZE) {
		fprintf(stderr, "Data size too big\n");
		return -1;
	}

	if (wait_for_ec(cmd_addr, 1000000)) {
		fprintf(stderr, "Timeout waiting for EC ready\n");
		return -1;
	}

	/* Write data, if any */
	/* TODO: optimized copy using outl() */
	for (i = 0, d = (uint8_t *)indata; i < insize; i++, d++)
		outb(*d, param_addr + i);

	outb(command, cmd_addr);

	if (wait_for_ec(cmd_addr, 1000000)) {
		fprintf(stderr, "Timeout waiting for EC response\n");
		return -1;
	}

	/* Check result */
	i = inb(data_addr);
	if (i) {
		fprintf(stderr, "EC returned error result code %d\n", i);
		return i;
	}

	/* Read data, if any */
	/* TODO: optimized copy using outl() */
	for (i = 0, d = (uint8_t *)outdata; i < outsize; i++, d++)
		*d = inb(param_addr + i);

	return 0;
}

static const struct {
	uint8_t insize;
	uint8_t outsize;
} lb_command_paramcount[] = {
	{ sizeof(((struct lpc_params_lightbar_cmd *)0)->in.dump),
	  sizeof(((struct lpc_params_lightbar_cmd *)0)->out.dump) },
	{ sizeof(((struct lpc_params_lightbar_cmd *)0)->in.off),
	  sizeof(((struct lpc_params_lightbar_cmd *)0)->out.off) },
	{ sizeof(((struct lpc_params_lightbar_cmd *)0)->in.on),
	  sizeof(((struct lpc_params_lightbar_cmd *)0)->out.on) },
	{ sizeof(((struct lpc_params_lightbar_cmd *)0)->in.init),
	  sizeof(((struct lpc_params_lightbar_cmd *)0)->out.init) },
	{ sizeof(((struct lpc_params_lightbar_cmd *)0)->in.brightness),
	  sizeof(((struct lpc_params_lightbar_cmd *)0)->out.brightness) },
	{ sizeof(((struct lpc_params_lightbar_cmd *)0)->in.seq),
	  sizeof(((struct lpc_params_lightbar_cmd *)0)->out.seq) },
	{ sizeof(((struct lpc_params_lightbar_cmd *)0)->in.reg),
	  sizeof(((struct lpc_params_lightbar_cmd *)0)->out.reg) },
	{ sizeof(((struct lpc_params_lightbar_cmd *)0)->in.rgb),
	  sizeof(((struct lpc_params_lightbar_cmd *)0)->out.rgb) },
	{ sizeof(((struct lpc_params_lightbar_cmd *)0)->in.get_seq),
	  sizeof(((struct lpc_params_lightbar_cmd *)0)->out.get_seq) },
};


static void lb_cmd_noargs(enum lightbar_command cmd)
{
	struct lpc_params_lightbar_cmd param;
	param.in.cmd = cmd;
	ec_command(EC_LPC_COMMAND_LIGHTBAR_CMD,
		   &param, lb_command_paramcount[param.in.cmd].insize,
		   &param, lb_command_paramcount[param.in.cmd].outsize);
}

inline void lightbar_off(void)
{
	lb_cmd_noargs(LIGHTBAR_CMD_OFF);
}

inline void lightbar_on(void)
{
	lb_cmd_noargs(LIGHTBAR_CMD_ON);
}

inline void lightbar_init_vals(void)
{
	lb_cmd_noargs(LIGHTBAR_CMD_INIT);
}

void lightbar_brightness(int newval)
{
	struct lpc_params_lightbar_cmd param;
	param.in.cmd = LIGHTBAR_CMD_BRIGHTNESS;
	param.in.brightness.num = newval;
	ec_command(EC_LPC_COMMAND_LIGHTBAR_CMD,
		   &param, lb_command_paramcount[param.in.cmd].insize,
		   &param, lb_command_paramcount[param.in.cmd].outsize);
}

void lightbar_sequence(enum lightbar_sequence num)
{
	struct lpc_params_lightbar_cmd param;
	param.in.cmd = LIGHTBAR_CMD_SEQ;
	param.in.seq.num = num;
	ec_command(EC_LPC_COMMAND_LIGHTBAR_CMD,
		   &param, lb_command_paramcount[param.in.cmd].insize,
		   &param, lb_command_paramcount[param.in.cmd].outsize);
}

void lightbar_reg(uint8_t ctrl, uint8_t reg, uint8_t val)
{
	struct lpc_params_lightbar_cmd param;
	param.in.cmd = LIGHTBAR_CMD_REG;
	param.in.reg.ctrl = ctrl;
	param.in.reg.reg = reg;
	param.in.reg.value = val;
	ec_command(EC_LPC_COMMAND_LIGHTBAR_CMD,
		   &param, lb_command_paramcount[param.in.cmd].insize,
		   &param, lb_command_paramcount[param.in.cmd].outsize);
}

void lightbar_rgb(int led, int red, int green, int blue)
{
	struct lpc_params_lightbar_cmd param;
	param.in.cmd = LIGHTBAR_CMD_RGB;
	param.in.rgb.led = led;
	param.in.rgb.red = red;
	param.in.rgb.green = green;
	param.in.rgb.blue = blue;
	ec_command(EC_LPC_COMMAND_LIGHTBAR_CMD,
		   &param, lb_command_paramcount[param.in.cmd].insize,
		   &param, lb_command_paramcount[param.in.cmd].outsize);
}

void wait_for_ec_to_stop(void)
{
	int r;
	struct lpc_params_lightbar_cmd param;
	int count = 0;

	do {
		usleep(100000);
		param.in.cmd = LIGHTBAR_CMD_GET_SEQ;
		r = ec_command(EC_LPC_COMMAND_LIGHTBAR_CMD,
			       &param,
			       lb_command_paramcount[param.in.cmd].insize,
			       &param,
			       lb_command_paramcount[param.in.cmd].outsize);
		if (count++ > 10) {
			fprintf(stderr, "EC isn't responding\n");
			exit(1);
		}
	} while (r != 0 && param.out.get_seq.num != LIGHTBAR_STOP);
}

int main(int argc, char **argv)
{
	int i;

	BUILD_ASSERT(ARRAY_SIZE(lb_command_paramcount) == LIGHTBAR_NUM_CMDS);

	/* Request I/O privilege */
	if (iopl(3) < 0) {
		perror("Error getting I/O privilege");
		return -3;
	}


	/* Tell the EC to let us drive. */
	lightbar_sequence(LIGHTBAR_STOP);

	/* Wait until it's listening */
	wait_for_ec_to_stop();

	/* Initialize it */
	lightbar_off();
	lightbar_init_vals();
	lightbar_brightness(0xff);
	lightbar_on();

	/* Play a bit */

	for (i = 0; i <= 255; i += 4) {
		lightbar_rgb(4, 0, i, 0);
		usleep(100000);
	}

	for (; i >= 0; i -= 4) {
		lightbar_rgb(4, i, 0, 0);
		usleep(100000);
	}

	/* Let the EC drive again */
	lightbar_sequence(LIGHTBAR_RUN);

	return 0;
}
