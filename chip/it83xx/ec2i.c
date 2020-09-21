/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* EC2I control module for IT83xx. */

#include "common.h"
#include "console.h"
#include "ec2i_chip.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

static const struct ec2i_t keyboard_settings[] = {
	/* Select logical device 06h(keyboard) */
	{HOST_INDEX_LDN, LDN_KBC_KEYBOARD},
	/* Set IRQ=01h for logical device */
	{HOST_INDEX_IRQNUMX, 0x01},
	/* Configure IRQTP for KBC. */
#ifdef CONFIG_HOSTCMD_ESPI
	/*
	 * Interrupt request type select (IRQTP) for KBC.
	 * bit 1, 0: IRQ request is buffered and applied to SERIRQ
	 *        1: IRQ request is inverted before being applied to SERIRQ
	 * bit 0, 0: Edge triggered mode
	 *        1: Level triggered mode
	 *
	 * SERIRQ# is by default deasserted level high. However, when using
	 * eSPI, SERIRQ# is routed over virtual wire as interrupt event. As
	 * per eSPI base spec (doc#327432), all virtual wire interrupt events
	 * are deasserted level low. Thus, it is necessary to configure this
	 * interrupt as inverted. ITE hardware takes care of routing the SERIRQ#
	 * signal appropriately over eSPI / LPC depending upon the selected
	 * mode.
	 *
	 * Additionally, this interrupt is configured as edge-triggered on the
	 * host side. So, match the trigger mode on the EC side as well.
	 */
	{HOST_INDEX_IRQTP, 0x02},
#endif
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},
};

#ifdef CONFIG_IT83XX_ENABLE_MOUSE_DEVICE
static const struct ec2i_t mouse_settings[] = {
	/* Select logical device 05h(mouse) */
	{HOST_INDEX_LDN, LDN_KBC_MOUSE},
	/* Set IRQ=0Ch for logical device */
	{HOST_INDEX_IRQNUMX, 0x0C},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},
};
#endif

static const struct ec2i_t pm1_settings[] = {
	/* Select logical device 11h(PM1 ACPI) */
	{HOST_INDEX_LDN, LDN_PMC1},
	/* Set IRQ=00h for logical device */
	{HOST_INDEX_IRQNUMX, 0x00},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},
};

static const struct ec2i_t pm2_settings[] = {
	/* Select logical device 12h(PM2) */
	{HOST_INDEX_LDN, LDN_PMC2},
	/* I/O Port Base Address 200h/204h */
	{HOST_INDEX_IOBAD0_MSB, 0x02},
	{HOST_INDEX_IOBAD0_LSB, 0x00},
	{HOST_INDEX_IOBAD1_MSB, 0x02},
	{HOST_INDEX_IOBAD1_LSB, 0x04},
	/* Set IRQ=00h for logical device */
	{HOST_INDEX_IRQNUMX, 0x00},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},
};

static const struct ec2i_t smfi_settings[] = {
	/* Select logical device 0Fh(SMFI) */
	{HOST_INDEX_LDN, LDN_SMFI},
	/* H2RAM LPC I/O cycle Dxxx */
	{HOST_INDEX_DSLDC6, 0x00},
	/* Enable H2RAM LPC I/O cycle */
	{HOST_INDEX_DSLDC7, 0x01},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},
};

/*
 * PM3 is enabled and base address is set to 80h so that we are able to get an
 * interrupt when host outputs data to port 80.
 */
static const struct ec2i_t pm3_settings[] = {
	/* Select logical device 17h(PM3) */
	{HOST_INDEX_LDN, LDN_PMC3},
	/* I/O Port Base Address 80h */
	{HOST_INDEX_IOBAD0_MSB, 0x00},
	{HOST_INDEX_IOBAD0_LSB, 0x80},
	{HOST_INDEX_IOBAD1_MSB, 0x00},
	{HOST_INDEX_IOBAD1_LSB, 0x00},
	/* Set IRQ=00h for logical device */
	{HOST_INDEX_IRQNUMX, 0x00},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},
};

