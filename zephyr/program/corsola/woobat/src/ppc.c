/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Woobat PPC/BC12 (mixed RT1739 or PI3USB9201+SYV682X) configuration */

#include "baseboard_usbc_config.h"
#include "cros_board_info.h"
#include "driver/usb_mux/ps8743_public.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "usb_mux.h"
#include "usbc/ppc.h"

#include <zephyr/logging/log.h>

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

LOG_MODULE_REGISTER(alt_dev_replacement);

#define BOARD_VERSION_UNKNOWN 0xffffffff

/* Check board version to decide which ppc/bc12 is used. */
static bool board_has_syv_ppc(void)
{
	static uint32_t board_version = BOARD_VERSION_UNKNOWN;

	if (board_version == BOARD_VERSION_UNKNOWN || IS_ENABLED(CONFIG_TEST)) {
		if (cbi_get_board_version(&board_version) != EC_SUCCESS) {
			LOG_ERR("Failed to get board version.");
			board_version = 0;
		}
	}

	return (board_version >= 3);
}

static void check_alternate_devices(void)
{
	/* Configure the PPC driver */
	if (board_has_syv_ppc())
		/* Arg is the USB port number */
		PPC_ENABLE_ALTERNATE(0);
}
DECLARE_HOOK(HOOK_INIT, check_alternate_devices, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_PLATFORM_EC_USB_CHARGER
void bc12_interrupt(enum gpio_signal signal)
{
	usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
}
#endif

/* USB Mux C1 : board_init of PS8743 */
int ps8743_eq_c1_setting(void)
{
	ps8743_write(usb_muxes[1].mux, PS8743_REG_USB_EQ_RX, 0x90);
	return EC_SUCCESS;
}

static void board_usbc_init(void)
{
	if (board_has_syv_ppc()) {
		/* Enable PPC interrupts. */
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_usb_c0_ppc));

#ifdef CONFIG_PLATFORM_EC_USB_CHARGER
		/* Enable BC1.2 interrupts. */
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_usb_c0_bc12));
#endif
	} else {
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(int_usb_c0_ppc));
	}
}
DECLARE_HOOK(HOOK_INIT, board_usbc_init, HOOK_PRIO_POST_DEFAULT);

void ppc_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_SIGNAL(DT_NODELABEL(usb_c0_ppc_int_odl))) {
		ppc_chips[0].drv->interrupt(0);
	}
	if (signal == GPIO_SIGNAL(DT_ALIAS(gpio_usb_c1_ppc_int_odl))) {
		ppc_chips[1].drv->interrupt(1);
	}
}
