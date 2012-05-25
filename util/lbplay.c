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

#include "comm-host.h"
#include "lightbar.h"
#include "ec_commands.h"

/* Handy tricks */
#define BUILD_ASSERT(cond) ((void)sizeof(char[1 - 2*!(cond)]))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))


static const struct {
	uint8_t insize;
	uint8_t outsize;
} lb_command_paramcount[] = {
	{ sizeof(((struct ec_params_lightbar_cmd *)0)->in.dump),
	  sizeof(((struct ec_params_lightbar_cmd *)0)->out.dump) },
	{ sizeof(((struct ec_params_lightbar_cmd *)0)->in.off),
	  sizeof(((struct ec_params_lightbar_cmd *)0)->out.off) },
	{ sizeof(((struct ec_params_lightbar_cmd *)0)->in.on),
	  sizeof(((struct ec_params_lightbar_cmd *)0)->out.on) },
	{ sizeof(((struct ec_params_lightbar_cmd *)0)->in.init),
	  sizeof(((struct ec_params_lightbar_cmd *)0)->out.init) },
	{ sizeof(((struct ec_params_lightbar_cmd *)0)->in.brightness),
	  sizeof(((struct ec_params_lightbar_cmd *)0)->out.brightness) },
	{ sizeof(((struct ec_params_lightbar_cmd *)0)->in.seq),
	  sizeof(((struct ec_params_lightbar_cmd *)0)->out.seq) },
	{ sizeof(((struct ec_params_lightbar_cmd *)0)->in.reg),
	  sizeof(((struct ec_params_lightbar_cmd *)0)->out.reg) },
	{ sizeof(((struct ec_params_lightbar_cmd *)0)->in.rgb),
	  sizeof(((struct ec_params_lightbar_cmd *)0)->out.rgb) },
	{ sizeof(((struct ec_params_lightbar_cmd *)0)->in.get_seq),
	  sizeof(((struct ec_params_lightbar_cmd *)0)->out.get_seq) },
};


static void lb_cmd_noargs(enum lightbar_command cmd)
{
	struct ec_params_lightbar_cmd param;
	param.in.cmd = cmd;
	ec_command(EC_CMD_LIGHTBAR_CMD,
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
	struct ec_params_lightbar_cmd param;
	param.in.cmd = LIGHTBAR_CMD_BRIGHTNESS;
	param.in.brightness.num = newval;
	ec_command(EC_CMD_LIGHTBAR_CMD,
		   &param, lb_command_paramcount[param.in.cmd].insize,
		   &param, lb_command_paramcount[param.in.cmd].outsize);
}

void lightbar_sequence(enum lightbar_sequence num)
{
	struct ec_params_lightbar_cmd param;
	param.in.cmd = LIGHTBAR_CMD_SEQ;
	param.in.seq.num = num;
	ec_command(EC_CMD_LIGHTBAR_CMD,
		   &param, lb_command_paramcount[param.in.cmd].insize,
		   &param, lb_command_paramcount[param.in.cmd].outsize);
}

void lightbar_reg(uint8_t ctrl, uint8_t reg, uint8_t val)
{
	struct ec_params_lightbar_cmd param;
	param.in.cmd = LIGHTBAR_CMD_REG;
	param.in.reg.ctrl = ctrl;
	param.in.reg.reg = reg;
	param.in.reg.value = val;
	ec_command(EC_CMD_LIGHTBAR_CMD,
		   &param, lb_command_paramcount[param.in.cmd].insize,
		   &param, lb_command_paramcount[param.in.cmd].outsize);
}

void lightbar_rgb(int led, int red, int green, int blue)
{
	struct ec_params_lightbar_cmd param;
	param.in.cmd = LIGHTBAR_CMD_RGB;
	param.in.rgb.led = led;
	param.in.rgb.red = red;
	param.in.rgb.green = green;
	param.in.rgb.blue = blue;
	ec_command(EC_CMD_LIGHTBAR_CMD,
		   &param, lb_command_paramcount[param.in.cmd].insize,
		   &param, lb_command_paramcount[param.in.cmd].outsize);
}

void wait_for_ec_to_stop(void)
{
	int r;
	struct ec_params_lightbar_cmd param;
	int count = 0;

	do {
		usleep(100000);
		param.in.cmd = LIGHTBAR_CMD_GET_SEQ;
		r = ec_command(EC_CMD_LIGHTBAR_CMD,
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

	if (comm_init() < 0)
		return -3;

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
