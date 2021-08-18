/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Type-C port manager for Parade PS8XXX with integrated superspeed muxes.
 *
 * Supported TCPCs:
 * - PS8705
 * - PS8751
 * - PS8755
 * - PS8805
 * - PS8815
 */

#include "common.h"
#include "console.h"
#include "ps8xxx.h"
#include "tcpm/tcpci.h"
#include "tcpm/tcpm.h"
#include "timer.h"
#include "usb_mux.h"
#include "usb_pd.h"

#if !defined(CONFIG_USB_PD_TCPM_PS8705) && \
	!defined(CONFIG_USB_PD_TCPM_PS8751) && \
	!defined(CONFIG_USB_PD_TCPM_PS8755) && \
	!defined(CONFIG_USB_PD_TCPM_PS8805) && \
	!defined(CONFIG_USB_PD_TCPM_PS8815)
#error "Unsupported PS8xxx TCPC."
#endif

#if !defined(CONFIG_USB_PD_TCPM_TCPCI) || \
	!defined(CONFIG_USB_PD_TCPM_MUX) || \
	!defined(CONFIG_USBC_SS_MUX)

#error "PS8XXX is using a standard TCPCI interface with integrated mux control"
#error "Please upgrade your board configuration"

#endif

#ifdef CONFIG_USB_PD_TCPM_PS8751
/* PS8751 cannot run with PD 3.0 (see b/148554997 for details) */
#if defined(CONFIG_USB_PD_REV30)
#error "PS8751 cannot run with PD 3.0.  Fall back to using PD 2.0"
#endif

#endif /* CONFIG_USB_PD_TCPM_PS8751 */

#ifdef CONFIG_USB_PD_TCPM_PS8751_CUSTOM_MUX_DRIVER
#if !defined(CONFIG_USB_PD_TCPM_PS8751)
#error "Custom MUX driver is available only for PS8751"
#endif

#endif /* CONFIG_USB_PD_TCPM_PS8751_CUSTOM_MUX_DRIVER */

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#define PS8XXX_I2C_RECOVERY_DELAY_MS	10

/*
 * The product_id per ports here is expected to be set in callback function -
 * .init of tcpm_drv by calling board_get_ps8xxx_product_id().
 *
 * In case of CONFIG_USB_PD_TCPM_MULTI_PS8XXX enabled, board code should
 * override the board_get_ps8xxx_product_id() for getting the correct id.
 */
static uint16_t product_id[CONFIG_USB_PD_PORT_MAX_COUNT];

/*
 * Revisions A1 and A0 of the PS8815 can corrupt the transmit buffer when
 * updating the transmit buffer within 1ms of writing the ROLE_CONTROL
 * register. When this version of silicon is detected, add a 1ms delay before
 * all writes to the transmit buffer.
 *
 * See b/171430855 for details.
 */
static uint8_t ps8xxx_role_control_delay_ms[CONFIG_USB_PD_PORT_MAX_COUNT];

/*
 * b/178664884, on PS8815, firmware revision 0x10 and older can report an
 * incorrect value on the the CC lines. This flag controls when to apply
 * the workaround.
 */
static bool ps8815_disable_rp_detect[CONFIG_USB_PD_PORT_MAX_COUNT];
static bool ps8815_disconnected[CONFIG_USB_PD_PORT_MAX_COUNT];
/*
 * timestamp of the next possible toggle to ensure the 2-ms spacing
 * between IRQ_HPD.
 */
static uint64_t hpd_deadline[CONFIG_USB_PD_PORT_MAX_COUNT];

void ps8xxx_wake_from_standby(const struct usb_mux *me);

#if defined(CONFIG_USB_PD_TCPM_PS8705) || \
	defined(CONFIG_USB_PD_TCPM_PS8751) || \
	defined(CONFIG_USB_PD_TCPM_PS8755) || \
	defined(CONFIG_USB_PD_TCPM_PS8805)
/*
 * DCI is enabled by default and burns about 40 mW when the port is in
 * USB2 mode or when a C-to-A dongle is attached, so force it off.
 */

