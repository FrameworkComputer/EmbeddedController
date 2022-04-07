/*
 * Copyright 2022 The Chromium OS Authors. All rights reserved.
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

#endif /* DT_BINDINGS_USBC_MUX_H_ */
