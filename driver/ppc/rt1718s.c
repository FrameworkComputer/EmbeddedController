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
	if (enable)
		atomic_or(&flags[port], RT1718S_FLAGS_SOURCE_ENABLED);
	else
		atomic_clear_bits(&flags[port], RT1718S_FLAGS_SOURCE_ENABLED);

	return tcpm_set_src_ctrl(port, enable);
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
	int status;
	int rv = read_reg(port, TCPC_REG_POWER_STATUS, &status);

	return (rv == 0) && (status & TCPC_REG_POWER_STATUS_VBUS_PRES);
}
#endif

static int rt1718s_init(int port)
{
	atomic_clear(&flags[port]);

	return EC_SUCCESS;
}

#ifdef CONFIG_USBC_PPC_POLARITY
static int rt1718s_set_polarity(int port, int polarity)
{
	return tcpci_tcpm_set_polarity(port, polarity);
}
#endif

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
};