static int ps8xxx_addr_dci_disable(int port, int i2c_addr, int i2c_reg)
{
	int status;
	int dci;

	status = tcpc_addr_read(port, i2c_addr, i2c_reg, &dci);
	if (status != EC_SUCCESS)
		return status;
	if ((dci & PS8XXX_REG_MUX_USB_DCI_CFG_MODE_MASK) !=
	    PS8XXX_REG_MUX_USB_DCI_CFG_MODE_OFF) {
		dci &= ~PS8XXX_REG_MUX_USB_DCI_CFG_MODE_MASK;
		dci |= PS8XXX_REG_MUX_USB_DCI_CFG_MODE_OFF;
		if (tcpc_addr_write(port, i2c_addr, i2c_reg, dci) != EC_SUCCESS)
			return status;
	}
	return EC_SUCCESS;
}
#endif /* CONFIG_USB_PD_TCPM_PS875[15] || CONFIG_USB_PD_TCPM_PS8[78]05 */

#if defined(CONFIG_USB_PD_TCPM_PS8705) || \
	defined(CONFIG_USB_PD_TCPM_PS8755) || \
	defined(CONFIG_USB_PD_TCPM_PS8805)
static int ps8705_dci_disable(int port)
{
	int p1_addr;
	int p3_addr;
	int regval;
	int rv;

	/* Enable access to debug pages. */
	p3_addr = tcpc_config[port].i2c_info.addr_flags;
	rv = tcpc_addr_read(port, p3_addr, PS8XXX_REG_I2C_DEBUGGING_ENABLE,
			    &regval);
	if (rv)
		return rv;

	rv = tcpc_addr_write(port, p3_addr, PS8XXX_REG_I2C_DEBUGGING_ENABLE,
			     PS8XXX_REG_I2C_DEBUGGING_ENABLE_ON);

	/* Disable Auto DCI */
	p1_addr = PS8751_P3_TO_P1_FLAGS(p3_addr);
	rv = ps8xxx_addr_dci_disable(port, p1_addr,
				     PS8XXX_P1_REG_MUX_USB_DCI_CFG);

	/*
	 * PS8705/PS8755/PS8805 will automatically re-assert bit:0 on the
	 * PS8XXX_REG_I2C_DEBUGGING_ENABLE register.
	 */
	return rv;
}
#endif /* CONFIG_USB_PD_TCPM_PS8755 || CONFIG_USB_PD_TCPM_PS8[78]05 */

#ifdef CONFIG_USB_PD_TCPM_PS8751
static int ps8751_dci_disable(int port)
{
	int p3_addr;

	p3_addr = tcpc_config[port].i2c_info.addr_flags;
	return ps8xxx_addr_dci_disable(port, p3_addr,
				       PS8751_REG_MUX_USB_DCI_CFG);
}
#endif /* CONFIG_USB_PD_TCPM_PS8751 */

#ifdef CONFIG_USB_PD_TCPM_PS8815
static int ps8815_dci_disable(int port)
{
	/* DCI is disabled on the ps8815 */
	return EC_SUCCESS;
}
#endif /* CONFIG_USB_PD_TCPM_PS8815 */

#ifdef CONFIG_USB_PD_TCPM_PS8805
static int ps8805_gpio_mask[] = {
	PS8805_REG_GPIO_0,
	PS8805_REG_GPIO_1,
	PS8805_REG_GPIO_2,
};

int ps8805_gpio_set_level(int port, enum ps8805_gpio signal, int level)
{
	int rv;
	int regval;
	int mask;

	if (signal >= PS8805_GPIO_NUM)
		return EC_ERROR_INVAL;

	rv = i2c_read8(tcpc_config[port].i2c_info.port,
		       PS8805_VENDOR_DEFINED_I2C_ADDR,
		       PS8805_REG_GPIO_CONTROL, &regval);
	if (rv)
		return rv;

	mask = ps8805_gpio_mask[signal];
	if (level)
		regval |= mask;
	else
		regval &= ~mask;

	return i2c_write8(tcpc_config[port].i2c_info.port,
		       PS8805_VENDOR_DEFINED_I2C_ADDR,
		       PS8805_REG_GPIO_CONTROL, regval);
}

int ps8805_gpio_get_level(int port, enum ps8805_gpio signal, int *level)
{
	int regval;
	int rv;

	if (signal >= PS8805_GPIO_NUM)
		return EC_ERROR_INVAL;

	rv = i2c_read8(tcpc_config[port].i2c_info.port,
		       PS8805_VENDOR_DEFINED_I2C_ADDR,
		       PS8805_REG_GPIO_CONTROL, &regval);
	if (rv)
		return rv;
	*level = !!(regval & ps8805_gpio_mask[signal]);

	return EC_SUCCESS;
}
#endif /* CONFIG_USB_PD_TCPM_PS8805 */

