/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "intc.h"
#include "it83xx_pd.h"
#include "ite_pd_intc.h"
#include "kmsc_chip.h"
#include "registers.h"
#include "task.h"
#include "tcpm/tcpm.h"
#include "usb_pd.h"

int __ram_code intc_get_ec_int(void)
{
	extern volatile int ec_int;
	return ec_int;
}

static void intc_cpu_int_group_5(void)
{
	/* Determine interrupt number. */
	int intc_group_5 = intc_get_ec_int();

	switch (intc_group_5) {
#if defined(CONFIG_HOSTCMD_X86) && defined(HAS_TASK_KEYPROTO)
	case IT83XX_IRQ_KBC_OUT:
		lpc_kbc_obe_interrupt();
		break;

	case IT83XX_IRQ_KBC_IN:
		lpc_kbc_ibf_interrupt();
		break;
#endif
	default:
		break;
	}
}
DECLARE_IRQ(CPU_INT_GROUP_5, intc_cpu_int_group_5, 2);

static void intc_cpu_int_group_4(void)
{
	/* Determine interrupt number. */
	int intc_group_4 = intc_get_ec_int();

	switch (intc_group_4) {
#ifdef CONFIG_HOSTCMD_X86
	case IT83XX_IRQ_PMC_IN:
		pm1_ibf_interrupt();
		break;

	case IT83XX_IRQ_PMC2_IN:
		pm2_ibf_interrupt();
		break;

	case IT83XX_IRQ_PMC3_IN:
		pm3_ibf_interrupt();
		break;

	case IT83XX_IRQ_PMC4_IN:
		pm4_ibf_interrupt();
		break;

	case IT83XX_IRQ_PMC5_IN:
		pm5_ibf_interrupt();
		break;
#endif
	default:
		break;
	}
}
DECLARE_IRQ(CPU_INT_GROUP_4, intc_cpu_int_group_4, 2);

static void intc_cpu_int_group_12(void)
{
	/* Determine interrupt number. */
	int intc_group_12 = intc_get_ec_int();

	switch (intc_group_12) {
#ifdef CONFIG_PECI
	case IT83XX_IRQ_PECI:
		peci_interrupt();
		break;
#endif
#ifdef CONFIG_HOST_INTERFACE_ESPI
	case IT83XX_IRQ_ESPI:
		espi_interrupt();
		break;

	case IT83XX_IRQ_ESPI_VW:
		espi_vw_interrupt();
		break;
#endif
#ifdef CONFIG_USB_PD_TCPM_ITE_ON_CHIP
	case IT83XX_IRQ_USBPD0:
		chip_pd_irq(USBPD_PORT_A);
		break;

	case IT83XX_IRQ_USBPD1:
		chip_pd_irq(USBPD_PORT_B);
		break;
#ifdef CONFIG_USB_PD_TCPM_DRIVER_IT8XXX2
	case IT83XX_IRQ_USBPD2:
		chip_pd_irq(USBPD_PORT_C);
		break;
#endif
#endif
#ifdef CONFIG_SPI
	case IT83XX_IRQ_SPI_PERIPHERAL:
		spi_peripheral_int_handler();
		break;
#endif
#ifdef CONFIG_CEC_IT83XX
	case IT83XX_IRQ_CEC:
		cec_interrupt();
		break;

#endif
	default:
		break;
	}
}
DECLARE_IRQ(CPU_INT_GROUP_12, intc_cpu_int_group_12, 2);

static void intc_cpu_int_group_7(void)
{
	/* Determine interrupt number. */
	int intc_group_7 = intc_get_ec_int();

	switch (intc_group_7) {
#ifdef CONFIG_ADC
	case IT83XX_IRQ_ADC:
		adc_interrupt();
		break;
#ifdef CONFIG_ADC_VOLTAGE_COMPARATOR
	case IT83XX_IRQ_V_COMP:
		voltage_comparator_interrupt();
		break;
#endif
#endif
	default:
		break;
	}
}
DECLARE_IRQ(CPU_INT_GROUP_7, intc_cpu_int_group_7, 2);

static void intc_cpu_int_group_6(void)
{
	/* Determine interrupt number. */
	int intc_group_6 = intc_get_ec_int();

	switch (intc_group_6) {
#if defined(CONFIG_I2C_CONTROLLER) || defined(CONFIG_I2C_PERIPHERAL)
	case IT83XX_IRQ_SMB_A:
#ifdef CONFIG_I2C_PERIPHERAL
		if (IT83XX_SMB_SFFCTL & IT83XX_SMB_SAFE)
			i2c_periph_interrupt(IT83XX_I2C_CH_A);
		else
#endif
			i2c_interrupt(IT83XX_I2C_CH_A);
		break;

	case IT83XX_IRQ_SMB_B:
		i2c_interrupt(IT83XX_I2C_CH_B);
		break;

	case IT83XX_IRQ_SMB_C:
		i2c_interrupt(IT83XX_I2C_CH_C);
		break;

	case IT83XX_IRQ_SMB_D:
#ifdef CONFIG_I2C_PERIPHERAL
		if (!(IT83XX_I2C_CTR(3) & IT83XX_I2C_MODE))
			i2c_periph_interrupt(IT83XX_I2C_CH_D);
		else
#endif
			i2c_interrupt(IT83XX_I2C_CH_D);
		break;

	case IT83XX_IRQ_SMB_E:
#ifdef CONFIG_I2C_PERIPHERAL
		if (!(IT83XX_I2C_CTR(0) & IT83XX_I2C_MODE))
			i2c_periph_interrupt(IT83XX_I2C_CH_E);
		else
#endif
			i2c_interrupt(IT83XX_I2C_CH_E);
		break;

	case IT83XX_IRQ_SMB_F:
#ifdef CONFIG_I2C_PERIPHERAL
		if (!(IT83XX_I2C_CTR(1) & IT83XX_I2C_MODE))
			i2c_periph_interrupt(IT83XX_I2C_CH_F);
		else
#endif
			i2c_interrupt(IT83XX_I2C_CH_F);
		break;
#endif
	default:
		break;
	}
}
DECLARE_IRQ(CPU_INT_GROUP_6, intc_cpu_int_group_6, 2);
