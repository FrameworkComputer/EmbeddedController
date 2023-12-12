/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* INTC control module for IT83xx. */

#ifndef __CROS_EC_INTC_H
#define __CROS_EC_INTC_H

/*
 * The DSB instruction guarantees a modified architecture or hardware state
 * can be seen by any following dependent data operations.
 */
static inline void data_serialization_barrier(void)
{
	if (IS_ENABLED(CHIP_CORE_NDS32))
		asm volatile("dsb");
}

int intc_get_ec_int(void);
void pm1_ibf_interrupt(void);
void pm2_ibf_interrupt(void);
void pm3_ibf_interrupt(void);
void pm4_ibf_interrupt(void);
void pm5_ibf_interrupt(void);
void lpcrst_interrupt(enum gpio_signal signal);
void peci_interrupt(void);
void adc_interrupt(void);
#ifdef CONFIG_ADC_VOLTAGE_COMPARATOR
void voltage_comparator_interrupt(void);
#endif
void i2c_interrupt(int port);
#ifdef CONFIG_I2C_PERIPHERAL
void i2c_periph_interrupt(int port);
#endif
void clock_sleep_mode_wakeup_isr(void);
int clock_ec_wake_from_sleep(void);
void __enter_hibernate(uint32_t seconds, uint32_t microseconds);
void espi_reset_pin_asserted_interrupt(enum gpio_signal signal);
void espi_fw_reset_module(void);
void espi_interrupt(void);
void espi_vw_interrupt(void);
void espi_enable_pad(int enable);
void espi_init(void);
void clock_cpu_standby(void);
void spi_emmc_cmd0_isr(uint32_t *cmd0_payload);
void spi_peripheral_int_handler(void);
void cec_interrupt(void);
#if defined(CONFIG_HOSTCMD_X86) && defined(HAS_TASK_KEYPROTO)
void lpc_kbc_ibf_interrupt(void);
void lpc_kbc_obe_interrupt(void);
#endif
#ifdef CONFIG_IT83XX_I2C_CMD_QUEUE
uint32_t i2c_idle_not_allowed(void);
#endif

#endif /* __CROS_EC_INTC_H */