enum ps8xxx_variant_regs {
	REG_FIRST_INDEX = 0,
	/* NOTE: The rev will read as 0x00 if the FW has malfunctioned. */
	REG_FW_VER = REG_FIRST_INDEX,
	REG_MAX_COUNT,
};

struct ps8xxx_variant_map {
	int product_id;
	int (*dci_disable_ptr)(int port);
	int reg_map[REG_MAX_COUNT];
};

/*
 * variant_map here is leveraged to lookup from ps8xxx_variant_regs to i2c
 * register and corresponding dci_disable function based on product_id.
 */
static struct ps8xxx_variant_map variant_map[] = {
#ifdef CONFIG_USB_PD_TCPM_PS8705
	{
		PS8705_PRODUCT_ID,
		ps8705_dci_disable,
		{
		 [REG_FW_VER] = 0x82,
		}
	},
#endif
#ifdef CONFIG_USB_PD_TCPM_PS8751
	{
		PS8751_PRODUCT_ID,
		ps8751_dci_disable,
		{
			[REG_FW_VER] = 0x90,
		}
	},
#endif
#ifdef CONFIG_USB_PD_TCPM_PS8755
	{
		PS8755_PRODUCT_ID,
		ps8705_dci_disable,
		{
			[REG_FW_VER] = 0x82,
		}
	},
#endif
#ifdef CONFIG_USB_PD_TCPM_PS8805
	{
		PS8805_PRODUCT_ID,
		ps8705_dci_disable,
		{
			[REG_FW_VER] = 0x82,
		}
	},
#endif
#ifdef CONFIG_USB_PD_TCPM_PS8815
	{
		PS8815_PRODUCT_ID,
		ps8815_dci_disable,
		{
			[REG_FW_VER] = 0x82,
		}
	},
#endif
};

static int get_reg_by_product(const int port,
			      const enum ps8xxx_variant_regs reg)
{
	int i;

	if (reg < REG_FIRST_INDEX || reg >= REG_MAX_COUNT)
		return INT32_MAX;

	for (i = 0; i < ARRAY_SIZE(variant_map); i++) {
		if (product_id[port] ==
		      variant_map[i].product_id) {
			return variant_map[i].reg_map[reg];
		}
	}

	CPRINTS("%s: failed to get register number by product_id.", __func__);
	return INT32_MAX;
}

static int dp_set_hpd(const struct usb_mux *me, int enable)
{
	int reg;
	int rv;

	rv = mux_read(me, MUX_IN_HPD_ASSERTION_REG, &reg);
	if (rv)
		return rv;
	if (enable)
		reg |= IN_HPD;
	else
		reg &= ~IN_HPD;
	return mux_write(me, MUX_IN_HPD_ASSERTION_REG, reg);
}

static int dp_set_irq(const struct usb_mux *me, int enable)
{
	int reg;
	int rv;

	rv = mux_read(me, MUX_IN_HPD_ASSERTION_REG, &reg);
	if (rv)
		return rv;
	if (enable)
		reg |= HPD_IRQ;
	else
		reg &= ~HPD_IRQ;
	return mux_write(me, MUX_IN_HPD_ASSERTION_REG, reg);
}

__overridable
uint16_t board_get_ps8xxx_product_id(int port)
{
	/* Board supporting multiple chip sources in ps8xxx.c MUST override this
	 * function to judge the real chip source for this board. For example,
	 * SKU ID / strappings / provisioning in the factory can be the ways.
	 */

	if (IS_ENABLED(CONFIG_USB_PD_TCPM_MULTI_PS8XXX)) {
		CPRINTS("%s: board should override this function.", __func__);
		return 0;
	} else if (IS_ENABLED(CONFIG_USB_PD_TCPM_PS8705)) {
		return PS8705_PRODUCT_ID;
	} else if (IS_ENABLED(CONFIG_USB_PD_TCPM_PS8751)) {
		return PS8751_PRODUCT_ID;
	} else if (IS_ENABLED(CONFIG_USB_PD_TCPM_PS8755)) {
		return PS8755_PRODUCT_ID;
	} else if (IS_ENABLED(CONFIG_USB_PD_TCPM_PS8805)) {
		return PS8805_PRODUCT_ID;
	} else if (IS_ENABLED(CONFIG_USB_PD_TCPM_PS8815)) {
		return PS8815_PRODUCT_ID;
	}

	CPRINTS("%s: Any new product id is not defined here?", __func__);
	return 0;
}

