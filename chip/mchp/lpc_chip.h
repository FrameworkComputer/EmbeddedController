/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Microchip MEC1701 specific module for Chrome EC */

#ifndef __CROS_EC_LPC_CHIP_H
#define __CROS_EC_LPC_CHIP_H

#ifdef CONFIG_HOSTCMD_ESPI

#include "espi.h"

/* eSPI Initialization functions */
void espi_init(void);

/* eSPI ESPI_RESET# interrupt handler */
void espi_reset_handler(void);

/*
 *
 */
int espi_vw_pulse_wire(enum espi_vw_signal signal, int pulse_level);

void lpc_update_host_event_status(void);

#endif

/* LPC LRESET interrupt handler */
void lpcrst_interrupt(enum gpio_signal signal);

void lpc_set_init_done(int val);

uint32_t lpc_mem_mapped_addr(void);

void lpc_mem_mapped_init(void);

#endif /* __CROS_EC_LPC_CHIP_H */