/*
 * This logical device is not enabled, however P80L* settings need to be
 * performed on this logical device to ensure that port80 BRAM index is
 * initialized correctly.
 */
static const struct ec2i_t rtct_settings[] = {
	/* Select logical device 10h(RTCT) */
	{HOST_INDEX_LDN, LDN_RTCT},
	/* P80L Begin Index */
	{HOST_INDEX_DSLDC4, P80L_P80LB},
	/* P80L End Index */
	{HOST_INDEX_DSLDC5, P80L_P80LE},
	/* P80L Current Index */
	{HOST_INDEX_DSLDC6, P80L_P80LC},
};

#ifdef CONFIG_UART_HOST
static const struct ec2i_t uart2_settings[] = {
	/* Select logical device 2h(UART2) */
	{HOST_INDEX_LDN, LDN_UART2},
	/*
	 * I/O port base address is 2F8h.
	 * Host can use LPC I/O port 0x2F8 ~ 0x2FF to access UART2.
	 * See specification 7.24.4 for more detial.
	 */
	{HOST_INDEX_IOBAD0_MSB, 0x02},
	{HOST_INDEX_IOBAD0_LSB, 0xF8},
	/* IRQ number is 3 */
	{HOST_INDEX_IRQNUMX, 0x03},
	/*
	 * Interrupt Request Type Select
	 * bit1, 0: IRQ request is buffered and applied to SERIRQ.
	 *       1: IRQ request is inverted before being applied to SERIRQ.
	 * bit0, 0: Edge triggered mode.
	 *       1: Level triggered mode.
	 */
	{HOST_INDEX_IRQTP, 0x02},
	/* Enable logical device */
	{HOST_INDEX_LDA, 0x01},
};
#endif

/* EC2I access index/data port */
enum ec2i_access {
	/* index port */
	EC2I_ACCESS_INDEX = 0,
	/* data port */
	EC2I_ACCESS_DATA = 1,
};

enum ec2i_status_mask {
	/* 1: EC read-access is still processing. */
	EC2I_STATUS_CRIB = BIT(1),
	/* 1: EC write-access is still processing with IHD register. */
	EC2I_STATUS_CWIB = BIT(2),
	EC2I_STATUS_ALL  = (EC2I_STATUS_CRIB | EC2I_STATUS_CWIB),
};

static int ec2i_wait_status_bit_cleared(enum ec2i_status_mask mask)
{
	/* delay ~15.25us */
	IT83XX_GCTRL_WNCKR = 0;

	return (IT83XX_EC2I_IBCTL & mask);
}

static enum ec2i_message ec2i_write_pnpcfg(enum ec2i_access sel, uint8_t data)
{
	int rv = EC_ERROR_UNKNOWN;

	/* bit1 : VCC power on */
	if (IT83XX_SWUC_SWCTL1 & BIT(1)) {
		/*
		 * Wait that both CRIB and CWIB bits in IBCTL register
		 * are cleared.
		 */
		rv = ec2i_wait_status_bit_cleared(EC2I_STATUS_ALL);
		if (!rv) {
			/* Set indirect host I/O offset. */
			IT83XX_EC2I_IHIOA = sel;
			/* Write the data to IHD register */
			IT83XX_EC2I_IHD = data;
			/* Enable EC access to the PNPCFG registers */
			IT83XX_EC2I_IBMAE |= BIT(0);
			/* bit0: EC to I-Bus access enabled. */
			IT83XX_EC2I_IBCTL |= BIT(0);
			/* Wait the CWIB bit in IBCTL cleared. */
			rv = ec2i_wait_status_bit_cleared(EC2I_STATUS_CWIB);
			/* Disable EC access to the PNPCFG registers. */
			IT83XX_EC2I_IBMAE &= ~BIT(0);
			/* Disable EC to I-Bus access. */
			IT83XX_EC2I_IBCTL &= ~BIT(0);
		}
	}

	return rv ? EC2I_WRITE_ERROR : EC2I_WRITE_SUCCESS;
}

