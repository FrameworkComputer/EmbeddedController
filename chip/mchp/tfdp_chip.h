/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/** @file tfdp_chip.h
 *MCHP MEC TFDP Peripheral Library API
 */
/** @defgroup MCHP MEC Peripherals Trace
 */

#ifndef _TFDP_CHIP_H
#define _TFDP_CHIP_H

#include <stdint.h>


#ifdef CONFIG_MCHP_TFDP

#undef TRACE0
#undef TRACE1
#undef TRACE2
#undef TRACE3
#undef TRACE4
#undef TRACE11
#undef TRACE12
#undef TRACE13
#undef TRACE14
#undef trace0
#undef trace1
#undef trace2
#undef trace3
#undef trace4
#undef trace11
#undef trace12
#undef trace13
#undef trace14

#define MCHP_TFDP_BASE_ADDR	(0x40008c00ul)

#define TFDP_FRAME_START   (0xFD)

#define TFDP_POWER_ON   (1u)
#define TFDP_POWER_OFF  (0u)

#define TFDP_ENABLE         (1u)
#define TFDP_DISABLE        (0u)
#define TFDP_CFG_PINS       (1u)
#define TFDP_NO_CFG_PINS    (0u)

#define  MCHP_TRACE_MASK_IRQ

#define TFDP_DELAY()

#ifdef __cplusplus
extern "C" {
#endif

void tfdp_power(uint8_t pwr_on);
void tfdp_enable(uint8_t en, uint8_t pin_cfg);
void TFDPTrace0(uint16_t nbr);
void TFDPTrace1(uint16_t nbr, uint32_t p1);
void TFDPTrace2(uint16_t nbr, uint32_t p1,
		uint32_t p2);
void TFDPTrace3(uint16_t nbr, uint32_t p1,
		uint32_t p2, uint32_t p3);
void TFDPTrace4(uint16_t nbr, uint32_t p1, uint32_t p2,
		uint32_t p3, uint32_t p4);
void TFDPTrace11(uint16_t nbr, uint32_t p1);
void TFDPTrace12(uint16_t nbr, uint32_t p1, uint32_t p2);
void TFDPTrace13(uint16_t nbr, uint32_t p1, uint32_t p2,
		 uint32_t p3);
void TFDPTrace14(uint16_t nbr, uint32_t p1, uint32_t p2,
		 uint32_t p3, uint32_t p4);

#ifdef __cplusplus
}
#endif

#define TRACE0(nbr, cat, b, str) TFDPTrace0(nbr)
#define TRACE1(nbr, cat, b, str, p1) TFDPTrace1(nbr, p1)
#define TRACE2(nbr, cat, b, str, p1, p2) TFDPTrace2(nbr, p1, p2)
#define TRACE3(nbr, cat, b, str, p1, p2, p3) TFDPTrace3(nbr, p1, p2, p3)
#define TRACE4(nbr, cat, b, str, p1, p2, p3, p4) TFDPTrace4(nbr, p1, p2, \
	p3, p4)
#define TRACE11(nbr, cat, b, str, p1) TFDPTrace11(nbr, p1)
#define TRACE12(nbr, cat, b, str, p1, p2) TFDPTrace12(nbr, p1, p2)
#define TRACE13(nbr, cat, b, str, p1, p2, p3) TFDPTrace13(nbr, p1, p2, p3)
#define TRACE14(nbr, cat, b, str, p1, p2, p3, p4) \
	TFDPTrace14(nbr, p1, p2, p3, p4)


#else /* #ifdef MCHP_TRACE */

/* !!! To prevent compiler warnings of unused parameters,
 * when trace is disabled by TRGEN source processing,
 * you can either:
 * 1. Disable compiler's unused parameter warning
 * 2. Change these macros to write parameters to a read-only
 *    register.
 */
#define tfdp_power(pwr_on)
#define tfdp_enable(en, pin_cfg)
#define TRACE0(nbr, cat, b, str)
#define TRACE1(nbr, cat, b, str, p1)
#define TRACE2(nbr, cat, b, str, p1, p2)
#define TRACE3(nbr, cat, b, str, p1, p2, p3)
#define TRACE4(nbr, cat, b, str, p1, p2, p3, p4)
#define TRACE11(nbr, cat, b, str, p1)
#define TRACE12(nbr, cat, b, str, p1, p2)
#define TRACE13(nbr, cat, b, str, p1, p2, p3)
#define TRACE14(nbr, cat, b, str, p1, p2, p3, p4)

#endif /* #ifdef CONFIG_MCHP_TFDP */

/*
 * Always define lower case traceN(...) as blank (fully removed)
 */
#define trace0(nbr, cat, b, str)
#define trace1(nbr, cat, b, str, p1)
#define trace2(nbr, cat, b, str, p1, p2)
#define trace3(nbr, cat, b, str, p1, p2, p3)
#define trace4(nbr, cat, b, str, p1, p2, p3, p4)
#define trace11(nbr, cat, b, str, p1)
#define trace12(nbr, cat, b, str, p1, p2)
#define trace13(nbr, cat, b, str, p1, p2, p3)
#define trace14(nbr, cat, b, str, p1, p2, p3, p4)

#endif /* #ifndef _TFDP_CHIP_H */
/* end tfdp_chip.h */
/**   @}
 */
