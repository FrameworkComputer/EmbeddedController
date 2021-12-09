/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Richtek RT1718S USB-C Power Path Controller */
#include "atomic.h"
#include "common.h"
#include "config.h"
#include "console.h"
#include "driver/ppc/rt1718s.h"
#include "driver/tcpm/tcpci.h"
#include "usbc_ppc.h"
#include "util.h"


#define RT1718S_FLAGS_SOURCE_ENABLED BIT(0)
static atomic_t flags[CONFIG_USB_PD_PORT_MAX_COUNT];

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static int read_reg(uint8_t port, int reg, int *val)
{
	if (reg > 0xFF) {
		return i2c_read_offset16(
			ppc_chips[port].i2c_port,
			ppc_chips[port].i2c_addr_flags,
			reg, val, 1);
	} else {
		return i2c_read8(
			ppc_chips[port].i2c_port,
			ppc_chips[port].i2c_addr_flags,
			reg, val);
	}
}

static int write_reg(uint8_t port, int reg, int val)
{
	if (reg > 0xFF) {
		return i2c_write_offset16(
			ppc_chips[port].i2c_port,
			ppc_chips[port].i2c_addr_flags,
			reg, val, 1);
	} else {
		return i2c_write8(
			ppc_chips[port].i2c_port,
			ppc_chips[port].i2c_addr_flags,
			reg, val);
	}
}

static int update_bits(int port, int reg, int mask, int val)
{
	int reg_val;

	if (mask == 0xFF)
		return write_reg(port, reg, val);

	RETURN_ERROR(read_reg(port, reg, &reg_val));

	reg_val &= (~mask);
	reg_val |= (mask & val);
	return write_reg(port, reg, reg_val);
}

static int rt1718s_is_sourcing_vbus(int port)
{
	return (flags[port] & RT1718S_FLAGS_SOURCE_ENABLED);
}

static int rt1718s_vbus_source_enable(int port, int enable)
{
	atomic_t prev_flag;

	if (enable)
		prev_flag = atomic_or(&flags[port],
				RT1718S_FLAGS_SOURCE_ENABLED);
	else
		prev_flag = atomic_clear_bits(&flags[port],
				RT1718S_FLAGS_SOURCE_ENABLED);

	/* Return if status doesn't change */
	if (!!(prev_flag & RT1718S_FLAGS_SOURCE_ENABLED) == !!enable)
		return EC_SUCCESS;

	RETURN_ERROR(tcpm_set_src_ctrl(port, enable));

#if defined(CONFIG_USB_CHARGER) && defined(CONFIG_USB_PD_VBUS_DETECT_PPC)
	/*
	 * Since the VBUS state could be changing here, need to wake the
	 * USB_CHG_N task so that BC 1.2 detection will be triggered.
	 */
	usb_charger_vbus_change(port, enable);
#endif

	return EC_SUCCESS;
}

static int rt1718s_vbus_sink_enable(int port, int enable)
{
	return tcpm_set_snk_ctrl(port, enable);
}

static int rt1718s_discharge_vbus(int port, int enable)
{
	return update_bits(port,
		TCPC_REG_POWER_CTRL,
		TCPC_REG_POWER_CTRL_FORCE_DISCHARGE,
		enable ? 0xFF : 0x00);
}

#ifdef CONFIG_CMD_PPC_DUMP
static int rt1718s_dump(int port)
{
	for (int i = 0; i <= 0xEF; i++) {
		int val = 0;
		int rt = read_reg(port, i, &val);

		if (i % 16 == 0)
			CPRINTF("%02X: ", i);
		if (rt)
			CPRINTF("-- ");
		else
			CPRINTF("%02X ", val);
		if (i % 16 == 15)
			CPRINTF("\n");
	}
	for (int i = 0xF200; i <= 0xF2CF; i++) {
		int val = 0;
		int rt = read_reg(port, i, &val);

		if (i % 16 == 0)
			CPRINTF("%04X: ", i);
		if (rt)
			CPRINTF("-- ");
		else
			CPRINTF("%02X ", val);
		if (i % 16 == 15)
			CPRINTF("\n");
	}

	return EC_SUCCESS;
}
#endif /* defined(CONFIG_CMD_PPC_DUMP) */

