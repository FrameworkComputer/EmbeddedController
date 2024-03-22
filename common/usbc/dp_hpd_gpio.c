/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Support for setting the Hot Plug Detect indication to the AP
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "timer.h"
#include "usb_pd.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#else
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

/* TODO(b/270409742): Remove this macro system for determining the GPIO */
#ifndef PORT_TO_HPD
#define PORT_TO_HPD(port) ((port) ? GPIO_USB_C1_DP_HPD : GPIO_USB_C0_DP_HPD)
#endif /* PORT_TO_HPD */

/*
 * Note: the following DP-related variables and functions must be kept
 * as-is since some boards are using them in their board-specific code.
 * TODO(b/267545470): Fold board DP code into the DP module
 */

#if defined(CONFIG_USB_PD_DP_HPD_GPIO) && \
	!defined(CONFIG_USB_PD_DP_HPD_GPIO_CUSTOM)
void svdm_set_hpd_gpio(int port, int en)
{
	gpio_set_level(PORT_TO_HPD(port), en);
}

int svdm_get_hpd_gpio(int port)
{
	return gpio_get_level(PORT_TO_HPD(port));
}
#endif

void svdm_set_hpd_gpio_irq(int port)
{
	svdm_set_hpd_gpio(port, 0);

	if (IS_ENABLED(CONFIG_USB_PD_DP_HPD_GPIO_IRQ_ACCURATE)) {
		udelay(HPD_DSTREAM_DEBOUNCE_IRQ);
	} else {
		usleep(HPD_DSTREAM_DEBOUNCE_IRQ);
	}

	svdm_set_hpd_gpio(port, 1);
}

enum ec_error_list dp_hpd_gpio_set(int port, bool level, bool irq)
{
	int cur_level = svdm_get_hpd_gpio(port);

	if (irq && !level) {
		/*
		 * IRQ can only be generated when the level is high, because
		 * the IRQ is signaled by a short low pulse from the high level.
		 */
		CPRINTF("ERR:HPD:IRQ&LOW\n");
		return EC_ERROR_INVAL;
	}

	if (irq && cur_level) {
		uint64_t now = get_time().val;
		/* wait for the minimum spacing between IRQ_HPD if needed */
		if (now < svdm_hpd_deadline[port])
			usleep(svdm_hpd_deadline[port] - now);

		svdm_set_hpd_gpio_irq(port);
	} else {
		svdm_set_hpd_gpio(port, level);
	}

	/* set the minimum time delay (2ms) for the next HPD IRQ */
	svdm_hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;

	return EC_SUCCESS;
}
