/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_ELANI80SA_CONFIG_H_
#define ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_ELANI80SA_CONFIG_H_

#define DT_DRV_COMPAT elan_elani80sa

#include "fingerprint_elan80series_private.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/toolchain.h>

#define FP_SENSOR_HWID_ELAN 0x5253

/* The hardware ID information and FW version */
#define PID 0x0903 /* USB product ID */
#define MID 0x01 /* Elan doesn't track model, so this has no meaning. */
#define VERSION 0x100B /* Elan internal firmware version */

/**
 * Elan sensor operation is controlled by sending commands and receiving
 * through the SPI interface. There are several SPI command codes for
 * controlling FP sensor:
 *
 * - START_SCAN            Start scan
 * - START_READ_IMAGE      Start read the image
 * - SRST                  Software reset
 * - FUSE_LOAD             Load OTP trims data to control registers
 * - READ_REG_HEAD         Register single read
 * - WRITE_REG_HEAD        Register burst write
 * - READ_SERIER_REG_HEAD  Register burst read
 * - PAGE_SEL              Register page selection
 * - SENSOR_STATUS         Read sensor status
 * - HWID_LO               Hardware ID low register
 * - HWID_HI               Hardware ID high register
 * - IMG_READY             Image ready register
 * - HV_ENABLE             High voltage enable register
 * - HV_CONTROL            High voltage control Register
 */
#define START_SCAN 0x01
#define START_READ_IMAGE 0x10
#define SRST 0x31
#define FUSE_LOAD 0x04
#define READ_REG_HEAD 0x40
#define WRITE_REG_HEAD 0x80
#define READ_SERIER_REG_HEAD 0xC0
#define PAGE_SEL 0x07
#define SENSOR_STATUS 0x03
#define HWID_LO 0x04
#define HWID_HI 0x02
#define IMG_READY 0x04
#define HV_ENABLE 0x01
#define HV_CONTROL 0x00

/* Sensor pixel resolution */
#define IMAGE_WIDTH 80
#define IMAGE_HEIGHT 80

/**
 * Sensor real image size:
 * ((IMAGE_HEIGHT * ONE_PIXEL_BYTE) + FP_DUMMY_BYTE) * IMAGE_WIDTH
 */
#define FP_DUMMY_BYTE 0
#define ONE_PIXEL_BYTE 2
#define IMAGE_TOTAL_PIXEL (IMAGE_WIDTH * IMAGE_HEIGHT)
#define RAW_PIXEL_SIZE (IMAGE_WIDTH * ONE_PIXEL_BYTE)
#define RAW_DATA_SIZE (RAW_PIXEL_SIZE + FP_DUMMY_BYTE)
#define IMG_BUF_SIZE (RAW_DATA_SIZE * IMAGE_HEIGHT)

/* SPI tx and rx buffer size */
#define ELAN_DMA_LOOP IMAGE_HEIGHT
#define ELAN_DMA_SIZE (IMAGE_TOTAL_PIXEL / ELAN_DMA_LOOP)
#define ELAN_SPI_TX_BUF_SIZE 2
#define ELAN_SPI_RX_BUF_SIZE (IMG_BUF_SIZE / ELAN_DMA_LOOP)

/* These are only supported on the ELANI80SA. */
#define CHARGE_PUMP_HVIC 0x83
#define VOLTAGE_HVIC 0x02

/**
 * ELANI80SA Line Scan timeout configuration:
 * - Single line scan time: ~350us
 * - Loop iterations per scan: ~10
 * - Conservative timeout limit: 20 iterations
 */
#define POLLING_SCAN_TIMER 20

/* Re-calibration timer */
#define REK_TIMES 3

#endif /* ZEPHYR_DRIVERS_FINGERPRINT_FINGERPRINT_ELANI80SA_CONFIG_H_ */
