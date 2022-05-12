/*
 * Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef DT_BINDINGS_USB_PD_TCPM_H_
#define DT_BINDINGS_USB_PD_TCPM_H_

/*
 * Macros for tcpc_config_t flags field.
 *
 * Bit 0 --> Polarity for TCPC alert. Set to 1 if alert is active high.
 * Bit 1 --> Set to 1 if TCPC alert line is open-drain instead of push-pull.
 * Bit 2 --> Polarity for TCPC reset. Set to 1 if reset line is active high.
 * Bit 3 --> Set to 1 if TCPC is using TCPCI Revision 2.0
 * Bit 4 --> Set to 1 if TCPC is using TCPCI Revision 2.0 but does not support
 *           the vSafe0V bit in the EXTENDED_STATUS_REGISTER
 * Bit 5 --> Set to 1 to prevent TCPC setting debug accessory control
 * Bit 6 --> TCPC controls VCONN (even when CONFIG_USB_PD_TCPC_VCONN is off)
 * Bit 7 --> TCPC controls FRS (even when CONFIG_USB_PD_FRS_TCPC is off)
 * Bit 8 --> TCPC enable VBUS monitoring
 */
#define TCPC_FLAGS_ALERT_ACTIVE_HIGH	BIT(0)
#define TCPC_FLAGS_ALERT_OD		BIT(1)
#define TCPC_FLAGS_RESET_ACTIVE_HIGH	BIT(2)
#define TCPC_FLAGS_TCPCI_REV2_0		BIT(3)
#define TCPC_FLAGS_TCPCI_REV2_0_NO_VSAFE0V	BIT(4)
#define TCPC_FLAGS_NO_DEBUG_ACC_CONTROL	BIT(5)
#define TCPC_FLAGS_CONTROL_VCONN	BIT(6)
#define TCPC_FLAGS_CONTROL_FRS		BIT(7)
#define TCPC_FLAGS_VBUS_MONITOR		BIT(8)

#endif
