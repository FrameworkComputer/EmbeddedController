/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define PECI_RD_PKG_CONFIG_WRITE_LENGTH         4
#define PECI_RD_PKG_CONFIG_READ_LENGTH_BYTE     2
#define PECI_RD_PKG_CONFIG_READ_LENGTH_WORD     3
#define PECI_RD_PKG_CONFIG_READ_LENGTH_DWORD    5
#define PECI_RD_PKG_CONFIG_TIMEOUT_US           200

#define PECI_WR_PKG_CONFIG_WRITE_LENGTH_BYTE    6
#define PECI_WR_PKG_CONFIG_WRITE_LENGTH_WORD    7
#define PECI_WR_PKG_CONFIG_WRITE_LENGTH_DWORD   9
#define PECI_WR_PKG_CONFIG_READ_LENGTH          1
#define PECI_WR_PKG_CONFIG_TIMEOUT_US           200


/* RdPkgConfig and WrPkgConfig CPU Thermal and Power Optimiztion Services */
#define PECI_INDEX_PACKAGE_INDENTIFIER_READ     0x00

#define PECI_PARAMS_CPU_ID_INFORMATION          0x0000
#define PECI_PARAMS_PLATFORM_ID                 0x0001
#define PECI_PARAMS_UNCORE_DEVICE_ID            0x0002
#define PECI_PARAMS_LOGICAL_CORES               0x0003
#define PECI_PARAMS_CPU_MICROCODE_REVISION      0x0004

#define PECI_INDEX_TEMP_TARGET_READ             0x10
#define PECI_PARAMS_PROCESSOR_TEMP              0x0000

#define PECI_INDEX_POWER_LIMITS_PL1             0x1A
#define PECI_PARAMS_POWER_LIMITS_PL1            0x0000
#define PECI_PL1_CONTROL_TIME_WINDOWS           (0xDC << 16) /* 28 seconds */
#define PECI_PL1_POWER_LIMIT_ENABLE             (0x01 << 15)
#define PECI_PL1_POWER_LIMIT(x)                 (x << 3)

#define PECI_INDEX_POWER_LIMITS_PL2             0x1B
#define PECI_PARAMS_POWER_LIMITS_PL2            0x0000
#define PECI_PL2_CONTROL_TIME_WINDOWS           (0x00 << 16)
#define PECI_PL2_POWER_LIMIT_ENABLE             (0x01 << 15)
#define PECI_PL2_POWER_LIMIT(x)                 (x << 3)

#define PECI_INDEX_POWER_LIMITS_PSYS_PL2        0x3B
#define PECI_PARAMS_POWER_LIMITS_PSYS_PL2       0x0000
#define PECI_PSYS_PL2_CONTROL_TIME_WINDOWS      (0xDC << 16) /* 28 seconds */
#define PECI_PSYS_PL2_POWER_LIMIT_ENABLE        (0x01 << 15)
#define PECI_PSYS_PL2_POWER_LIMIT(x)            (x << 3)

#define PECI_INDEX_POWER_LIMITS_PL4             0x3C
#define PECI_PARAMS_POWER_LIMITS_PL4            0x0000
#define PECI_PL4_POWER_LIMIT(x)                 (x << 3)


int peci_Rd_Pkg_Config(uint8_t index, uint16_t parameter, int rlen, uint8_t *in);
int peci_Wr_Pkg_Config(uint8_t index, uint16_t parameter, uint32_t data, int wlen);
int espi_oob_retry_receive_date(uint8_t *readBuf);
int espi_oob_peci_transaction(struct peci_data *peci);

int peci_update_PL1(int watt);
int peci_update_PL2(int watt);
int peci_update_PL4(int watt);
int peci_update_PsysPL2(int watt);

/**
 * This function return the peci gettemp value from global variant, the
 * actually value is read from read_peci_over_espi_gettemp();
 *
 * @param idx		no used
 * @param temp_ptr	return temp value pointer
 * @return int		return get value status
 */
int peci_over_espi_temp_sensor_get_val(int idx, int *temp_ptr);