bool check_ps8755_chip(int port)
{
	int val;
	int p0_addr;
	int status;
	bool is_ps8755 = false;

	p0_addr = PS8751_P3_TO_P0_FLAGS(tcpc_config[port].i2c_info.addr_flags);
	status = tcpc_addr_read(port, p0_addr, PS8755_P0_REG_SM, &val);
	if (status == EC_SUCCESS && val == PS8755_P0_REG_SM_VALUE)
		is_ps8755 = true;

	return is_ps8755;
}

void ps8xxx_tcpc_update_hpd_status(const struct usb_mux *me,
				   int hpd_lvl, int hpd_irq)
{
	int port = me->usb_port;

	if (IS_ENABLED(CONFIG_USB_PD_TCPM_PS8751_CUSTOM_MUX_DRIVER) &&
	    product_id[me->usb_port] == PS8751_PRODUCT_ID &&
	    me->flags & USB_MUX_FLAG_NOT_TCPC)
		ps8xxx_wake_from_standby(me);

	dp_set_hpd(me, hpd_lvl);

	if (hpd_irq) {
		uint64_t now = get_time().val;
		/* wait for the minimum spacing between IRQ_HPD if needed */
		if (now < hpd_deadline[port])
			usleep(hpd_deadline[port] - now);

		dp_set_irq(me, 0);
		usleep(HPD_DSTREAM_DEBOUNCE_IRQ);
		dp_set_irq(me, hpd_irq);
	}
	/* enforce 2-ms delay between HPD pulses */
	hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
}

static int ps8xxx_tcpc_bist_mode_2(int port)
{
	int rv;

	/* Generate BIST for 50ms. */
	rv = tcpc_write(port,
		PS8XXX_REG_BIST_CONT_MODE_BYTE0, PS8751_BIST_COUNTER_BYTE0);
	rv |= tcpc_write(port,
		PS8XXX_REG_BIST_CONT_MODE_BYTE1, PS8751_BIST_COUNTER_BYTE1);
	rv |= tcpc_write(port,
		PS8XXX_REG_BIST_CONT_MODE_BYTE2, PS8751_BIST_COUNTER_BYTE2);

	/* Auto stop */
	rv |= tcpc_write(port, PS8XXX_REG_BIST_CONT_MODE_CTR, 0);

	/* Start BIST MODE 2 */
	rv |= tcpc_write(port, TCPC_REG_TRANSMIT, TCPC_TX_BIST_MODE_2);

	return rv;
}

static int ps8xxx_tcpm_transmit(int port, enum tcpm_sop_type type,
			uint16_t header, const uint32_t *data)
{
	if (type == TCPC_TX_BIST_MODE_2)
		return ps8xxx_tcpc_bist_mode_2(port);
	else
		return tcpci_tcpm_transmit(port, type, header, data);
}

static int ps8xxx_tcpm_release(int port)
{
	int version;
	int status;
	int reg = get_reg_by_product(port, REG_FW_VER);

	status = tcpc_read(port, reg, &version);
	if (status != 0) {
		/* wait for chip to wake up */
		msleep(10);
	}

	return tcpci_tcpm_release(port);
}

static void ps8xxx_role_control_delay(int port)
{
	int delay;

	delay = ps8xxx_role_control_delay_ms[port];
	if (delay)
		msleep(delay);
}

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
static int ps8xxx_set_role_ctrl(int port, enum tcpc_drp drp,
	enum tcpc_rp_value rp, enum tcpc_cc_pull pull)
{
	int rv;

	rv = tcpci_set_role_ctrl(port, drp, rp, pull);

	/*
	 * b/171430855 delay 1 ms after ROLE_CONTROL updates to prevent
	 * transmit buffer corruption
	 */
	ps8xxx_role_control_delay(port);

	return rv;
}

