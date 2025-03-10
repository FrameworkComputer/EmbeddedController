/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fingerprint sensor interface */

#ifndef __CROS_EC_FPSENSOR_FPSENSOR_H
#define __CROS_EC_FPSENSOR_FPSENSOR_H

#include "common.h"
#include "ec_commands.h"
#include "fpsensor_alg.h"
#include "fpsensor_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SPI_FP_DEVICE
#define SPI_FP_DEVICE (&spi_devices[0])
#endif

/*  Four-character-code */
#define FOURCC(a, b, c, d)                                              \
	((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | \
	 ((uint32_t)(d) << 24))

/* 8-bit greyscale pixel format as defined by V4L2 headers */
#define V4L2_PIX_FMT_GREY FOURCC('G', 'R', 'E', 'Y')

/* --- functions provided by the sensor-specific driver --- */

/**
 * @brief Initialize the connected sensor hardware and put it in a low power
 * mode.
 *
 * It is expected that @ref fp_sensor_init and @ref fp_sensor_deinit may be
 * called multiple times during runtime to clear the active user session. For
 * example, on user logout, @ref fp_sensor_deinit is called and then @ref
 * fp_sensor_init is called.
 *
 * This function updates a static error variable with the following error codes
 * (whenever detected), which is read using the @ref fp_sensor_get_info function
 * defined in the same source file. The errors are ultimately read from the
 * application processor using the host command EC_CMD_FP_INFO:
 *
 * FP_ERROR_DEAD_PIXELS: The number of dead pixels detected on the sensor during
 * maintenance.
 *
 * FP_ERROR_DEAD_PIXELS_UNKNOWN: The number of dead pixels has not been tested
 * yet.
 *
 * FP_ERROR_NO_IRQ: No interrupt signal received from the sensor.
 *
 * FP_ERROR_SPI_COMM: Error in SPI communication with the sensor.
 *
 * FP_ERROR_BAD_HWID: Invalid sensor hardware ID.
 *
 * FP_ERROR_INIT_FAIL: Sensor initialization failed.
 *
 *
 * @return EC_SUCCESS always
 */
int fp_sensor_init(void);

/**
 * De-initialize the sensor hardware.
 *
 * This function closes the fingerprint sensor. It also shuts down any ongoing
 * biometric algorithm processes. It is called as part of the sensor reset
 * process.
 *
 * @warning This function **must be called before** releasing the resources used
 * by the matching algorithm including @p fp_buffer.
 *
 * @return 0 on success
 * @return negative value on error including failures to properly exit from the
 * biometric algorithm or close the fingerprint sensor.
 */
int fp_sensor_deinit(void);

/**
 * Fill the @p ec_response_fp_info buffer with the sensor information
 * as required by the EC_CMD_FP_INFO host command.
 *
 * Fills both the static information and information read from the sensor at
 * runtime such as sensor_id, errors, etc.
 *
 * @param[out] resp sensor info
 *
 * @return EC_SUCCESS on success
 * @return EC_RES_ERROR on error
 */
int fp_sensor_get_info(struct ec_response_fp_info *resp);

/**
 * Put the sensor in its lowest power state.
 *
 * fp_sensor_configure_detect needs to be called to restore finger detection
 * functionality.
 */
void fp_sensor_low_power(void);

/**
 * Configure finger detection.
 *
 * Send the settings to the sensor, so it is properly configured to detect
 * the presence of a finger.
 */
void fp_configure_detect(void);

/**
 * Returns the status of the finger on the sensor.
 * (assumes fp_configure_detect was called before)
 *
 * @return finger_state A value from @ref finger_state enum.
 */
enum finger_state fp_finger_status(void);

/**
 * Image captured but quality is too low
 */
#define FP_SENSOR_LOW_IMAGE_QUALITY 1
/**
 * Finger removed before image was captured
 */
#define FP_SENSOR_TOO_FAST 2

/**
 * Sensor not fully covered by finger
 */
#define FP_SENSOR_LOW_SENSOR_COVERAGE 3

/**
 * Acquires a fingerprint image with specific capture mode.
 *
 * This function captures a fingerprint image, allowing the caller to set a
 * specific @p mode to one of the fp_capture_type constants. For capture
 * types that require a finger presence (e.g. simple, qual test,etc.), this
 * function is called once the finger has been detected and cover enough area of
 * the sensor. Otherwise, it does the acquisition immediately (e.g. pattern0,
 * reset, etc.).
 *
 * @param[out] image_data Image from sensor. Buffer must be allocated by
 * caller with size FP_SENSOR_IMAGE_SIZE.
 * @param mode enum fp_capture_type
 *
 * @return 0 on success
 * @return negative value on error
 * @return FP_SENSOR_LOW_IMAGE_QUALITY on image captured but quality is too low
 * @return FP_SENSOR_TOO_FAST on finger removed before image was captured
 * @return FP_SENSOR_LOW_SENSOR_COVERAGE on sensor not fully covered by finger
 */
int fp_acquire_image_with_mode(uint8_t *image_data, enum fp_capture_type mode);

/*
 * TODO(b/378523729): Refactor fpsensor API so that error_state is maintained by
 * our code.
 */
/**
 * Runs a test for defective pixels.
 *
 * Should be triggered periodically by the client. The maintenance command can
 * take several hundred milliseconds to run. The function updates the
 * `error_state`, which is a uint16_t variable where the error state will be
 * stored. The function must update error_state about dead pixels by setting
 * bits in the FP_ERROR_DEAD_PIXELS field.
 *
 * @return EC_ERROR_HW_INTERNAL on error (such as finger on sensor)
 * @return EC_SUCCESS on success
 */
int fp_maintenance(void);

#ifdef CONFIG_ZEPHYR
/**
 * Put the sensor into idle state
 *
 * This function is useful if it's necessary e.g. to leave 'detect' mode
 * due to timeout or user cancel.
 *
 * @return 0 on success
 * @return negative value on error
 */
int fp_idle(void);

#endif /* CONFIG_ZEPHYR */

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_FPSENSOR_FPSENSOR_H */
