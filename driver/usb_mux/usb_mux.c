/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB mux high-level driver. */

#include "atomic.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "task.h"
#include "usb_mux.h"
#include "usbc_ppc.h"
#include "util.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

static int enable_debug_prints;

/*
 * Flags will reset to 0 after sysjump; This works for current flags as LPM will
 * get reset in the init method which is called during PD task startup.
 */
static uint32_t flags[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Device is in low power mode. */
#define USB_MUX_FLAG_IN_LPM		BIT(0)

/* The following bit is used to configure virtual mux in disconnect mode */
#define USB_MUX_FLAG_DISCONNECT_LATCH	BIT(1)

/* Device initialized at least once */
#define USB_MUX_FLAG_INIT		BIT(2)

enum mux_config_type {
	USB_MUX_INIT,
	USB_MUX_LOW_POWER,
	USB_MUX_SET_MODE,
	USB_MUX_GET_MODE,
	USB_MUX_CHIPSET_RESET,
};

/* Configure the MUX */
static int configure_mux(int port,
			 enum mux_config_type config,
			 mux_state_t *mux_state)
{
	int rv = EC_SUCCESS;
	const struct usb_mux *mux_ptr;

	if (config == USB_MUX_SET_MODE ||
	    config == USB_MUX_GET_MODE) {
		if (mux_state == NULL)
			return EC_ERROR_INVAL;

		if (config == USB_MUX_GET_MODE)
			*mux_state = USB_PD_MUX_NONE;
	}

	if ((config == USB_MUX_SET_MODE && *mux_state == USB_PD_MUX_NONE) ||
	      config == USB_MUX_INIT) {
		usb_mux_set_disconnect_latch_flag(port, true);
	}

	/*
	 * a MUX for a particular port can be a linked list chain of
	 * MUXes.  So when we change one, we traverse the whole list
	 * to make sure they are all updated appropriately.
	 */
	for (mux_ptr = &usb_muxes[port];
	     rv == EC_SUCCESS && mux_ptr != NULL;
	     mux_ptr = mux_ptr->next_mux) {
		mux_state_t lcl_state;
		const struct usb_mux_driver *drv = mux_ptr->driver;

		switch (config) {
		case USB_MUX_INIT:
			if (drv && drv->init) {
				rv = drv->init(mux_ptr);
				if (rv)
					break;
			}

			/* Apply board specific initialization */
			if (mux_ptr->board_init)
				rv = mux_ptr->board_init(mux_ptr);

			break;

		case USB_MUX_LOW_POWER:
			if (drv && drv->enter_low_power_mode)
				rv = drv->enter_low_power_mode(mux_ptr);

			break;

		case USB_MUX_CHIPSET_RESET:
			if (drv && drv->chipset_reset)
				rv = drv->chipset_reset(mux_ptr);

			break;

		case USB_MUX_SET_MODE:
			lcl_state = *mux_state;

			if (mux_ptr->flags & USB_MUX_FLAG_SET_WITHOUT_FLIP)
				lcl_state &= ~USB_PD_MUX_POLARITY_INVERTED;

			if (drv && drv->set) {
				rv = drv->set(mux_ptr, lcl_state);
				if (rv)
					break;
			}

			/* Apply board specific setting */
			if (mux_ptr->board_set)
				rv = mux_ptr->board_set(mux_ptr, lcl_state);

			break;

		case USB_MUX_GET_MODE:
			/*
			 * This is doing a GET_CC on all of the MUXes in the
			 * chain and ORing them together. This will make sure
			 * if one of the MUX values has FLIP turned off that
			 * we will end up with the correct value in the end.
			 */
			if (drv && drv->get) {
				rv = drv->get(mux_ptr, &lcl_state);
				if (rv)
					break;
				*mux_state |= lcl_state;
			}
			break;
		}
	}

	if (rv)
		CPRINTS("mux config:%d, port:%d, rv:%d",
			config, port, rv);

	return rv;
}

static void enter_low_power_mode(int port)
{
	/*
	 * Set LPM flag regardless of method presence or method failure. We
	 * want know know that we tried to put the device in low power mode
	 * so we can re-initialize the device on the next access.
	 */
	atomic_or(&flags[port], USB_MUX_FLAG_IN_LPM);

	/* Apply any low power customization if present */
	configure_mux(port, USB_MUX_LOW_POWER, NULL);
}

static inline void exit_low_power_mode(int port)
{
	/* If we are in low power, initialize device (which clears LPM flag) */
	if (flags[port] & USB_MUX_FLAG_IN_LPM)
		usb_mux_init(port);
}

void usb_mux_init(int port)
{
	int rv;

	ASSERT(port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);

	if (port >= board_get_usb_pd_port_count()) {
		return;
	}

	rv = configure_mux(port, USB_MUX_INIT, NULL);

	if (rv == EC_SUCCESS)
		atomic_or(&flags[port], USB_MUX_FLAG_INIT);

	/*
	 * Mux may fail initialization if it's not powered. Mark this port
	 * as in LPM mode to try initialization again.
	 */
	if (rv == EC_ERROR_NOT_POWERED)
		atomic_or(&flags[port], USB_MUX_FLAG_IN_LPM);
	else
		atomic_clear_bits(&flags[port], USB_MUX_FLAG_IN_LPM);
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

	if (port >= board_get_usb_pd_port_count()) {
		return;
	}

	/* Perform initialization if not initialized yet */
	if (!(flags[port] & USB_MUX_FLAG_INIT))
		usb_mux_init(port);

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
	int rv;

	if (port >= board_get_usb_pd_port_count()) {
		return USB_PD_MUX_NONE;
	}

	/* Perform initialization if not initialized yet */
	if (!(flags[port] & USB_MUX_FLAG_INIT))
		usb_mux_init(port);

	if (flags[port] & USB_MUX_FLAG_IN_LPM)
		return USB_PD_MUX_NONE;

	rv = configure_mux(port, USB_MUX_GET_MODE, &mux_state);

	return rv ? USB_PD_MUX_NONE : mux_state;
}

/* Get USB MUX (virtual MUX) disconnect flag */
bool usb_mux_get_disconnect_latch_flag(int port)
{
	bool rv = false;

	if (port >= board_get_usb_pd_port_count())
		return rv;

	if (!IS_ENABLED(CONFIG_USB_MUX_VIRTUAL))
		return rv;

	return !!(flags[port] & USB_MUX_FLAG_DISCONNECT_LATCH);
}

/* Set USB MUX (virtual MUX) disconnect flag */
void usb_mux_set_disconnect_latch_flag(int port, bool enable)
{
	if (port >= board_get_usb_pd_port_count())
		return;

	if (!IS_ENABLED(CONFIG_USB_MUX_VIRTUAL))
		return;

	if (enable)
		atomic_or(&flags[port], USB_MUX_FLAG_DISCONNECT_LATCH);
	else
		atomic_clear_bits(&flags[port], USB_MUX_FLAG_DISCONNECT_LATCH);
}

void usb_mux_flip(int port)
{
	mux_state_t mux_state;

	if (port >= board_get_usb_pd_port_count()) {
		return;
	}

	/* Perform initialization if not initialized yet */
	if (!(flags[port] & USB_MUX_FLAG_INIT))
		usb_mux_init(port);

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
	mux_state_t mux_state;
	const struct usb_mux *mux_ptr = &usb_muxes[port];

	if (port >= board_get_usb_pd_port_count()) {
		return;
	}

	/* Perform initialization if not initialized yet */
	if (!(flags[port] & USB_MUX_FLAG_INIT))
		usb_mux_init(port);

	for (; mux_ptr; mux_ptr = mux_ptr->next_mux)
		if (mux_ptr->hpd_update)
			mux_ptr->hpd_update(mux_ptr, hpd_lvl, hpd_irq);

	if (!configure_mux(port, USB_MUX_GET_MODE, &mux_state)) {
		mux_state |= (hpd_lvl ? USB_PD_MUX_HPD_LVL : 0) |
			     (hpd_irq ? USB_PD_MUX_HPD_IRQ : 0);
		configure_mux(port, USB_MUX_SET_MODE, &mux_state);
	}
}

static void mux_chipset_reset(void)
{
	int port;

	for (port = 0; port < board_get_usb_pd_port_count(); ++port)
		configure_mux(port, USB_MUX_CHIPSET_RESET, NULL);
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, mux_chipset_reset, HOOK_PRIO_DEFAULT);

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
			"HPD_LVL=%d SAFE=%d TBT=%d USB4=%d\n", port,
			!!(mux_state & USB_PD_MUX_USB_ENABLED),
			!!(mux_state & USB_PD_MUX_DP_ENABLED),
			mux_state & USB_PD_MUX_POLARITY_INVERTED ?
				"INVERTED" : "NORMAL",
			!!(mux_state & USB_PD_MUX_HPD_IRQ),
			!!(mux_state & USB_PD_MUX_HPD_LVL),
			!!(mux_state & USB_PD_MUX_SAFE_MODE),
			!!(mux_state & USB_PD_MUX_TBT_COMPAT_ENABLED),
			!!(mux_state & USB_PD_MUX_USB4_ENABLED));

		return EC_SUCCESS;
	}

	for (i = 0; i < ARRAY_SIZE(mux_name); i++)
		if (!strcasecmp(argv[2], mux_name[i]))
			mux = i;
	usb_mux_set(port, mux, mux == USB_PD_MUX_NONE ?
				      USB_SWITCH_DISCONNECT :
				      USB_SWITCH_CONNECT,
			  polarity_rm_dts(pd_get_polarity(port)));
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
	mux_state_t mux_state;

	if (port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	if (configure_mux(port, USB_MUX_GET_MODE, &mux_state))
		return EC_RES_ERROR;

	r->flags = mux_state;

	/*
	 * Force disconnect mode if disconnect latch flag is set.
	 * Send host event for configuring the latest mux state
	 */
	if (IS_ENABLED(CONFIG_USB_MUX_VIRTUAL) &&
	    usb_mux_get_disconnect_latch_flag(port)) {
		r->flags = USB_PD_MUX_NONE;
		usb_mux_set_disconnect_latch_flag(port, false);
		args->response_size = sizeof(*r);
		host_set_single_event(EC_HOST_EVENT_USB_MUX);
		return EC_RES_SUCCESS;
	}

	/* Clear HPD IRQ event since we're about to inform host of it. */
	if (IS_ENABLED(CONFIG_USB_MUX_VIRTUAL) &&
	    (r->flags & USB_PD_MUX_HPD_IRQ)) {
		usb_mux_hpd_update(port, r->flags & USB_PD_MUX_HPD_LVL, 0);
	}

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_MUX_INFO,
		     hc_usb_pd_mux_info,
		     EC_VER_MASK(0));

static enum ec_status hc_usb_pd_mux_ack(struct host_cmd_handler_args *args)
{
	__maybe_unused const struct ec_params_usb_pd_mux_ack *p = args->params;

	if (!IS_ENABLED(CONFIG_USB_MUX_AP_ACK_REQUEST))
		return EC_RES_INVALID_COMMAND;

	task_set_event(PD_PORT_TO_TASK_ID(p->port), PD_EVENT_AP_MUX_DONE);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_MUX_ACK,
		     hc_usb_pd_mux_ack,
		     EC_VER_MASK(0));