static int ps8xxx_tcpc_drp_toggle(int port)
{
	int rv;
	int status;
	int opposite_pull;

	/*
	 * Workaround for PS8805/PS8815, which can't restart Connection
	 * Detection if the partner already presents pull. Now starts with
	 * the opposite pull. Check b/149570002.
	 */
	if (product_id[port] == PS8805_PRODUCT_ID ||
	    product_id[port] == PS8815_PRODUCT_ID) {
		if (ps8815_disable_rp_detect[port]) {
			CPRINTS("TCPC%d: rearm Rp disable detect on connect",
				port);
			ps8815_disconnected[port] = true;
		}

		/* Check CC_STATUS for the current pull */
		rv = tcpc_read(port, TCPC_REG_CC_STATUS, &status);
		if (status & TCPC_REG_CC_STATUS_CONNECT_RESULT_MASK) {
			/* Current pull: Rd */
			opposite_pull = TYPEC_CC_RP;
		} else {
			/* Current pull: Rp */
			opposite_pull = TYPEC_CC_RD;
		}

		/* Set auto drp toggle, starting with the opposite pull */
		rv |= ps8xxx_set_role_ctrl(port, TYPEC_DRP, TYPEC_RP_USB,
			opposite_pull);

		/* Set Look4Connection command */
		rv |= tcpc_write(port, TCPC_REG_COMMAND,
				 TCPC_REG_COMMAND_LOOK4CONNECTION);

		return rv;
	} else {
		return tcpci_tcpc_drp_toggle(port);
	}
}
#endif

#ifdef CONFIG_USB_PD_TCPM_PS8805_FORCE_DID
static int ps8805_make_device_id(int port, int *id)
{
	int p0_addr;
	int val;
	int status;

	p0_addr = PS8751_P3_TO_P0_FLAGS(tcpc_config[port].i2c_info.addr_flags);

	status = tcpc_addr_read(port, p0_addr, PS8805_P0_REG_CHIP_REVISION,
				&val);
	if (status != EC_SUCCESS)
		return status;
	switch (val & 0xF0) {
	case 0x00: /* A2 chip */
		*id = 1;
		break;
	case 0xa0: /* A3 chip */
		*id = 2;
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}
#endif

#ifdef CONFIG_USB_PD_TCPM_PS8815_FORCE_DID
/*
 * Early ps8815 A1 firmware reports 0x0001 in the TCPCI Device ID
 * registers which makes it indistinguishable from A0. This
 * overrides the Device ID based if vendor specific registers
 * identify the chip as A1.
 *
 * See b/159289062.
 */
static int ps8815_make_device_id(int port, int *id)
{
	int p1_addr;
	int val;
	int status;

	/* P1 registers are always accessible on PS8815 */
	p1_addr = PS8751_P3_TO_P1_FLAGS(tcpc_config[port].i2c_info.addr_flags);

	status = tcpc_addr_read16(port, p1_addr, PS8815_P1_REG_HW_REVISION,
				  &val);
	if (status != EC_SUCCESS)
		return status;
	switch (val) {
	case 0x0a00:
		*id = 1;
		break;
	case 0x0a01:
		*id = 2;
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}
#endif

/*
 * The ps8815 can take up to 50ms (FW_INIT_DELAY_MS) to fully wake up
 * from sleep/low power mode - specially when it contains an application
 * block firmware update. When the chip is asleep, the 1st I2C
 * transaction will fail but the chip will begin to wake up within 10ms
 * (I2C_RECOVERY_DELAY_MS). After this delay, I2C transactions succeed,
 * but the firmware is still not fully operational. The way to check if
 * the firmware is ready, is to poll the firmware register for a
 * non-zero value. This logic applies to all ps8xxx family members
 * supported by this driver.
 */

static int ps8xxx_lpm_recovery_delay(int port)
{
	int val;
	int status;
	int fw_reg;
	timestamp_t deadline;

	fw_reg = get_reg_by_product(port, REG_FW_VER);

	deadline = get_time();
	deadline.val += PS8815_FW_INIT_DELAY_MS * 1000;

	val = 0;
	for (;;) {
		status = tcpc_read(port, fw_reg, &val);
		if (status != EC_SUCCESS) {
			/* wait for chip to wake up */
			msleep(PS8XXX_I2C_RECOVERY_DELAY_MS);
			continue;
		}
		if (val != 0)
			break;
		msleep(1);
		if (timestamp_expired(deadline, NULL))
			return EC_ERROR_TIMEOUT;
	}

	return EC_SUCCESS;
}

static int ps8xxx_get_chip_info(int port, int live,
			struct ec_response_pd_chip_info_v1 *chip_info)
{
	int val;
	int reg;
	int rv = tcpci_get_chip_info(port, live, chip_info);

	if (rv != EC_SUCCESS)
		return rv;

	if (chip_info == NULL)
		return EC_SUCCESS;

	if (!live) {
		uint16_t pid;

		pid = board_get_ps8xxx_product_id(port);
		if (pid == 0)
			return EC_ERROR_UNKNOWN;
		product_id[port] = pid;
		chip_info->vendor_id = PS8XXX_VENDOR_ID;
		chip_info->product_id = product_id[port];
	}

#ifdef CONFIG_USB_PD_TCPM_PS8805_FORCE_DID
	if (chip_info->product_id == PS8805_PRODUCT_ID &&
	    chip_info->device_id == 0x0001) {
		rv = ps8805_make_device_id(port, &val);
		if (rv != EC_SUCCESS)
			return rv;
		chip_info->device_id = val;
	}
#endif
#ifdef CONFIG_USB_PD_TCPM_PS8815_FORCE_DID
	if (chip_info->product_id == PS8815_PRODUCT_ID &&
	    chip_info->device_id == 0x0001) {
		rv = ps8815_make_device_id(port, &val);
		if (rv != EC_SUCCESS)
			return rv;
		chip_info->device_id = val;
	}
#endif
	reg = get_reg_by_product(port, REG_FW_VER);
	rv = tcpc_read(port, reg, &val);
	if (rv != EC_SUCCESS)
		return rv;

	chip_info->fw_version_number = val;

	/* Treat unexpected values as error (FW not initiated from reset) */
	if (live && (
	    chip_info->vendor_id != PS8XXX_VENDOR_ID ||
	    chip_info->product_id != board_get_ps8xxx_product_id(port) ||
	    chip_info->fw_version_number == 0))
		return EC_ERROR_UNKNOWN;

#if defined(CONFIG_USB_PD_TCPM_PS8751) && \
	defined(CONFIG_USB_PD_VBUS_DETECT_TCPC)
	/*
	 * Min firmware version of PS8751 to ensure that it can detect Vbus
	 * properly. See b/109769787#comment7
	 */
	chip_info->min_req_fw_version_number = 0x39;
#endif

	return rv;
}

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
static int ps8xxx_enter_low_power_mode(int port)
{
	/*
	 * PS8751 has the auto sleep function that enters low power mode on
	 * its own in ~2 seconds. Other chips don't have it. Stub it out for
	 * PS8751.
	 */
	if (product_id[port] == PS8751_PRODUCT_ID)
		return EC_SUCCESS;

	return tcpci_enter_low_power_mode(port);
}
#endif

static int ps8xxx_dci_disable(int port)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(variant_map); i++) {
		if (product_id[port] == variant_map[i].product_id)
			return variant_map[i].dci_disable_ptr(port);
	}

	CPRINTS("%s: failed to get dci_disable function pointers.", __func__);
	return EC_ERROR_INVAL;
}

