/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "intc.h"
#include "it83xx_pd.h"
#include "kmsc_chip.h"
#include "registers.h"
#include "task.h"
#include "tcpm.h"
#include "usb_pd.h"

#if defined(CONFIG_USB_PD_TCPM_ITE_ON_CHIP)
static void chip_pd_irq(enum usbpd_port port)
{
	task_clear_pending_irq(usbpd_ctrl_regs[port].irq);

	/* check status */
	if (IS_ENABLED(IT83XX_INTC_FAST_SWAP_SUPPORT) &&
		IS_ENABLED(CONFIG_USB_PD_FRS_TCPC) &&
		IS_ENABLED(CONFIG_USB_PD_REV30)) {
		/*
		 * FRS detection must handle first, because we need to short
		 * the interrupt -> board_frs_handler latency-critical time.
		 */
		if (USBPD_IS_FAST_SWAP_DETECT(port)) {
			/* clear detect FRS signal (cc to GND) status */
			USBPD_CLEAR_FRS_DETECT_STATUS(port);
			if (board_frs_handler)
				board_frs_handler(port);
			/* inform TCPMv2 to change state */
			pd_got_frs_signal(port);
		}
	}

	if (USBPD_IS_HARD_RESET_DETECT(port)) {
		/* clear interrupt */
		IT83XX_USBPD_ISR(port) = USBPD_REG_MASK_HARD_RESET_DETECT;
		USBPD_SW_RESET(port);
		task_set_event(PD_PORT_TO_TASK_ID(port),
			PD_EVENT_RX_HARD_RESET, 0);
	}

	if (USBPD_IS_RX_DONE(port)) {
		tcpm_enqueue_message(port);
		/* clear RX done interrupt */
		IT83XX_USBPD_ISR(port) = USBPD_REG_MASK_MSG_RX_DONE;
	}

	if (USBPD_IS_TX_DONE(port)) {
#ifdef CONFIG_USB_PD_TCPM_DRIVER_IT8XXX2
		it83xx_clear_tx_error_status(port);
		/* check TX status, clear by TX_DONE status too */
		if (USBPD_IS_TX_ERR(port))
			it83xx_get_tx_error_status(port);
#endif
		/* clear TX done interrupt */
		IT83XX_USBPD_ISR(port) = USBPD_REG_MASK_MSG_TX_DONE;
		task_set_event(PD_PORT_TO_TASK_ID(port),
			TASK_EVENT_PHY_TX_DONE, 0);
	}

	if (IS_ENABLED(IT83XX_INTC_PLUG_IN_OUT_SUPPORT)) {
		if (USBPD_IS_PLUG_IN_OUT_DETECT(port)) {
			if (USBPD_IS_PLUG_IN(port))
				/*
				 * When tcpc detect type-c plug in:
				 * 1)If we are sink, disable detect interrupt,
				 * messages on cc line won't trigger interrupt.
				 * 2)If we are source, then set plug out
				 * detection.
				 */
				switch_plug_out_type(port);
			else
				/*
				 * When tcpc detect type-c plug out:
				 * switch to detect plug in.
				 */
				IT83XX_USBPD_TCDCR(port) &=
					~USBPD_REG_PLUG_OUT_SELECT;

			/* clear type-c device plug in/out detect interrupt */
			IT83XX_USBPD_TCDCR(port) |=
				USBPD_REG_PLUG_IN_OUT_DETECT_STAT;
			task_set_event(PD_PORT_TO_TASK_ID(port),
				PD_EVENT_CC, 0);
		}
	}
}
#endif

int __ram_code intc_get_ec_int(void)
{
	extern volatile int ec_int;
	return ec_int;
}

void intc_cpu_int_group_5(void)
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

void intc_cpu_int_group_4(void)
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

void intc_cpu_int_group_12(void)
{
	/* Determine interrupt number. */
	int intc_group_12 = intc_get_ec_int();

	switch (intc_group_12) {
#ifdef CONFIG_PECI
	case IT83XX_IRQ_PECI:
		peci_interrupt();
		break;
#endif
#ifdef CONFIG_HOSTCMD_ESPI
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
	case IT83XX_IRQ_SPI_SLAVE:
		spi_slv_int_handler();
		break;
#endif
	default:
		break;
	}
}
DECLARE_IRQ(CPU_INT_GROUP_12, intc_cpu_int_group_12, 2);

void intc_cpu_int_group_7(void)
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

void intc_cpu_int_group_6(void)
{
	/* Determine interrupt number. */
	int intc_group_6 = intc_get_ec_int();

	switch (intc_group_6) {
#if defined(CONFIG_I2C_MASTER) || defined(CONFIG_I2C_SLAVE)
	case IT83XX_IRQ_SMB_A:
#ifdef CONFIG_I2C_SLAVE
		if (IT83XX_SMB_SFFCTL & IT83XX_SMB_SAFE)
			i2c_slv_interrupt(IT83XX_I2C_CH_A);
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
#ifdef CONFIG_I2C_SLAVE
		if (!(IT83XX_I2C_CTR(3) & IT83XX_I2C_MODE))
			i2c_slv_interrupt(IT83XX_I2C_CH_D);
		else
#endif
			i2c_interrupt(IT83XX_I2C_CH_D);
		break;

	case IT83XX_IRQ_SMB_E:
#ifdef CONFIG_I2C_SLAVE
		if (!(IT83XX_I2C_CTR(0) & IT83XX_I2C_MODE))
			i2c_slv_interrupt(IT83XX_I2C_CH_E);
		else
#endif
			i2c_interrupt(IT83XX_I2C_CH_E);
		break;

	case IT83XX_IRQ_SMB_F:
#ifdef CONFIG_I2C_SLAVE
		if (!(IT83XX_I2C_CTR(1) & IT83XX_I2C_MODE))
			i2c_slv_interrupt(IT83XX_I2C_CH_F);
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
