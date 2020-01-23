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

enum mux_config_type {
	USB_MUX_INIT,
	USB_MUX_LOW_POWER,
	USB_MUX_SET_MODE,
	USB_MUX_GET_MODE,
};

/* Configure the retimer */
static int configure_retimer(int port, enum mux_config_type config,
				mux_state_t mux_state)
{
	int res = 0;

	if (IS_ENABLED(CONFIG_USBC_MUX_RETIMER)) {
		const struct usb_retimer *retimer = &usb_retimers[port];

		if (!retimer->driver)
			return 0;

		switch (config) {
		case USB_MUX_INIT:
			if (retimer->driver->init)
				res = retimer->driver->init(port);
			break;
		case USB_MUX_LOW_POWER:
			if (retimer->driver->enter_low_power_mode)
				res = retimer->driver->enter_low_power_mode(
									port);
			break;
		case USB_MUX_SET_MODE:
			if (retimer->driver->set)
				res = retimer->driver->set(port, mux_state);
			break;
		default:
			break;
		}
	}

	return res;
}

/* Configure the MUX */
static int configure_mux(int port, enum mux_config_type config,
				mux_state_t *mux_state)
{
	const struct usb_mux *mux = &usb_muxes[port];
	int res;

	switch (config) {
	case USB_MUX_INIT:
		res = mux->driver->init(port);
		if (res)
			break;

		res = configure_retimer(port, config, USB_PD_MUX_NONE);
		if (res)
			break;

		/* Apply board specific initialization */
		if (mux->board_init)
			res = mux->board_init(port);

		break;
	case USB_MUX_LOW_POWER:
		if (mux->driver->enter_low_power_mode) {
			res = mux->driver->enter_low_power_mode(port);
			if (res)
				break;
		}
		res = configure_retimer(port, config, USB_PD_MUX_NONE);
		break;
	case USB_MUX_SET_MODE:
		res = mux->driver->set(port, *mux_state);
		if (res)
			break;
		res = configure_retimer(port, config, *mux_state);
		break;
	case USB_MUX_GET_MODE:
		res = mux->driver->get(port, mux_state);
		break;
	}

	if (res)
		CPRINTS("mux config:%d, port:%d, res:%d", config, port, res);

	return res;
}

static void enter_low_power_mode(int port)
{
	mux_state_t mux_state = USB_PD_MUX_NONE;

	/*
	 * Set LPM flag regardless of method presence or method failure. We want
	 * know know that we tried to put the device in low power mode so we can
	 * re-initialize the device on the next access.
	 */
	flags[port] |= USB_MUX_FLAG_IN_LPM;

	/* Apply any low power customization if present */
	configure_mux(port, USB_MUX_LOW_POWER, &mux_state);
}

static inline void exit_low_power_mode(int port)
{
	/* If we are in low power, initialize device (which clears LPM flag) */
	if (flags[port] & USB_MUX_FLAG_IN_LPM)
		usb_mux_init(port);
}

void usb_mux_init(int port)
{
	mux_state_t mux_state = USB_PD_MUX_NONE;

	ASSERT(port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);

	configure_mux(port, USB_MUX_INIT, &mux_state);

	/* Device is always out of LPM after initialization. */
	flags[port] &= ~USB_MUX_FLAG_IN_LPM;
}

/*
 * TODO(crbug.com/505480): Setting muxes often involves I2C transcations,
 * which can block. Consider implementing an asynchronous task.
 */
void usb_mux_set(int port, mux_state_t mux_mode,
		 enum usb_switch usb_mode, int polarity)
{
	mux_state_t mux_state;
	const int should_enter_low_power_mode =
		(mux_mode == USB_PD_MUX_NONE &&
		usb_mode == USB_SWITCH_DISCONNECT);

	/* Configure USB2.0 */
	if (IS_ENABLED(CONFIG_USB_CHARGER))
		usb_charger_set_switches(port, usb_mode);

	/*
	 * Don't wake device up just to put it back to sleep. Low power mode
	 * flag is only set if the mux set() operation succeeded previously for
	 * the same disconnected state.
	 */
	if (should_enter_low_power_mode && (flags[port] & USB_MUX_FLAG_IN_LPM))
		return;

