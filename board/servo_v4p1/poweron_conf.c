/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ccd_measure_sbu.h"
#include "common.h"
#include "gl3590.h"
#include "ioexpanders.h"
#include "pathsel.h"
#include "poweron_conf.h"
#include "system.h"
#include "usb_pd_config.h"
#include "util.h"

/*
 * Ensure this struct is the same size as buffer -
 * uint8_t poweron_conf[CONFIG_POWERON_CONF_LEN]
 */
struct servo_poweron_conf {
	/* A0 */
	uint8_t top_usb;
	/* A1 */
	uint8_t bottom_usb;
	uint8_t uservo_usb;
	uint8_t cc_config;
};

const static struct servo_poweron_conf default_poweron_conf = {
	.top_usb = USB_PORT_MUX_TO_DUT | USB_PORT_POWER_EN | USB_PORT_MUX_EN,
	.bottom_usb = USB_PORT_MUX_TO_DUT | USB_PORT_POWER_EN | USB_PORT_MUX_EN,
	.uservo_usb = USB_PORT_POWER_EN | USB_PORT_MUX_EN,
	/* DTS ON by default */
	.cc_config = 0,
};

static void servo_print_usb_poweron_conf(const char *port, uint8_t bitmask)
{
	ccprintf("On port %s - mux_sel:", port);
	if (!strcasecmp(port, "uservo_usb"))
		ccprintf((bitmask & USB_PORT_MUX_TO_DUT) ? "fastboot" :
							   "uservo");
	else
		ccprintf((bitmask & USB_PORT_MUX_TO_DUT) ? "dut_sees_usbkey" :
							   "servo_sees_usbkey");

	ccprintf(", mux_en:%s, pwr_en:%s\n",
		 (bitmask & USB_PORT_MUX_EN) ? "on" : "off",
		 (bitmask & USB_PORT_POWER_EN) ? "on" : "off");
}

static void servo_print_cc_poweron_conf(uint8_t bitmask)
{
	ccprintf("CC settings - dts:%s\n",
		 (bitmask & CC_DISABLE_DTS) ? "off" : "on");
}

