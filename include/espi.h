/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* eSPI module for Chrome EC */

#ifndef __CROS_EC_ESPI_H
#define __CROS_EC_ESPI_H

#include "gpio_signal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Signal through VW */
enum espi_vw_signal {
	/* The first valid VW signal is 0x2000 */
	VW_SIGNAL_START = IOEX_LIMIT + 1,
	VW_SLP_S3_L = VW_SIGNAL_START, /* index 02h (In)  */
	VW_SLP_S4_L,
	VW_SLP_S5_L,
	VW_SUS_STAT_L, /* index 03h (In)  */
	VW_PLTRST_L,
	VW_OOB_RST_WARN,
	VW_OOB_RST_ACK, /* index 04h (Out) */
	VW_WAKE_L,
	VW_PME_L,
	VW_ERROR_FATAL, /* index 05h (Out) */
	VW_ERROR_NON_FATAL,
	/* Merge bit 3/0 into one signal. Need to set them simultaneously */
	VW_PERIPHERAL_BTLD_STATUS_DONE,
	VW_SCI_L, /* index 06h (Out) */
	VW_SMI_L,
	VW_RCIN_L,
	VW_HOST_RST_ACK,
	VW_HOST_RST_WARN, /* index 07h (In)  */
	VW_SUS_ACK, /* index 40h (Out) */
	VW_SUS_WARN_L, /* index 41h (In)  */
	VW_SUS_PWRDN_ACK_L,
	VW_SLP_A_L,
	VW_SLP_LAN, /* index 42h (In)  */
	VW_SLP_WLAN,
	VW_SIGNAL_END,
	VW_LIMIT = 0x2FFF
};
BUILD_ASSERT(VW_SIGNAL_END < VW_LIMIT);

#define VW_SIGNAL_COUNT (VW_SIGNAL_END - VW_SIGNAL_START)

/**
 * Set eSPI Virtual-Wire signal to Host
 *
 * @param signal vw signal needs to set
 * @param level  level of vw signal
 * @return EC_SUCCESS, or non-zero if error.
 */
int espi_vw_set_wire(enum espi_vw_signal signal, uint8_t level);

/**
 * Get eSPI Virtual-Wire signal from host
 *
 * @param signal vw signal needs to get
 * @return      1: set by host, otherwise: no signal
 */
int espi_vw_get_wire(enum espi_vw_signal signal);

/**
 * Enable VW interrupt of power sequence signal
 *
 * @param signal vw signal needs to enable interrupt
 * @return EC_SUCCESS, or non-zero if error.
 */
int espi_vw_enable_wire_int(enum espi_vw_signal signal);

/**
 * Disable VW interrupt of power sequence signal
 *
 * @param signal vw signal needs to disable interrupt
 * @return EC_SUCCESS, or non-zero if error.
 */
int espi_vw_disable_wire_int(enum espi_vw_signal signal);

/**
 * Return pointer to constant eSPI virtual wire signal name
 *
 * @param signal virtual wire enum
 * @return pointer to string or NULL if signal out of range
 */
const char *espi_vw_get_wire_name(enum espi_vw_signal signal);

/**
 * Check if signal is an eSPI virtual wire
 * @param signal is gpio_signal or espi_vw_signal enum casted to int
 * @return 1 if signal is virtual wire else returns 0.
 */
int espi_signal_is_vw(int signal);

/**
 * Wait for the specified VW's DIRTY bit to be cleared.
 * @param signal VW to poll DIRTY bit for
 * @param timeout max time in microseconds to poll.
 */
void espi_wait_vw_not_dirty(enum espi_vw_signal signal,
			    unsigned int timeout_us);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ESPI_H */
