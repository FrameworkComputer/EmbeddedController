/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This file contains public definitions and prototypes for the CRC functions.
 */

#ifndef __UTIL_UUT_LIB_CRC_H
#define __UTIL_UUT_LIB_CRC_H

/*-------------------------------------------------------------------*\
 *                                                                   *
 *   Library         : lib_crc                                       *
 *   File            : lib_crc.h                                     *
 *   Author          : Lammert Bies  1999-2005                       *
 *   E-mail          : info@lammertbies.nl                           *
 *   Language        : ANSI C                                        *
 *                                                                   *
 *                                                                   *
 *   Description                                                     *
 *   ===========                                                     *
 *                                                                   *
 *   The file lib_crc.h contains public definitions  and  proto-     *
 *   types for the CRC functions present in lib_crc.c.               *
 *                                                                   *
 *                                                                   *
 *   Dependencies                                                    *
 *   ============                                                    *
 *                                                                   *
 *   none                                                            *
 *                                                                   *
 *                                                                   *
 *   Modification history                                            *
 *   ====================                                            *
 *                                                                   *
 *   Date        Version Comment                                     *
 *                                                                   *
 *   2005-02-14  1.12    Added CRC-CCITT with initial value 0        *
 *                                                                   *
 *   2005-02-05  1.11    Fixed bug in CRC-DNP routine                *
 *                                                                   *
 *   2005-02-04  1.10    Added CRC-DNP routines                      *
 *                                                                   *
 *   2005-01-07  1.02    Changes in tst_crc.c                        *
 *                                                                   *
 *   1999-02-21  1.01    Added FALSE and TRUE mnemonics              *
 *                                                                   *
 *   1999-01-22  1.00    Initial source                              *
 *                                                                   *
 *-------------------------------------------------------------------*
 */

/*---------------------------------------------------------------------------
 * CRC library constant definitions
 *---------------------------------------------------------------------------
 */
#define CRC_VERSION "1.12"

/*---------------------------------------------------------------------------
 * CRC library API
 *---------------------------------------------------------------------------
 */
unsigned short update_crc(unsigned short crc, unsigned char c);

#endif /* __UTIL_UUT_LIB_CRC_H */
