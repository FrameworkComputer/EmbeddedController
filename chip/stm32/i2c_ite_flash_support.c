/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* STM implementation for flashing ITE-based ECs over i2c */

#include "i2c.h"
#include "i2c_ite_flash_support.h"
#include "registers.h"
#include "time.h"

/*
 * As of 2018-11-27 the default for both is 60 bytes.  These larger values allow
 * for reflashing of ITE EC chips over I2C
 * (https://issuetracker.google.com/79684405) in reasonably speedy fashion.  If
 * the EC firmware defaults are ever raised significantly, consider removing
 * these overrides.
 *
 * As of 2018-11-27 the actual maximum write size supported by the I2C-over-USB
 * protocol is (1<<12)-1, and the maximum read size supported is
 * (1<<15)-1.  However compile time assertions require that these values be
 * powers of 2 after overheads are included.  Thus, the write limit set here
 * /should/ be (1<<12)-4 and the read limit should be (1<<15)-6, however those
 * ideal limits are not actually possible because stm32 lacks sufficient
 * spare memory for them.  With symmetrical limits, the maximum that currently
 * fits is (1<<11)-4 write limit and (1<<11)-6 read limit, leaving 1404 bytes of
 * RAM available.
 *
 * However even with a sufficiently large write value here, the maximum that
 * actually works as of 2018-12-03 is 255 bytes.  Additionally, ITE EC firmware
 * image verification requires exactly 256 byte reads.  Thus the only useful
 * powers-of-2-minus-overhead limits to set here are (1<<9)-4 writes and
 * (1<<9)-6 reads, leaving 6012 bytes of RAM available, down from 7356 bytes of
 * RAM available with the default 60 byte limits.
 */
#if CONFIG_USB_I2C_MAX_WRITE_COUNT != ((1 << 9) - 4)
#error Must set CONFIG_USB_I2C_MAX_WRITE_COUNT to ((1<<9) - 4)
#endif
#if CONFIG_USB_I2C_MAX_READ_COUNT != ((1 << 9) - 6)
#error Must set CONFIG_USB_I2C_MAX_WRITE_COUNT to ((1<<9) - 6)
#endif

/*
 * iteflash requires 256 byte reads for verifying ITE EC firmware.  Without this
 * the limit is CONFIG_I2C_CHIP_MAX_TRANSFER_SIZE which is 255 for STM32F0 due
 * to an 8 bit field, per src/platform/ec/include/config.h comment.
 */
#ifndef CONFIG_I2C_XFER_LARGE_TRANSFER
#error Must define CONFIG_I2C_XFER_LARGE_TRANSFER
#endif

#define KHz 1000
#define MHz (1000 * KHz)

/*
 * These constants are values that one might want to try changing if
 * enable_ite_dfu stops working, or does not work on a new ITE EC chip revision.
 */

#define ITE_DFU_I2C_CMD_ADDR_FLAGS 0x5A
#define ITE_DFU_I2C_DATA_ADDR_FLAGS 0x35

#define SMCLK_WAVEFORM_PERIOD_HZ (100 * KHz)
#define SMDAT_WAVEFORM_PERIOD_HZ (200 * KHz)

#define START_DELAY_MS 5
#define SPECIAL_WAVEFORM_MS 50
#define PLL_STABLE_MS 10

/*
 * Digital line levels to hold before (PRE_) or after (POST_) sending the
 * special waveforms.  0 for low, 1 for high.
 */
#define SMCLK_PRE_LEVEL 0
#define SMDAT_PRE_LEVEL 0
#define SMCLK_POST_LEVEL 0
#define SMDAT_POST_LEVEL 0

/* The caller should hold the i2c_lock() for ite_dfu_config.i2c_port. */
static int ite_i2c_read_register(uint8_t register_offset, uint8_t *output)
{
	/*
	 * Ideally the write and read would be done in one I2C transaction, as
	 * is normally done when reading from the same I2C address that the
	 * write was sent to.  The ITE EC is abnormal in that regard, with its
	 * different 7-bit addresses for writes vs reads.
	 *
	 * i2c_xfer() does not support that.  Its I2C_XFER_START and
	 * I2C_XFER_STOP flag bits do not cleanly support that scenario, they
	 * are for continuing transfers without either of STOP or START
	 * in-between.
	 *
	 * For what it's worth, the iteflash.c FTDI-based implementation of this
	 * does the same thing, issuing a STOP between the write and read.  This
	 * works, even if perhaps it should not.
	 */
	int ret;
	/* Tell the ITE EC which register we want to read. */
	ret = i2c_xfer_unlocked(ite_dfu_config.i2c_port,
				ITE_DFU_I2C_CMD_ADDR_FLAGS, &register_offset,
				sizeof(register_offset), NULL, 0,
				I2C_XFER_SINGLE);
	if (ret != EC_SUCCESS)
		return ret;
	/* Read in the 1 byte register value. */
	ret = i2c_xfer_unlocked(ite_dfu_config.i2c_port,
				ITE_DFU_I2C_DATA_ADDR_FLAGS, NULL, 0, output,
				sizeof(*output), I2C_XFER_SINGLE);
	return ret;
}