	exit_low_power_mode(port);

	/* Configure superspeed lanes */
	mux_state = ((mux_mode != USB_PD_MUX_NONE) && polarity)
			? mux_mode | USB_PD_MUX_POLARITY_INVERTED
			: mux_mode;

	if (configure_mux(port, USB_MUX_SET_MODE, &mux_state))
		return;

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

mux_state_t usb_mux_get(int port)
{
	mux_state_t mux_state;

	exit_low_power_mode(port);

	if (configure_mux(port, USB_MUX_GET_MODE, &mux_state))
		return USB_PD_MUX_NONE;

	return mux_state;
}

void usb_mux_flip(int port)
{
	mux_state_t mux_state;

	exit_low_power_mode(port);

	if (configure_mux(port, USB_MUX_GET_MODE, &mux_state))
		return;

	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		mux_state &= ~USB_PD_MUX_POLARITY_INVERTED;
	else
		mux_state |= USB_PD_MUX_POLARITY_INVERTED;

	configure_mux(port, USB_MUX_SET_MODE, &mux_state);
}

void usb_mux_hpd_update(int port, int hpd_lvl, int hpd_irq)
{
	const struct usb_mux *mux = &usb_muxes[port];
	mux_state_t mux_state;

	if (mux->hpd_update)
		mux->hpd_update(port, hpd_lvl, hpd_irq);

	if (!configure_mux(port, USB_MUX_GET_MODE, &mux_state)) {
		mux_state |= (hpd_lvl ? USB_PD_MUX_HPD_LVL : 0) |
			(hpd_irq ? USB_PD_MUX_HPD_IRQ : 0);
		configure_retimer(port, USB_MUX_SET_MODE, mux_state);
	}
}

#ifdef CONFIG_CMD_TYPEC
static int command_typec(int argc, char **argv)
{
	const char * const mux_name[] = {"none", "usb", "dp", "dock"};
	char *e;
	int port;
	mux_state_t mux = USB_PD_MUX_NONE;
	int i;

	if (argc == 2 && !strcasecmp(argv[1], "debug")) {
		enable_debug_prints = 1;
		return EC_SUCCESS;
	}

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &e, 10);
	if (*e || port >= board_get_usb_pd_port_count())
		return EC_ERROR_PARAM1;

	if (argc < 3) {
		mux_state_t mux_state;

		mux_state = usb_mux_get(port);
		ccprintf("Port %d: USB=%d DP=%d POLARITY=%s HPD_IRQ=%d "
			"HPD_LVL=%d SAFE=%d TBT=%d\n", port,
			!!(mux_state & USB_PD_MUX_USB_ENABLED),
			!!(mux_state & USB_PD_MUX_DP_ENABLED),
			mux_state & USB_PD_MUX_POLARITY_INVERTED ?
				"INVERTED" : "NORMAL",
			!!(mux_state & USB_PD_MUX_HPD_IRQ),
			!!(mux_state & USB_PD_MUX_HPD_LVL),
			!!(mux_state & USB_PD_MUX_SAFE_MODE),
			!!(mux_state & USB_PD_MUX_TBT_COMPAT_ENABLED));

		return EC_SUCCESS;
	}

	for (i = 0; i < ARRAY_SIZE(mux_name); i++)
		if (!strcasecmp(argv[2], mux_name[i]))
			mux = i;
	usb_mux_set(port, mux, mux == USB_PD_MUX_NONE ?
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
	const struct usb_mux *mux = &usb_muxes[port];
	mux_state_t mux_state;

	if (port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	if (configure_mux(port, USB_MUX_GET_MODE, &mux_state))
		return EC_RES_ERROR;

	r->flags = mux_state;

	/* Clear HPD IRQ event since we're about to inform host of it. */
	if (IS_ENABLED(CONFIG_USB_MUX_VIRTUAL) &&
	    (r->flags & USB_PD_MUX_HPD_IRQ) &&
	    (mux->hpd_update == &virtual_hpd_update)) {
		usb_mux_hpd_update(port, r->flags & USB_PD_MUX_HPD_LVL, 0);
	}

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_MUX_INFO,
		     hc_usb_pd_mux_info,
		     EC_VER_MASK(0));
