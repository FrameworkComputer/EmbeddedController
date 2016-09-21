/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* INTC control module for IT83xx. */

#ifndef __CROS_EC_INTC_H
#define __CROS_EC_INTC_H

int intc_get_ec_int(void);
void lpc_kbc_ibf_interrupt(void);
void lpc_kbc_obe_interrupt(void);
void pm1_ibf_interrupt(void);
void pm2_ibf_interrupt(void);
void pm3_ibf_interrupt(void);
void pm4_ibf_interrupt(void);
void pm5_ibf_interrupt(void);
void lpcrst_interrupt(enum gpio_signal signal);
void peci_interrupt(void);
void i2c_interrupt(int port);
int gpio_clear_pending_interrupt(enum gpio_signal signal);
void clock_sleep_mode_wakeup_isr(void);
int clock_ec_wake_from_sleep(void);
void __enter_hibernate(uint32_t seconds, uint32_t microseconds);

#endif /* __CROS_EC_INTC_H */
