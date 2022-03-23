/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * @file
 * @brief Microchip XEC register structure definitions used by the Chrome OS EC.
 */

#ifndef _MICROCHIP_XEC_REG_DEF_CROS_H
#define _MICROCHIP_XEC_REG_DEF_CROS_H

/* RTC register structure */
struct rtc_hw {
__IOM uint8_t   SECV;     /*!< (@ 0x0000) RTC seconds value */
__IOM uint8_t   SECA;     /*!< (@ 0x0001) RTC seconds alarm */
__IOM uint8_t   MINV;     /*!< (@ 0x0002) RTC minutes value */
__IOM uint8_t   MINA;     /*!< (@ 0x0003) RTC minutes alarm */
__IOM uint8_t   HRSV;     /*!< (@ 0x0004) RTC hours value, AM/PM indicator */
__IOM uint8_t   HRSA;     /*!< (@ 0x0005) RTC hours alarm */
__IOM uint8_t   DOWV;     /*!< (@ 0x0006) RTC day of week value */
__IOM uint8_t   DOMV;     /*!< (@ 0x0007) RTC day of month value */
__IOM uint8_t   MONV;     /*!< (@ 0x0008) RTC month value */
__IOM uint8_t   YEARV;    /*!< (@ 0x0009) RTC Year value */
__IOM uint8_t   REGA;     /*!< (@ 0x000A) RTC register A */
__IOM uint8_t   REGB;     /*!< (@ 0x000B) RTC register B */
__IOM uint8_t   REGC;     /*!< (@ 0x000C) RTC register C */
__IOM uint8_t   REGD;     /*!< (@ 0x000D) RTC register D */
__IM  uint16_t  RESERVED;
__IOM uint8_t   CTRL;     /*!< (@ 0x0010) RTC control */
__IM  uint8_t   RESERVED1;
__IM  uint16_t  RESERVED2;
__IOM uint8_t   WKA;      /*!< (@ 0x0014) RTC week alarm */
__IM  uint8_t   RESERVED3;
__IM  uint16_t  RESERVED4;
__IOM uint32_t  DLSF;     /*!< (@ 0x0018) RTC daylight savings forward */
__IOM uint32_t  DLSB;     /*!< (@ 0x001C) RTC daylight savings backward */
};

#endif /* _MICROCHIP_XEC_REG_DEF_CROS_H */