/* Helper function to read ITE chip ID, for verifying ITE DFU mode. */
static int cprint_ite_chip_id(void)
{
	/*
	 * Per i2c_read8() implementation, use an array even for single byte
	 * reads to ensure alignment for DMA on STM32.
	 */
	uint8_t chipid1[1];
	uint8_t chipid2[1];
	uint8_t chipver[1];

	int ret;
	int chip_version;

	i2c_lock(ite_dfu_config.i2c_port, 1);

	/* Read the CHIPID1 register. */
	ret = ite_i2c_read_register(0x00, chipid1);
	if (ret != EC_SUCCESS)
		goto unlock;

	/* Read the CHIPID2 register. */
	ret = ite_i2c_read_register(0x01, chipid2);
	if (ret != EC_SUCCESS)
		goto unlock;

	/* Read the CHIPVER register. */
	ret = ite_i2c_read_register(0x02, chipver);

unlock:
	i2c_lock(ite_dfu_config.i2c_port, 0);
	if (ret != EC_SUCCESS)
		return ret;

	chip_version = chipver[0] & 0x07;

	ccprintf("ITE EC info: CHIPID1=0x%02X CHIPID2=0x%02X CHIPVER=0x%02X ",
		 chipid1[0], chipid2[0], chipver[0]);
	ccprintf("version=%d\n", chip_version);

	return ret;
}