#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
static int rt1718s_is_vbus_present(int port)
{
	__maybe_unused static atomic_t vbus_prev[CONFIG_USB_PD_PORT_MAX_COUNT];
	int status, vbus;

	if (read_reg(port, TCPC_REG_POWER_STATUS, &status))
		return 0;

	vbus = !!(status & TCPC_REG_POWER_STATUS_VBUS_PRES);

#ifdef CONFIG_USB_CHARGER
	if (!!(vbus_prev[port] != vbus))
		usb_charger_vbus_change(port, vbus);

	if (vbus)
		atomic_or(&vbus_prev[port], 1);
	else
		atomic_clear(&vbus_prev[port]);
#endif

	return vbus;
}
#endif

int rt1718s_frs_init(int port)
{
	/* Set Rx frs unmasked */
	RETURN_ERROR(update_bits(port, RT1718S_RT_MASK1,
				 RT1718S_RT_MASK1_M_RX_FRS, 0xFF));
	return EC_SUCCESS;
}

static int rt1718s_init(int port)
{
	atomic_clear(&flags[port]);

	if (IS_ENABLED(CONFIG_USB_PD_FRS_PPC))
		RETURN_ERROR(rt1718s_frs_init(port));

	return EC_SUCCESS;
}

#ifdef CONFIG_USBC_PPC_POLARITY
static int rt1718s_set_polarity(int port, int polarity)
{
	return tcpci_tcpm_set_polarity(port, polarity);
}
#endif

int rt1718s_set_frs_enable(int port, int enable)
{
	/*
	 * Use write instead of update to save 2 i2c read.
	 * Assume other bits are at their reset value.
	 */
	int frs_ctrl2 = 0x10, vbus_ctrl_en = 0x3F;

	if (enable) {
		frs_ctrl2 |= RT1718S_FRS_CTRL2_RX_FRS_EN;
		frs_ctrl2 |= RT1718S_FRS_CTRL2_VBUS_FRS_EN;

		vbus_ctrl_en |= RT1718S_VBUS_CTRL_EN_GPIO2_VBUS_PATH_EN;
		vbus_ctrl_en |= RT1718S_VBUS_CTRL_EN_GPIO1_VBUS_PATH_EN;
	}

	RETURN_ERROR(write_reg(port, RT1718S_FRS_CTRL2, frs_ctrl2));
	RETURN_ERROR(write_reg(port, RT1718S_VBUS_CTRL_EN, vbus_ctrl_en));
	return EC_SUCCESS;
}

const struct ppc_drv rt1718s_ppc_drv = {
	.init = &rt1718s_init,
	.is_sourcing_vbus = &rt1718s_is_sourcing_vbus,
	.vbus_sink_enable = &rt1718s_vbus_sink_enable,
	.vbus_source_enable = &rt1718s_vbus_source_enable,
#ifdef CONFIG_CMD_PPC_DUMP
	.reg_dump = &rt1718s_dump,
#endif /* defined(CONFIG_CMD_PPC_DUMP) */

#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
	.is_vbus_present = &rt1718s_is_vbus_present,
#endif /* defined(CONFIG_USB_PD_VBUS_DETECT_PPC) */
	.discharge_vbus = &rt1718s_discharge_vbus,
#ifdef CONFIG_USBC_PPC_POLARITY
	.set_polarity = &rt1718s_set_polarity,
#endif
#ifdef CONFIG_USBC_PPC_VCONN
	.set_vconn = &tcpci_tcpm_set_vconn,
#endif
#ifdef CONFIG_USB_PD_FRS_PPC
	.set_frs_enable = rt1718s_set_frs_enable,
#endif
};