__maybe_unused static int ps8815_transmit_buffer_workaround_check(int port)
{
	int p1_addr;
	int val;
	int status;

	if (product_id[port] != PS8815_PRODUCT_ID)
		return EC_SUCCESS;

	/* P1 registers are always accessible on PS8815 */
	p1_addr = PS8751_P3_TO_P1_FLAGS(tcpc_config[port].i2c_info.addr_flags);

	status = tcpc_addr_read16(port, p1_addr, PS8815_P1_REG_HW_REVISION,
				  &val);
	if (status != EC_SUCCESS)
		return status;

	switch (val) {
	case 0x0a00:
	case 0x0a01:
		ps8xxx_role_control_delay_ms[port] = 1;
		break;
	default:
		break;
	}

	return EC_SUCCESS;
}

__maybe_unused static int ps8815_disable_rp_detect_workaround_check(int port)
{
	int val;
	int rv;
	int reg;

	ps8815_disable_rp_detect[port] = false;
	ps8815_disconnected[port] = true;

	reg = get_reg_by_product(port, REG_FW_VER);
	rv = tcpc_read(port, reg, &val);
	if (rv != EC_SUCCESS)
		return rv;

	/*
	 * RP detect is a problem in firmware version 0x10 and older.
	 */
	if (val <= 0x10)
		ps8815_disable_rp_detect[port] = true;

	return EC_SUCCESS;
}

__overridable void board_ps8xxx_tcpc_init(int port)
{}

static int ps8xxx_tcpm_init(int port)
{
	int status;

	product_id[port] = board_get_ps8xxx_product_id(port);

	status = ps8xxx_lpm_recovery_delay(port);
	if (status != EC_SUCCESS) {
		CPRINTS("C%d: init: LPM recovery failed", port);
		return status;
	}

	if (IS_ENABLED(CONFIG_USB_PD_TCPM_PS8815)) {
		status = ps8815_transmit_buffer_workaround_check(port);
		if (status != EC_SUCCESS)
			return status;
		status = ps8815_disable_rp_detect_workaround_check(port);
		if (status != EC_SUCCESS)
			return status;
	}

	board_ps8xxx_tcpc_init(port);

	status = tcpci_tcpm_init(port);
	if (status != EC_SUCCESS)
		return status;

	return ps8xxx_dci_disable(port);
}

