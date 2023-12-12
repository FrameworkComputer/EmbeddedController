/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef DT_BINDINGS_USBC_MUX_H_
#define DT_BINDINGS_USBC_MUX_H_

#ifndef BIT
#define BIT(n) (1U << n)
#endif

/* Flags used for cros-ec,usbc-mux flags property / struct usb_mux.flags */
#define USB_MUX_FLAG_NOT_TCPC BIT(0) /* TCPC/MUX device used only as MUX */
#define USB_MUX_FLAG_SET_WITHOUT_FLIP BIT(1) /* SET should not flip */
#define USB_MUX_FLAG_RESETS_IN_G3 BIT(2) /* Mux chip will reset in G3 */
#define USB_MUX_FLAG_POLARITY_INVERTED BIT(3) /* Mux polarity is inverted */
#define USB_MUX_FLAG_CAN_IDLE BIT(4) /* MUX supports idle mode */

/* Flags representing mux state */
#define USB_PD_MUX_NONE 0 /* Open switch */
#define USB_PD_MUX_USB_ENABLED BIT(0) /* USB connected */
#define USB_PD_MUX_DP_ENABLED BIT(1) /* DP connected */
#define USB_PD_MUX_POLARITY_INVERTED BIT(2) /* CC line Polarity inverted */
#define USB_PD_MUX_SAFE_MODE BIT(5) /* DP is in safe mode */
#define USB_PD_MUX_TBT_COMPAT_ENABLED BIT(6) /* TBT compat enabled */
#define USB_PD_MUX_USB4_ENABLED BIT(7) /* USB4 enabled */

/* USB-C Dock connected */
#define USB_PD_MUX_DOCK (USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED)

#endif /* DT_BINDINGS_USBC_MUX_H_ */
