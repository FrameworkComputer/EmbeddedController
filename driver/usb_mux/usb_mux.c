/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB mux high-level driver. */

#include "common.h"
#include "console.h"
#include "host_command.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static int enable_debug_prints;

/*
 * Flags will reset to 0 after sysjump; This works for current flags as LPM will
 * get reset in the init method which is called during PD task startup.
 */
static uint8_t flags[CONFIG_USB_PD_PORT_MAX_COUNT];

#define USB_MUX_FLAG_IN_LPM BIT(0) /* Device is in low power mode. */


static void enter_low_power_mode(int port)
{
	const struct usb_mux *mux = &usb_muxes[port];
	int res;

	/*
	 * Set LPM flag regardless of method presence or method failure. We want
	 * know know that we tried to put the device in low power mode so we can
	 * re-initialize the device on the next access.
	 */
	flags[port] |= USB_MUX_FLAG_IN_LPM;

	/* Apply any low power customization if present */
	if (mux->driver->enter_low_power_mode) {
		res = mux->driver->enter_low_power_mode(port);

		if (res)
			CPRINTS("Err: enter_low_power_mode mux port(%d): %d",
				port, res);
	}
}

static inline void exit_low_power_mode(int port)
{
	/* If we are in low power, initialize device (which clears LPM flag) */
	if (flags[port] & USB_MUX_FLAG_IN_LPM)
		usb_mux_init(port);
}

void usb_mux_init(int port)
{
	const struct usb_mux *mux = &usb_muxes[port];
	int res;

	ASSERT(port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);

	res = mux->driver->init(port);
	if (res) {
		CPRINTS("Err: init mux port(%d): %d", port, res);
		return;
	}

	/* Device is always out of LPM after initialization. */
	flags[port] &= ~USB_MUX_FLAG_IN_LPM;

	/* Apply board specific initialization */
	if (mux->board_init) {
		res = mux->board_init(port);

		if (res)
			CPRINTS("Err: board_init mux port(%d): %d", port, res);
	}
}

/*
 * TODO(crbug.com/505480): Setting muxes often involves I2C transcations,
 * which can block. Consider implementing an asynchronous task.
 */
void usb_mux_set(int port, enum typec_mux mux_mode,
		 enum usb_switch usb_mode, int polarity)
{
	const struct usb_mux *mux = &usb_muxes[port];
	int res;
	mux_state_t mux_state;
	const int should_enter_low_power_mode =
		mux_mode == TYPEC_MUX_NONE && usb_mode == USB_SWITCH_DISCONNECT;

#ifdef CONFIG_USB_CHARGER
	/* Configure USB2.0 */
	usb_charger_set_switches(port, usb_mode);
#endif

	/*
	 * Don't wake device up just to put it back to sleep. Low power mode
	 * flag is only set if the mux set() operation succeeded previously for
	 * the same disconnected state.
	 */
	if (should_enter_low_power_mode && (flags[port] & USB_MUX_FLAG_IN_LPM))
		return;

	exit_low_power_mode(port);

	/* Configure superspeed lanes */
	mux_state = polarity ? mux_mode | MUX_POLARITY_INVERTED : mux_mode;
	res = mux->driver->set(port, mux_state);
	if (res) {
		CPRINTS("Err: set mux port(%d): %d", port, res);
		return;
	}

	if (enable_debug_prints)
		CPRINTS(
		     "usb/dp mux: port(%d) typec_mux(%d) usb2(%d) polarity(%d)",
		     port, mux_mode, usb_mode, polarity);

	/*
	 * If we are completely disconnecting the mux, then we should put it in
	 * its lowest power state.
	 */
	if (should_enter_low_power_mode)
		enter_low_power_mode(port);
}

int usb_mux_get(int port, const char **dp_str, const char **usb_str)
{
	const struct usb_mux *mux = &usb_muxes[port];
	int res;
	mux_state_t mux_state;
	const char *dp, *usb;

	exit_low_power_mode(port);

	res = mux->driver->get(port, &mux_state);
	if (res) {
		CPRINTS("Err: get mux port(%d): %d", port, res);
		return 0;
	}

	dp = mux_state & MUX_POLARITY_INVERTED ? "DP2" : "DP1";
	usb = mux_state & MUX_POLARITY_INVERTED ? "USB2" : "USB1";

	*dp_str = mux_state & MUX_DP_ENABLED ? dp : NULL;
	*usb_str = mux_state & MUX_USB_ENABLED ? usb : NULL;

	return *dp_str || *usb_str;
}

void usb_mux_flip(int port)
{
	const struct usb_mux *mux = &usb_muxes[port];
	int res;
	mux_state_t mux_state;

	exit_low_power_mode(port);

	res = mux->driver->get(port, &mux_state);
	if (res) {
		CPRINTS("Err: get mux port(%d): %d", port, res);
		return;
	}

	if (mux_state & MUX_POLARITY_INVERTED)
		mux_state &= ~MUX_POLARITY_INVERTED;
	else
		mux_state |= MUX_POLARITY_INVERTED;

	res = mux->driver->set(port, mux_state);
	if (res)
		CPRINTS("Err: set mux port(%d): %d", port, res);
}

#ifdef CONFIG_CMD_TYPEC
static int command_typec(int argc, char **argv)
{
	const char * const mux_name[] = {"none", "usb", "dp", "dock"};
	char *e;
	int port;
	enum typec_mux mux = TYPEC_MUX_NONE;
	int i;

	if (argc == 2 && !strcasecmp(argv[1], "debug")) {
		enable_debug_prints = 1;
		return EC_SUCCESS;
	}

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &e, 10);
	if (*e || port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return EC_ERROR_PARAM1;

	if (argc < 3) {
		const char *dp_str, *usb_str;
		ccprintf("Port C%d: polarity:CC%d\n",
			port, pd_get_polarity(port) + 1);
		if (usb_mux_get(port, &dp_str, &usb_str))
			ccprintf("Superspeed %s%s%s\n",
				 dp_str ? dp_str : "",
				 dp_str && usb_str ? "+" : "",
				 usb_str ? usb_str : "");
		else
			ccprintf("No Superspeed connection\n");

		return EC_SUCCESS;
	}

	for (i = 0; i < ARRAY_SIZE(mux_name); i++)
		if (!strcasecmp(argv[2], mux_name[i]))
			mux = i;
	usb_mux_set(port, mux, mux == TYPEC_MUX_NONE ?
				      USB_SWITCH_DISCONNECT :
				      USB_SWITCH_CONNECT,
			  pd_get_polarity(port));
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(typec, command_typec,
			"[port|debug] [none|usb|dp|dock]",
			"Control type-C connector muxing");
#endif

static enum ec_status hc_usb_pd_mux_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_mux_info *p = args->params;
	struct ec_response_usb_pd_mux_info *r = args->response;
	int port = p->port;
	const struct usb_mux *mux;

	if (port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return EC_RES_INVALID_PARAM;

	mux = &usb_muxes[port];
	if (mux->driver->get(port, &r->flags) != EC_SUCCESS)
		return EC_RES_ERROR;

#ifdef CONFIG_USB_MUX_VIRTUAL
	/* Clear HPD IRQ event since we're about to inform host of it. */
	if ((r->flags & USB_PD_MUX_HPD_IRQ) &&
	    mux->hpd_update == &virtual_hpd_update)
		mux->hpd_update(port, r->flags & USB_PD_MUX_HPD_LVL, 0);
#endif

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_MUX_INFO,
		     hc_usb_pd_mux_info,
		     EC_VER_MASK(0));