#ifdef CONFIG_USB_PD_TCPM_PS8751
/*
 * TODO(twawrzynczak): Remove this workaround when no
 * longer needed.  See: https://issuetracker.google.com/147684491
 *
 * This is a workaround for what appears to be a bug in PS8751 firmware
 * version 0x44.  (Does the bug exist in other PS8751 firmware versions?
 * Should this workaround be limited to only 0x44?)
 *
 * With nothing connected to the port, sometimes after DRP is disabled,
 * the CC_STATUS register reads the CC state incorrectly (reading it
 * as though a port partner is detected), which ends up confusing
 * our TCPM.  The workaround for this seems to be a short sleep and
 * then re-reading the CC state.  In other words, the issue shows up
 * as a short glitch or transient, which an extra read and then a short
 * delay will allow the transient to disappear.
 */
static int ps8751_get_gcc(int port, enum tcpc_cc_voltage_status *cc1,
			 enum tcpc_cc_voltage_status *cc2)
{
	int rv;
	int status;
	rv = tcpc_read(port, TCPC_REG_CC_STATUS, &status);
	if (rv)
		return rv;

	/* Derived empirically */
	usleep(300);

	return tcpci_tcpm_get_cc(port, cc1, cc2);
}
#endif

static int ps8xxx_tcpm_set_cc(int port, int pull)
{
	int rv;

	/*
	 * b/178664884: Before presenting Rp on initial connect, disable
	 * internal function that checks Rp value. This is a workaround
	 * in the PS8815 firmware that reports an incorrect value on the CC
	 * lines.
	 *
	 * The PS8815 self-clears these bits.
	 */
	if (ps8815_disable_rp_detect[port] && ps8815_disconnected[port] &&
	    pull == TYPEC_CC_RP) {
		CPRINTS("TCPC%d: disable chip based Rp detect on connection",
			port);
		tcpc_write(port, PS8XXX_REG_RP_DETECT_CONTROL,
			   RP_DETECT_DISABLE);
		ps8815_disconnected[port] = false;
	}

	rv = tcpci_tcpm_set_cc(port, pull);

	/*
	 * b/171430855 delay 1 ms after ROLE_CONTROL updates to prevent
	 * transmit buffer corruption
	 */
	ps8xxx_role_control_delay(port);

	return rv;
}

static int ps8xxx_tcpm_get_cc(int port, enum tcpc_cc_voltage_status *cc1,
			 enum tcpc_cc_voltage_status *cc2)
{
#ifdef CONFIG_USB_PD_TCPM_PS8751
	if (product_id[port] == PS8751_PRODUCT_ID)
		return ps8751_get_gcc(port, cc1, cc2);
#endif

	return tcpci_tcpm_get_cc(port, cc1, cc2);
}

const struct tcpm_drv ps8xxx_tcpm_drv = {
	.init			= ps8xxx_tcpm_init,
	.release		= ps8xxx_tcpm_release,
	.get_cc			= ps8xxx_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.check_vbus_level	= tcpci_tcpm_check_vbus_level,
#endif
	.select_rp_value	= tcpci_tcpm_select_rp_value,
	.set_cc			= ps8xxx_tcpm_set_cc,
	.set_polarity		= tcpci_tcpm_set_polarity,
#ifdef CONFIG_USB_PD_DECODE_SOP
	.sop_prime_enable	= tcpci_tcpm_sop_prime_enable,
#endif
	.set_vconn		= tcpci_tcpm_set_vconn,
	.set_msg_header		= tcpci_tcpm_set_msg_header,
	.set_rx_enable		= tcpci_tcpm_set_rx_enable,
	.get_message_raw	= tcpci_tcpm_get_message_raw,
	.transmit		= ps8xxx_tcpm_transmit,
	.tcpc_alert		= tcpci_tcpc_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus	= tcpci_tcpc_discharge_vbus,
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle		= ps8xxx_tcpc_drp_toggle,
#endif
#ifdef CONFIG_USB_PD_PPC
	.set_snk_ctrl		= tcpci_tcpm_set_snk_ctrl,
	.set_src_ctrl		= tcpci_tcpm_set_src_ctrl,
#endif
	.get_chip_info		= ps8xxx_get_chip_info,
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode	= ps8xxx_enter_low_power_mode,
#endif
	.set_bist_test_mode	= tcpci_set_bist_test_mode,
};

