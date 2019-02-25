/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel BASEBOARD-RVP VBUS detection specific configuration */

#include "task.h"
#include "usb_charge.h"

/* USB VBUS detection configuration */
#ifdef CONFIG_USB_PD_VBUS_DETECT_GPIO
void vbus0_evt(enum gpio_signal signal)
{
#ifdef HAS_TASK_USB_CHG_P0
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_VBUS, 0);
#endif
	task_wake(TASK_ID_PD_C0);
}

#ifdef HAS_TASK_PD_C1
void vbus1_evt(enum gpio_signal signal)
{
#ifdef HAS_TASK_USB_CHG_P1
	task_set_event(TASK_ID_USB_CHG_P1, USB_CHG_EVENT_VBUS, 0);
#endif
	task_wake(TASK_ID_PD_C1);
}
#endif /* HAS_TASK_PD_C1 */
#endif /* CONFIG_USB_PD_VBUS_DETECT_GPIO */
