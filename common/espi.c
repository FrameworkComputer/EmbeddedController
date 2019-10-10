/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* eSPI common functionality for Chrome EC */

#include "common.h"
#include "gpio.h"
#include "registers.h"
#include "espi.h"
#include "timer.h"
#include "util.h"


const char *espi_vw_names[] = {
	"VW_SLP_S3_L",
	"VW_SLP_S4_L",
	"VW_SLP_S5_L",
	"VW_SUS_STAT_L",
	"VW_PLTRST_L",
	"VW_OOB_RST_WARN",
	"VW_OOB_RST_ACK",
	"VW_WAKE_L",
	"VW_PME_L",
	"VW_ERROR_FATAL",
	"VW_ERROR_NON_FATAL",
	/* Merge bit 3/0 into one signal. Need to set them simultaneously */
	"VW_SLAVE_BTLD_STATUS_DONE",
	"VW_SCI_L",
	"VW_SMI_L",
	"VW_RCIN_L",
	"VW_HOST_RST_ACK",
	"VW_HOST_RST_WARN",
	"VW_SUS_ACK",
	"VW_SUS_WARN_L",
	"VW_SUS_PWRDN_ACK_L",
	"VW_SLP_A_L",
	"VW_SLP_LAN",
	"VW_SLP_WLAN",
};
BUILD_ASSERT(ARRAY_SIZE(espi_vw_names) == VW_SIGNAL_COUNT);


const char *espi_vw_get_wire_name(enum espi_vw_signal signal)
{
	if (espi_signal_is_vw(signal))
		return espi_vw_names[signal - VW_SIGNAL_START];

	return NULL;
}


int espi_signal_is_vw(int signal)
{
	return ((signal >= VW_SIGNAL_START) && (signal < VW_SIGNAL_END));
}