#ifdef CONFIG_CMD_I2C_STRESS_TEST_TCPC
struct i2c_stress_test_dev ps8xxx_i2c_stress_test_dev = {
	.reg_info = {
		.read_reg = PS8XXX_REG_VENDOR_ID_L,
		.read_val = PS8XXX_VENDOR_ID & 0xFF,
		.write_reg = MUX_IN_HPD_ASSERTION_REG,
	},
	.i2c_read = tcpc_i2c_read,
	.i2c_write = tcpc_i2c_write,
};
#endif /* CONFIG_CMD_I2C_STRESS_TEST_TCPC */

#ifdef CONFIG_USB_PD_TCPM_PS8751_CUSTOM_MUX_DRIVER

static int ps8xxx_mux_init(const struct usb_mux *me)
{
	RETURN_ERROR(tcpci_tcpm_mux_init(me));

	/* If this MUX is also the TCPC, then skip init */
	if (!(me->flags & USB_MUX_FLAG_NOT_TCPC))
		return EC_SUCCESS;

	product_id[me->usb_port] = board_get_ps8xxx_product_id(me->usb_port);

	return EC_SUCCESS;
}

/*
 * PS8751 goes to standby mode automatically when both CC lines are set to RP.
 * In standby mode it doesn't respond to first I2C access, but next
 * transactions are working fine (until it goes to sleep again).
 *
 * To wake device documentation recommends read content of 0xA0 register.
 */
void ps8xxx_wake_from_standby(const struct usb_mux *me)
{
	int reg;

	/* Since we are waking up device, this call will most likely fail */
	mux_read(me, PS8XXX_REG_I2C_DEBUGGING_ENABLE, &reg);
	msleep(10);
}

static int ps8xxx_mux_set(const struct usb_mux *me, mux_state_t mux_state,
			  bool *ack_required)
{
	if (product_id[me->usb_port] == PS8751_PRODUCT_ID &&
	    me->flags & USB_MUX_FLAG_NOT_TCPC) {
		ps8xxx_wake_from_standby(me);

		/*
		 * To operate properly, when working as mux only, PS8751 CC
		 * lines needs to be RD all the time. Changing to RP after
		 * setting mux breaks SuperSpeed connection.
		 */
		if (mux_state != USB_PD_MUX_NONE)
			RETURN_ERROR(mux_write(me, TCPC_REG_ROLE_CTRL,
					TCPC_REG_ROLE_CTRL_SET(TYPEC_NO_DRP,
							       TYPEC_RP_USB,
							       TYPEC_CC_RD,
							       TYPEC_CC_RD)));
	}

	return tcpci_tcpm_mux_set(me, mux_state, ack_required);
}

static int ps8xxx_mux_get(const struct usb_mux *me, mux_state_t *mux_state)
{
	if (product_id[me->usb_port] == PS8751_PRODUCT_ID &&
	    me->flags & USB_MUX_FLAG_NOT_TCPC)
		ps8xxx_wake_from_standby(me);

	return tcpci_tcpm_mux_get(me, mux_state);
}

static int ps8xxx_mux_enter_low_power(const struct usb_mux *me)
{
	/*
	 * Set PS8751 lines to RP. This allows device to standby
	 * automatically after ~2 seconds
	 */
	if (product_id[me->usb_port] == PS8751_PRODUCT_ID &&
	    me->flags & USB_MUX_FLAG_NOT_TCPC) {
		/*
		 * It may happen that this write will fail, but
		 * RP seems to be set correctly
		 */
		mux_write(me, TCPC_REG_ROLE_CTRL,
			  TCPC_REG_ROLE_CTRL_SET(TYPEC_NO_DRP, TYPEC_RP_USB,
						 TYPEC_CC_RP, TYPEC_CC_RP));
		return EC_SUCCESS;
	}

	return tcpci_tcpm_mux_enter_low_power(me);
}

const struct usb_mux_driver ps8xxx_usb_mux_driver = {
	.init = ps8xxx_mux_init,
	.set = ps8xxx_mux_set,
	.get = ps8xxx_mux_get,
	.enter_low_power_mode = ps8xxx_mux_enter_low_power,
};

#endif /* CONFIG_USB_PD_TCPM_PS8751_CUSTOM_MUX_DRIVER */