static enum ec2i_message ec2i_read_pnpcfg(enum ec2i_access sel)
{
	int rv = EC_ERROR_UNKNOWN;
	uint8_t ihd = 0;

	/* bit1 : VCC power on */
	if (IT83XX_SWUC_SWCTL1 & BIT(1)) {
		/*
		 * Wait that both CRIB and CWIB bits in IBCTL register
		 * are cleared.
		 */
		rv = ec2i_wait_status_bit_cleared(EC2I_STATUS_ALL);
		if (!rv) {
			/* Set indirect host I/O offset. */
			IT83XX_EC2I_IHIOA = sel;
			/* Enable EC access to the PNPCFG registers */
			IT83XX_EC2I_IBMAE |= BIT(0);
			/* bit1: a read-action */
			IT83XX_EC2I_IBCTL |= BIT(1);
			/* bit0: EC to I-Bus access enabled. */
			IT83XX_EC2I_IBCTL |= BIT(0);
			/* Wait the CRIB bit in IBCTL cleared. */
			rv = ec2i_wait_status_bit_cleared(EC2I_STATUS_CRIB);
			/* Read the data from IHD register */
			ihd = IT83XX_EC2I_IHD;
			/* Disable EC access to the PNPCFG registers. */
			IT83XX_EC2I_IBMAE &= ~BIT(0);
			/* Disable EC to I-Bus access. */
			IT83XX_EC2I_IBCTL &= ~BIT(0);
		}
	}

	return rv ? EC2I_READ_ERROR : (EC2I_READ_SUCCESS + ihd);
}

/* EC2I read */
enum ec2i_message ec2i_read(enum host_pnpcfg_index index)
{
	enum ec2i_message ret = EC2I_READ_ERROR;
	/* critical section with interrupts off */
	uint32_t int_mask = read_clear_int_mask();

	/* Set index */
	if (ec2i_write_pnpcfg(EC2I_ACCESS_INDEX, index) == EC2I_WRITE_SUCCESS)
		/* read data port */
		ret = ec2i_read_pnpcfg(EC2I_ACCESS_DATA);
	/* restore interrupts */
	set_int_mask(int_mask);

	return ret;
}

/* EC2I write */
enum ec2i_message ec2i_write(enum host_pnpcfg_index index, uint8_t data)
{
	enum ec2i_message ret = EC2I_WRITE_ERROR;
	/* critical section with interrupts off */
	uint32_t int_mask = read_clear_int_mask();

	/* Set index */
	if (ec2i_write_pnpcfg(EC2I_ACCESS_INDEX, index) == EC2I_WRITE_SUCCESS)
		/* Set data */
		ret = ec2i_write_pnpcfg(EC2I_ACCESS_DATA, data);
	/* restore interrupts */
	set_int_mask(int_mask);

	return ret;
}

static void pnpcfg_configure(const struct ec2i_t *settings, size_t entries)
{
	size_t i;

	for (i = 0; i < entries; i++) {
		if (ec2i_write(settings[i].index_port, settings[i].data_port) ==
		    EC2I_WRITE_ERROR) {
			ccprints("Failed to apply %zd", i);
			break;
		}
	}
}

#define PNPCFG(_s)						\
	pnpcfg_configure(_s##_settings, ARRAY_SIZE(_s##_settings))

static void pnpcfg_init(void)
{
	/* Host access is disabled */
	IT83XX_EC2I_LSIOHA |= 0x3;

	PNPCFG(keyboard);
#ifdef CONFIG_IT83XX_ENABLE_MOUSE_DEVICE
	PNPCFG(mouse);
#endif
	PNPCFG(pm1);
	PNPCFG(pm2);
	PNPCFG(smfi);
	PNPCFG(pm3);
	PNPCFG(rtct);
#ifdef CONFIG_UART_HOST
	PNPCFG(uart2);
#endif
}
DECLARE_HOOK(HOOK_INIT, pnpcfg_init, HOOK_PRIO_DEFAULT);
