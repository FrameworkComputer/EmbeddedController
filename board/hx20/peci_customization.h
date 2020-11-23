/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define PECI_RD_PKG_CONFIG_WRITE_LENGTH         4
#define PECI_RD_PKG_CONFIG_READ_LENGTH_BYTE     2
#define PECI_RD_PKG_CONFIG_READ_LENGTH_WORD     3
#define PECI_RD_PKG_CONFIG_READ_LENGTH_DWORD    5
#define PECI_RD_PKG_CONFIG_TIMEOUT_US           200


/* RdPkgConfig and WrPkgConfig CPU Thermal and Power Optimiztion Services */
#define PECI_INDEX_PACKAGE_INDENTIFIER_READ     0x00

#define PECI_PARAMS_CPU_ID_INFORMATION          0x0000
#define PECI_PARAMS_PLATFORM_ID                 0x0001
#define PECI_PARAMS_UNCORE_DEVICE_ID            0x0002
#define PECI_PARAMS_LOGICAL_CORES               0x0003
#define PECI_PARAMS_CPU_MICROCODE_REVISION      0x0004

#define PECI_INDEX_TEMP_TARGET_READ             0x10
#define PECI_PARAMS_PROCESSOR_TEMP              0x0000

int peci_Rd_Pkg_Config(uint8_t index, uint16_t parameter, int rlen, uint8_t *in);