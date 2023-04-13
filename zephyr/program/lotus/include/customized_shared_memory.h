/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_CUSTOMIZED_SHARED_MEMORY_H
#define __BOARD_CUSTOMIZED_SHARED_MEMORY_H

/* Customized memmap start at offset 0xF00 */
#define EC_CUSTOMIZED_MEMMAP_SYSTEM_FLAGS	0x100
#define EC_CUSTOMIZED_MEMMAP_POWER_STATE	0x101
#define EC_CUSTOMIZED_MEMMAP_WAKE_EVENT		0x102
/*
 * define wake source for keep PCH power
 * BIT0 for RTCwake
 * BIT1 for USBwake
 */
#define RTCWAKE  BIT(0)
#define USBWAKE  BIT(1)

/* Battery */
#define EC_CUSTOMIZED_MEMMAP_BATT_AVER_TEMP	0x103
#define EC_CUSTOMIZED_MEMMAP_BATT_CHARGE_CURR	0x104 /* Reserved */
#define EC_CUSTOMIZED_MEMMAP_BATT_PERCENTAGE	0x106
#define EC_CUSTOMIZED_MEMMAP_BATT_STATUS	0x107
#define EC_CUSTOMIZED_MEMMAP_BATT_TRIP_POINT	0x108
#define EC_CUSTOMIZED_MEMMAP_BATT_MANUF_DAY	0x144 /* Manufacturer date - day */
#define EC_CUSTOMIZED_MEMMAP_BATT_MANUF_MONTH	0x145 /* Manufacturer date - month */
#define EC_CUSTOMIZED_MEMMAP_BATT_MANUF_YEAR	0x146 /* Manufacturer date - year */

#define EC_BATT_FLAG_FULL			BIT(0) /* Full Charged */
#define EC_BATT_TYPE				BIT(1) /* (0: NiMh,1: LION) */
#define EC_BATT_MODE				BIT(2) /* (0=mW, 1=mA) */

#define EC_CUSTOMIZED_MEMMAP_DPTF_EVENT		0x110 /* Reserved */
#define EC_CUSTOMIZED_MEMMAP_PROJECTID		0x111 /* Reserved */

/* UCSI */
#define EC_CUSTOMIZED_MEMMAP_UCSI_VERSION	0x112
#define EC_CUSTOMIZED_MEMMAP_UCSI_CONN_CHANGE	0x114
#define EC_CUSTOMIZED_MEMMAP_UCSI_DATA_LEN	0x115
#define EC_CUSTOMIZED_MEMMAP_UCSI_RESERVE	0x116
#define EC_CUSTOMIZED_MEMMAP_UCSI_CCI		0x117
#define EC_CUSTOMIZED_MEMMAP_UCSI_COMMAND	0x118
#define EC_CUSTOMIZED_MEMMAP_UCSI_CTR_DATA_LEN	0x119
#define EC_CUSTOMIZED_MEMMAP_UCSI_CTR_SPECIFIC	0x11A
#define EC_CUSTOMIZED_MEMMAP_UCSI_MESSAGE_IN	0x120
#define EC_CUSTOMIZED_MEMMAP_UCSI_MESSAGE_OUT	0x130


#define EC_CUSTOMIZED_MEMMAP_SERIAL_STRCUT_INFO	0x140
#define EC_CUSTOMIZED_MEMMAP_BIOS_SETUP_FUNC	0x148
#define EC_CUSTOMIZED_MEMMAP_PD_VERSION		0x14C

#endif /* __BOARD_CUSTOMIZED_SHARED_MEMORY_H */