static int
servo_write_poweron_conf(const struct servo_poweron_conf *servo_poweron_conf)
{
	uint8_t buf[CONFIG_POWERON_CONF_LEN];
	int rv;

	if (!servo_poweron_conf)
		return EC_ERROR_INVAL;

	if (sizeof(*servo_poweron_conf) > CONFIG_POWERON_CONF_LEN)
		return EC_ERROR_INVAL;

	/* Save this new config to flash. */
	rv = board_write_poweron_conf((uint8_t *)servo_poweron_conf);
	if (rv)
		return rv;

	/* Check if we can read saved config. */
	if (board_read_poweron_conf(buf))
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

static int
servo_read_poweron_conf(struct servo_poweron_conf *servo_poweron_conf)
{
	int rv;

	if (sizeof(*servo_poweron_conf) != CONFIG_POWERON_CONF_LEN)
		return EC_ERROR_UNKNOWN;

	rv = board_read_poweron_conf((uint8_t *)servo_poweron_conf);
	if (rv) {
		/*
		 * Write default values if there is no stored,
		 * or error during read.
		 */
		ccprintf("Poweron config read failed or config never set.\n"
			 "Write default values to poweron config.\n");
		rv = servo_write_poweron_conf(&default_poweron_conf);
		if (rv)
			return rv;
		*servo_poweron_conf = default_poweron_conf;
	}

	return EC_SUCCESS;
}

/*
 * poweron_conf top_usb [mux_sel (servo_sees_usbkey|dut_sees_usbkey) |
 * mux_en (on|off) | pwr_en (on|off)]
 *
 * poweron_conf bottom_usb [mux_sel (servo_sees_usbkey|dut_sees_usbkey) |
 * mux_en (on|off) | pwr_en (on|off)]
 *
 * poweron_conf uservo_usb [mux_sel (uservo|fastboot) |
 * mux_en (on|off) | pwr_en (on|off)]
 */
static int servo_subcommand_usb_poweron_conf(int argc, const char *argv[])
{
	int ret;
	struct servo_poweron_conf current_config;
	uint8_t *port_bitmap;

	ret = servo_read_poweron_conf(&current_config);
	if (ret)
		return ret;

	/*
	 * If none of these keywords matches its wrong param.
	 * If any matches this param would be port we are modifying.
	 */
	if (!strcasecmp(argv[1], "top_usb"))
		port_bitmap = &current_config.top_usb;
	else if (!strcasecmp(argv[1], "bottom_usb"))
		port_bitmap = &current_config.bottom_usb;
	else if (!strcasecmp(argv[1], "uservo_usb"))
		port_bitmap = &current_config.uservo_usb;
	else
		return EC_ERROR_PARAM2;

	if (argc == 2) {
		servo_print_usb_poweron_conf(argv[1], *port_bitmap);
		return EC_SUCCESS;
	}

	if (argc > 2 && !(argc % 2)) {
		for (int i = 2; i < argc; i += 2) {
			if (!strcasecmp(argv[i], "mux_sel")) {
				if (!strcasecmp(argv[i + 1],
						"servo_sees_usbkey") ||
				    !strcasecmp(argv[i + 1], "uservo"))
					*port_bitmap &= ~USB_PORT_MUX_TO_DUT;
				else if (!strcasecmp(argv[i + 1],
						     "dut_sees_usbkey") ||
					 !strcasecmp(argv[i + 1], "fastboot"))
					*port_bitmap |= USB_PORT_MUX_TO_DUT;
				else
					return (EC_ERROR_PARAM1 + i + 1);
			} else if (!strcasecmp(argv[i], "mux_en")) {
				if (!strcasecmp(argv[i + 1], "on"))
					*port_bitmap |= USB_PORT_MUX_EN;
				else if (!strcasecmp(argv[i + 1], "off"))
					*port_bitmap &= ~USB_PORT_MUX_EN;
				else
					return (EC_ERROR_PARAM1 + i + 1);
			} else if (!strcasecmp(argv[i], "pwr_en")) {
				if (!strcasecmp(argv[i + 1], "on"))
					*port_bitmap |= USB_PORT_POWER_EN;
				else if (!strcasecmp(argv[i + 1], "off"))
					*port_bitmap &= ~USB_PORT_POWER_EN;
				else
					return (EC_ERROR_PARAM1 + i + 1);
			} else {
				return (EC_ERROR_PARAM1 + i);
			}
		}

		ret = servo_write_poweron_conf(&current_config);
		if (ret)
			return EC_ERROR_UNKNOWN;
		ccprintf("Changes saved. Reboot to apply new config.\n");

	} else {
		return EC_ERROR_PARAM_COUNT;
	}

	return EC_SUCCESS;
}

/*
 * poweron_conf cc [dts (on|off)]
 * Further options to be implemented in future, if needed.
 */
static int servo_subcommand_cc_poweron_conf(int argc, const char *argv[])
{
	int ret;
	struct servo_poweron_conf current_config;

	ret = servo_read_poweron_conf(&current_config);
	if (ret)
		return ret;

	if (argc == 2) {
		servo_print_cc_poweron_conf(current_config.cc_config);
		return EC_SUCCESS;
	} else if (argc == 4) {
		if (!strcasecmp(argv[2], "dts")) {
			if (!strcasecmp(argv[3], "on"))
				current_config.cc_config &= ~CC_DISABLE_DTS;
			else if (!strcasecmp(argv[3], "off"))
				current_config.cc_config |= CC_DISABLE_DTS;
			else
				return EC_ERROR_PARAM4;
		} else {
			return EC_ERROR_PARAM3;
		}

		ret = servo_write_poweron_conf(&current_config);
		if (ret)
			return EC_ERROR_UNKNOWN;
		ccprintf("Changes saved. Reboot to apply new config.\n");
	} else {
		return EC_ERROR_PARAM_COUNT;
	}

	return EC_SUCCESS;
}

static int servo_subcommand_default_poweron_conf(void)
{
	int ret;

	ret = servo_write_poweron_conf(&default_poweron_conf);
	if (ret)
		return ret;
	ccprintf("Poweron config brought to default. "
		 "Reboot to apply new config.\n");
	return EC_SUCCESS;
}

/*
 * This function does not apply this config.
 * It only writes to nonvolatile memory, this memory is read and applied
 * during poweron init (eg. after reboot) .
 */

static int command_poweron_conf(int argc, const char *argv[])
{
	int ret;
	struct servo_poweron_conf current_config;

	ret = servo_read_poweron_conf(&current_config);
	if (ret)
		return EC_ERROR_UNKNOWN;

	if (argc == 1) {
		servo_print_usb_poweron_conf("top_usb", current_config.top_usb);
		servo_print_usb_poweron_conf("bottom_usb",
					     current_config.bottom_usb);
		servo_print_usb_poweron_conf("uservo_usb",
					     current_config.uservo_usb);
		servo_print_cc_poweron_conf(current_config.cc_config);
	} else if (argc <= 8) {
		if (!strcasecmp(argv[1], "top_usb") ||
		    !strcasecmp(argv[1], "bottom_usb") ||
		    !strcasecmp(argv[1], "uservo_usb"))
			ret = servo_subcommand_usb_poweron_conf(argc, argv);
		else if (!strcasecmp(argv[1], "cc"))
			ret = servo_subcommand_cc_poweron_conf(argc, argv);
		else if (!strcasecmp(argv[1], "default"))
			ret = servo_subcommand_default_poweron_conf();
		else
			return EC_ERROR_PARAM3;
	} else {
		return EC_ERROR_PARAM_COUNT;
	}

	if (ret)
		return ret;
	else
		return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(poweron_conf, command_poweron_conf, "",
			"Read and write servo poweron config.");

/* Read config and set usb ports and cc as expected in poweron config */
void apply_poweron_conf(void)
{
	struct servo_poweron_conf servo_poweron_conf;

	servo_read_poweron_conf(&servo_poweron_conf);

	/* Init top USB port */
	/* Set mux direction */
	if (servo_poweron_conf.top_usb & USB_PORT_MUX_TO_DUT)
		usb3_a0_to_dut();
	else
		usb3_a0_to_host();

	/* Connect/disconnect data lines */
	if (servo_poweron_conf.top_usb & USB_PORT_MUX_EN)
		usb3_a0_mux_en_l(0);
	else
		usb3_a0_mux_en_l(1);

	/* Enable/disable port power*/
	if (servo_poweron_conf.top_usb & USB_PORT_POWER_EN) {
		ec_usb3_a0_pwr_en(1);
		hh_usb3_a0_pwr_en(1);
	} else {
		ec_usb3_a0_pwr_en(0);
		hh_usb3_a0_pwr_en(0);
	}

	/* Init top USB port */
	if (servo_poweron_conf.bottom_usb & USB_PORT_MUX_TO_DUT)
		usb3_a1_to_dut();
	else
		usb3_a1_to_host();

	if (servo_poweron_conf.bottom_usb & USB_PORT_MUX_EN)
		gpio_set_level(GPIO_USB3_A1_MUX_EN_L, 0);
	else
		gpio_set_level(GPIO_USB3_A1_MUX_EN_L, 1);

	if (servo_poweron_conf.bottom_usb & USB_PORT_POWER_EN) {
		ec_usb3_a1_pwr_en(1);
		hh_usb3_a1_pwr_en(1);
	} else {
		ec_usb3_a1_pwr_en(0);
		hh_usb3_a1_pwr_en(0);
	}

	/* Init uservo USB port. */
	if (servo_poweron_conf.uservo_usb & USB_PORT_MUX_TO_DUT)
		dut_to_host();
	else
		uservo_to_host();

	if (servo_poweron_conf.uservo_usb & USB_PORT_MUX_EN)
		gpio_set_level(GPIO_FASTBOOT_DUTHUB_MUX_EN_L, 0);
	else
		gpio_set_level(GPIO_FASTBOOT_DUTHUB_MUX_EN_L, 1);

	if (servo_poweron_conf.uservo_usb & USB_PORT_POWER_EN) {
		ec_uservo_power_en(1);
		gl3590_enable_ports(0, GL3590_DFP4, 1);

	} else {
		ec_uservo_power_en(0);
		gl3590_enable_ports(0, GL3590_DFP4, 0);
	}

	/* Init CCD config */
	if (servo_poweron_conf.cc_config & CC_DISABLE_DTS) {
		set_cc_flag(CC_DISABLE_DTS, true);
	} else {
		/* Start SuzyQ detection */
		start_ccd_meas_sbu_cycle();
		set_cc_flag(CC_DISABLE_DTS, false);
	}
}
