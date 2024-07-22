/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ELAN_SETTING_H
#define ELAN_SETTING_H

#include "common.h"

#include <stdint.h>

/* The hardware ID information and FW version */
#define VID 0x04F3
#define PID 0x0903
#define MID 0x01
#define VERSION 0x100B

/* The 16-bit hardware ID  */
#define FP_SENSOR_HWID_ELAN FP_SENSOR_HWID_ELAN_80SG
#define FP_SENSOR_HWID_ELAN_80SG 0x4f4f

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

/* Sensor type name */
#define EFSA515 1
#define EFSA80SC 2
#define EFSA80SG 3
#if defined(CONFIG_FP_SENSOR_ELAN80)
#define IC_SELECTION EFSA80SC
#elif defined(CONFIG_FP_SENSOR_ELAN515)
#define IC_SELECTION EFSA515
#elif defined(CONFIG_FP_SENSOR_ELAN80SG)
#define IC_SELECTION EFSA80SG
#endif

/* Sensor pixel resolution */
#if (IC_SELECTION == EFSA80SC || IC_SELECTION == EFSA80SG)
#define IMAGE_WIDTH 80
#define IMAGE_HEIGHT 80
#elif (IC_SELECTION == EFSA515)
#define IMAGE_WIDTH 52
#define IMAGE_HEIGHT 150
#endif

/**
 * Sensor real image size:
 * ((IMAGE_HEIGHT * ONE_PIXEL_BYTE) + FP_DUMMY_BYTE) * IMAGE_WIDTH
 */
#define FP_DUMMY_BYTE 2
#define ONE_PIXEL_BYTE 2
#define IMAGE_TOTAL_PIXEL (IMAGE_WIDTH * IMAGE_HEIGHT)
#define RAW_PIXEL_SIZE (IMAGE_WIDTH * ONE_PIXEL_BYTE)
#define RAW_DATA_SIZE (RAW_PIXEL_SIZE + FP_DUMMY_BYTE)
#define IMG_BUF_SIZE (RAW_DATA_SIZE * IMAGE_HEIGHT)

/* SPI tx and rx buffer size */
#if (IC_SELECTION == EFSA515)
#define ELAN_DMA_LOOP 5
#else
#define ELAN_DMA_LOOP 4
#endif
#define ELAN_DMA_SIZE (IMAGE_TOTAL_PIXEL / ELAN_DMA_LOOP)
#define ELAN_SPI_TX_BUF_SIZE 2
#define ELAN_SPI_RX_BUF_SIZE (IMG_BUF_SIZE / ELAN_DMA_LOOP)

/* These are only supported on the EFSA80SG. */
#define CHARGE_PUMP_HVIC 0x83
#define VOLTAGE_HVIC 0x00

/* Polling scan status counter */
#define POLLING_SCAN_TIMER 10000

/* Re-calibration timer */
#define REK_TIMES 3

/* Console output macros */
#define LOGE_SA(format, args...) cprints(CC_FP, format, ##args)

/**
 * Set ELAN fingerprint sensor register initialization
 *
 * @return 0 on success.
 *         negative value on error.
 */
__staticlib int register_initialization(void);

/**
 * To calibrate ELAN fingerprint sensor and keep the calibration results
 * for correcting fingerprint image data
 *
 * @return 0 on success.
 *         negative value on error.
 */
__staticlib int calibration(void);

#endif /* _ELAN_SETTING_H */
