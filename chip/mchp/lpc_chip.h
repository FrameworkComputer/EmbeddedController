/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Microchip MEC1701 specific module for Chrome EC */

#ifndef __CROS_EC_LPC_CHIP_H
#define __CROS_EC_LPC_CHIP_H

#ifdef CONFIG_HOSTCMD_ESPI

#include "espi.h"

#define MCHP_HOST_IF_LPC  (0)
#define MCHP_HOST_IF_ESPI (1)

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

void lpc_mem_mapped_init(void);

#ifndef CONFIG_HOSTCMD_ESPI
void lpcrst_interrupt(enum gpio_signal signal);
#endif

void chip_acpi_ec_config(int instance, uint32_t io_base, uint8_t mask);
void chip_8042_config(uint32_t io_base);
void chip_emi0_config(uint32_t io_base);
void chip_port80_config(uint32_t io_base);
#ifdef CONFIG_EMI_REGION1
uint8_t *lpc_get_customer_memmap_range(void);
#endif

#endif /* __CROS_EC_LPC_CHIP_H */