/* Enable ITE direct firmware update (DFU) mode. */
static int command_enable_ite_dfu(int argc, const char **argv)
{
	if (argc > 1)
		return EC_ERROR_PARAM_COUNT;

	/* Ensure we can perform the dfu operation */
	if (ite_dfu_config.access_allow && !ite_dfu_config.access_allow())
		return EC_ERROR_ACCESS_DENIED;

	/* Enable peripheral clocks. */
	STM32_RCC_APB2ENR |= STM32_RCC_APB2ENR_TIM16EN |
			     STM32_RCC_APB2ENR_TIM17EN;

	/* Reset timer registers which are not otherwise set below. */
	STM32_TIM_CR2(16) = 0x0000;
	STM32_TIM_CR2(17) = 0x0000;
	STM32_TIM_DIER(16) = 0x0000;
	STM32_TIM_DIER(17) = 0x0000;
	STM32_TIM_SR(16) = 0x0000;
	STM32_TIM_SR(17) = 0x0000;
	STM32_TIM_CNT(16) = 0x0000;
	STM32_TIM_CNT(17) = 0x0000;
	STM32_TIM_RCR(16) = 0x0000;
	STM32_TIM_RCR(17) = 0x0000;
	STM32_TIM_DCR(16) = 0x0000;
	STM32_TIM_DCR(17) = 0x0000;
	STM32_TIM_DMAR(16) = 0x0000;
	STM32_TIM_DMAR(17) = 0x0000;

	/* Prescale to 1 MHz and use ARR to achieve NNN KHz periods. */
	/* This approach is seen in STM's documentation. */
	STM32_TIM_PSC(16) = (CPU_CLOCK / MHz) - 1;
	STM32_TIM_PSC(17) = (CPU_CLOCK / MHz) - 1;

	/* Set the waveform periods based on 1 MHz prescale. */
	STM32_TIM_ARR(16) = (MHz / SMCLK_WAVEFORM_PERIOD_HZ) - 1;
	STM32_TIM_ARR(17) = (MHz / SMDAT_WAVEFORM_PERIOD_HZ) - 1;

	/* Set output compare 1 mode to PWM mode 1 and enable preload. */
	STM32_TIM_CCMR1(16) = STM32_TIM_CCMR1_OC1M_PWM_MODE_1 |
			      STM32_TIM_CCMR1_OC1PE;
	STM32_TIM_CCMR1(17) = STM32_TIM_CCMR1_OC1M_PWM_MODE_1 |
			      STM32_TIM_CCMR1_OC1PE;

	/*
	 * Enable output compare 1 (or its N counterpart). Note that if only
	 * OC1N is enabled, then it is not complemented. From datasheet:
	 * "When only OCxN is enabled (CCxE=0, CCxNE=1), it is not complemented"
	 *
	 * Note: we want the rising edge of SDA to be in the middle of SCL, so
	 * invert the SDA (faster) signal.
	 */
	if (ite_dfu_config.use_complement_timer_channel) {
		STM32_TIM_CCER(16) = STM32_TIM_CCER_CC1NE;
		STM32_TIM_CCER(17) = STM32_TIM_CCER_CC1NE |
				     STM32_TIM_CCER_CC1NP;
	} else {
		STM32_TIM_CCER(16) = STM32_TIM_CCER_CC1E;
		STM32_TIM_CCER(17) = STM32_TIM_CCER_CC1E | STM32_TIM_CCER_CC1P;
	}

	/* Enable main output. */
	STM32_TIM_BDTR(16) = STM32_TIM_BDTR_MOE;
	STM32_TIM_BDTR(17) = STM32_TIM_BDTR_MOE;

	/* Update generation (reinitialize counters). */
	STM32_TIM_EGR(16) = STM32_TIM_EGR_UG;
	STM32_TIM_EGR(17) = STM32_TIM_EGR_UG;

	/* Set duty cycle to 0% or 100%, pinning each channel low or high. */
	STM32_TIM_CCR1(16) = SMCLK_PRE_LEVEL ? 0xFFFF : 0x0000;
	STM32_TIM_CCR1(17) = SMDAT_PRE_LEVEL ? 0xFFFF : 0x0000;

	/* Enable timer counters. */
	STM32_TIM_CR1(16) = STM32_TIM_CR1_CEN;
	STM32_TIM_CR1(17) = STM32_TIM_CR1_CEN;

	/* Set GPIO to alternate mode TIM(16|17)_CH1(N)? */
	gpio_config_pin(MODULE_I2C_TIMERS, ite_dfu_config.scl, 1);
	gpio_config_pin(MODULE_I2C_TIMERS, ite_dfu_config.sda, 1);

	crec_msleep(START_DELAY_MS);

	/* Set pulse width to half of waveform period. */
	STM32_TIM_CCR1(16) = (MHz / SMCLK_WAVEFORM_PERIOD_HZ) / 2;
	STM32_TIM_CCR1(17) = (MHz / SMDAT_WAVEFORM_PERIOD_HZ) / 2;

	crec_msleep(SPECIAL_WAVEFORM_MS);

	/* Set duty cycle to 0% or 100%, pinning each channel low or high. */
	STM32_TIM_CCR1(16) = SMCLK_POST_LEVEL ? 0xFFFF : 0x0000;
	STM32_TIM_CCR1(17) = SMDAT_POST_LEVEL ? 0xFFFF : 0x0000;

	crec_msleep(PLL_STABLE_MS);

	/* Set GPIO back to alternate mode I2C. */
	gpio_config_pin(MODULE_I2C, ite_dfu_config.scl, 1);
	gpio_config_pin(MODULE_I2C, ite_dfu_config.sda, 1);

	/* Disable timer counters. */
	STM32_TIM_CR1(16) = 0x0000;
	STM32_TIM_CR1(17) = 0x0000;

	/* Disable peripheral clocks. */
	STM32_RCC_APB2ENR &=
		~(STM32_RCC_APB2ENR_TIM16EN | STM32_RCC_APB2ENR_TIM17EN);

	return cprint_ite_chip_id();
}
DECLARE_CONSOLE_COMMAND(enable_ite_dfu, command_enable_ite_dfu, "",
			"Enable ITE Direct Firmware Update (DFU) mode");

/* Read ITE chip ID.  Can be used to verify ITE DFU mode. */
static int command_get_ite_chipid(int argc, const char **argv)
{
	if (argc > 1)
		return EC_ERROR_PARAM_COUNT;

	/* Ensure we can perform the dfu operation */
	if (ite_dfu_config.access_allow && !ite_dfu_config.access_allow())
		return EC_ERROR_ACCESS_DENIED;

	return cprint_ite_chip_id();
}
DECLARE_CONSOLE_COMMAND(get_ite_chipid, command_get_ite_chipid, "",
			"Read ITE EC chip ID and version (must be in DFU mode)");
