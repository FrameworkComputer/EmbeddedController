/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Gingerbread board-specific configuration */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "util.h"

#include "gpio_list.h" /* Must come after other header files. */

/*
 * Table GPIO signals control both power rails and reset lines to various chips
 * on the board. The order the signals are changed and the delay between GPIO
 * signals is driven by USB/MST hub power sequencing requirements.
 */
const struct power_seq board_power_seq[] = {
	{GPIO_EN_AC_JACK,               1, 20},
	{GPIO_EN_PP5000_A,              1, 31},
	{GPIO_EN_PP3300_A,              1, 35},
	{GPIO_STATUS_LED1,              0, 100},
	{GPIO_EN_BB,                    1, 30},
	{GPIO_EN_PP1100_A,              1, 30},
	{GPIO_EN_PP1000_A,              1, 20},
	{GPIO_EN_PP1050_A,              1, 30},
	{GPIO_EN_PP1200_A,              1, 20},
	{GPIO_EN_PP5000_HSPORT,         1, 31},
	{GPIO_EN_DP_SINK,               1, 80},
	{GPIO_MST_LP_CTL_L,             1, 80},
	{GPIO_MST_RST_L,                1, 41},
	{GPIO_EC_HUB1_RESET_L,          1, 41},
	{GPIO_EC_HUB2_RESET_L,          1, 33},
	{GPIO_USBC_DP_PD_RST_L,         1, 100},
	{GPIO_USBC_UF_RESET_L,          1, 33},
	{GPIO_DEMUX_DUAL_DP_PD_N,       1, 100},
	{GPIO_DEMUX_DUAL_DP_RESET_N,    1, 100},
	{GPIO_DEMUX_DP_HDMI_PD_N,       1, 10},
	{GPIO_DEMUX_DUAL_DP_MODE,       1, 10},
	{GPIO_DEMUX_DP_HDMI_MODE,       1, 1},
	{GPIO_STATUS_LED2,              0, 100},
};

const size_t board_power_seq_count = ARRAY_SIZE(board_power_seq);

static void board_init(void)
{
	/* TODO */
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
