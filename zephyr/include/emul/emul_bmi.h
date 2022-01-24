/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Backend API for BMI emulator
 */

#ifndef __EMUL_BMI_H
#define __EMUL_BMI_H

#include <drivers/emul.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>

/**
 * @brief BMI emulator backend API
 * @defgroup bmi_emul BMI emulator
 * @{
 *
 * BMI emulator supports responses to all write and read I2C messages.
 * Accelerometer and gyroscope registers are obtained from internal emulator
 * state, range register and offset. FIFO is fully simulated. Emulator can be
 * extended to support more models of BMI.
 * Application may alter emulator state:
 *
 * - define a Device Tree overlay file to set which inadvisable driver behaviour
 *   should be treated as errors and which model is emulated
 * - call @ref bmi_emul_set_reg and @ref bmi_emul_get_reg to set and get value
 *   of BMI registers
 * - call @ref bmi_emul_set_off and @ref bmi_emul_get_off to set and get
 *   internal offset value
 * - call @ref bmi_emul_set_value and @ref bmi_emul_get_value to set and get
 *   accelerometer or gyroscope value
 * - call bmi_emul_set_err_* to change emulator behaviour on inadvisable driver
 *   behaviour
 * - call @ref bmi_emul_simulate_cmd_exec_time to enable or disable simulation
 *   of command execution time
 * - call @ref bmi_emul_append_frame to add frame to FIFO
 * - call @reg bmi_emul_set_skipped_frames to generate skip frame on next access
 *   to FIFO
 * - call functions from emul_common_i2c.h to setup custom handlers for I2C
 *   messages
 */

/**
 * Axis argument used in @ref bmi_emul_set_value @ref bmi_emul_get_value
 * @ref bmi_emul_set_off and @ref bmi_emul_get_off
 */
enum bmi_emul_axis {
	BMI_EMUL_ACC_X,
	BMI_EMUL_ACC_Y,
	BMI_EMUL_ACC_Z,
	BMI_EMUL_GYR_X,
	BMI_EMUL_GYR_Y,
	BMI_EMUL_GYR_Z,
};

/** BMI emulator models */
#define BMI_EMUL_160		1
#define BMI_EMUL_260		2

/** Last register supported by emulator */
#define BMI_EMUL_MAX_REG	0x80
/** Maximum number of registers that can be backed in NVM */
#define BMI_EMUL_MAX_NVM_REGS	10

/** Headers used in FIFO frames */
#define BMI_EMUL_FIFO_HEAD_SKIP			0x40
#define BMI_EMUL_FIFO_HEAD_TIME			0x44
#define BMI_EMUL_FIFO_HEAD_CONFIG		0x48
#define BMI_EMUL_FIFO_HEAD_EMPTY		0x80
#define BMI_EMUL_FIFO_HEAD_DATA			0x80
#define BMI_EMUL_FIFO_HEAD_DATA_MAG		BIT(4)
#define BMI_EMUL_FIFO_HEAD_DATA_GYR		BIT(3)
#define BMI_EMUL_FIFO_HEAD_DATA_ACC		BIT(2)
#define BMI_EMUL_FIFO_HEAD_DATA_TAG_MASK	0x03

/**
 * Acceleration 1g in internal emulator units. It is helpful for using
 * functions @ref bmi_emul_set_value @ref bmi_emul_get_value
 * @ref bmi_emul_set_off and @ref bmi_emul_get_off
 */
#define BMI_EMUL_1G		BIT(14)
/**
 * Gyroscope 125°/s in internal emulator units. It is helpful for using
 * functions @ref bmi_emul_set_value @ref bmi_emul_get_value
 * @ref bmi_emul_set_off and @ref bmi_emul_get_off
 */
#define BMI_EMUL_125_DEG_S	BIT(15)

/** Type of frames that can be added to the emulator frames list */
#define BMI_EMUL_FRAME_CONFIG	BIT(0)
#define BMI_EMUL_FRAME_ACC	BIT(1)
#define BMI_EMUL_FRAME_MAG	BIT(2)
#define BMI_EMUL_FRAME_GYR	BIT(3)

/**
 * Code returned by model specific handle_read and handle_write functions, when
 * RO register is accessed on write or WO register is accessed on read
 */
#define BMI_EMUL_ACCESS_E	1

/** Structure used to describe single FIFO frame */
struct bmi_emul_frame {
	/** Type of frame */
	uint8_t type;
	/** Tag added to data frame */
	uint8_t tag;
	/** Value used in config frame */
	uint8_t config;
	/** Accelerometer sensor values in internal emulator units */
	int32_t acc_x;
	int32_t acc_y;
	int32_t acc_z;
	/** Gyroscope sensor values in internal emulator units */
	int32_t gyr_x;
	int32_t gyr_y;
	int32_t gyr_z;
	/** Magnetometer/other sensor values in internal emulator units */
	int32_t mag_x;
	int32_t mag_y;
	int32_t mag_z;
	int32_t rhall;

	/** Pointer to next frame or NULL */
	struct bmi_emul_frame *next;
};

/** Structure describing specific BMI model */
struct bmi_emul_type_data {
	/** Indicate if time frame should follow config frame */
	bool sensortime_follow_config_frame;

	/**
	 * @brief Compute register address that acctually will be accessed, when
	 *        selected register is @p reg and there was @p byte handled in
	 *        the current I2C message
	 *
	 * @param emul Pointer to BMI emulator
	 * @param reg Selected register
	 * @param byte Number of handled bytes in the current I2C message
	 * @param read If current I2C message is read
	 *
	 * @return Register address that will be accessed
	 */
	int (*access_reg)(struct i2c_emul *emul, int reg, int byte, bool read);

	/**
	 * @brief Model specific write function. It should modify state of
	 *        emulator if required.
	 *
	 * @param regs Pointer to array of emulator's registers
	 * @param emul Pointer to BMI emulator
	 * @param reg Selected register
	 * @param byte Number of handled bytes in this write command
	 * @param val Value that is being written
	 *
	 * @return 0 on success
	 * @return BMI_EMUL_ACCESS_E on RO register access
	 * @return other on error
	 */
	int (*handle_write)(uint8_t *regs, struct i2c_emul *emul, int reg,
			    int byte, uint8_t val);
	/**
	 * @brief Model specific read function. It should modify state of
	 *        emulator if required. @p buf should be set to response value.
	 *
	 * @param regs Pointer to array of emulator's registers
	 * @param emul Pointer to BMI emulator
	 * @param reg Selected register
	 * @param byte Byte which is accessed during block read
	 * @param buf Pointer where read byte should be stored
	 *
	 * @return 0 on success
	 * @return BMI_EMUL_ACCESS_E on WO register access
	 * @return other on error
	 */
	int (*handle_read)(uint8_t *regs, struct i2c_emul *emul, int reg,
			   int byte, char *buf);
	/**
	 * @brief Model specific reset function. It should modify state of
	 *        emulator to imitate after reset conditions.
	 *
	 * @param regs Pointer to array of emulator's registers
	 * @param emul Pointer to BMI emulator
	 */
	void (*reset)(uint8_t *regs, struct i2c_emul *emul);

	/** Array of reserved bits mask for each register */
	const uint8_t *rsvd_mask;

	/** Array of registers that are backed in NVM */
	const int *nvm_reg;
	/** Number of registers backed in NVM */
	int nvm_len;

	/** Gyroscope X axis register */
	int gyr_off_reg;
	/** Accelerometer X axis register */
	int acc_off_reg;
	/** Gyroscope 9 and 8 bits register */
	int gyr98_off_reg;
};

/**
 * @brief Get BMI160 model specific structure.
 *
 * @return Pointer to BMI160 specific structure
 */
const struct bmi_emul_type_data *get_bmi160_emul_type_data(void);
/**
 * @brief Get BMI260 model specific structure.
 *
 * @return Pointer to BMI260 specific structure
 */
const struct bmi_emul_type_data *get_bmi260_emul_type_data(void);

/**
 * @brief Get pointer to BMI emulator using device tree order number.
 *
 * @param ord Device tree order number obtained from DT_DEP_ORD macro
 *
 * @return Pointer to BMI emulator
 */
struct i2c_emul *bmi_emul_get(int ord);

/**
 * @brief Set value of given register of BMI
 *
 * @param emul Pointer to BMI emulator
 * @param reg Register address which value will be changed
 * @param val New value of the register
 */
void bmi_emul_set_reg(struct i2c_emul *emul, int reg, uint8_t val);

/**
 * @brief Get value of given register of BMI
 *
 * @param emul Pointer to BMI emulator
 * @param reg Register address
 *
 * @return Value of the register
 */
uint8_t bmi_emul_get_reg(struct i2c_emul *emul, int reg);

/**
 * @brief Get internal value of offset for given axis and sensor
 *
 * @param emul Pointer to BMI emulator
 * @param axis Axis to access
 *
 * @return Offset of given axis. LSB for accelerometer is 0.061mg and for
 *         gyroscope is 0.0037°/s.
 */
int16_t bmi_emul_get_off(struct i2c_emul *emul, enum bmi_emul_axis axis);

/**
 * @brief Set internal value of offset for given axis and sensor
 *
 * @param emul Pointer to BMI emulator
 * @param axis Axis to access
 * @param val New value of given axis. LSB for accelerometer is 0.061mg and for
 *            gyroscope is 0.0037°/s.
 */
void bmi_emul_set_off(struct i2c_emul *emul, enum bmi_emul_axis axis,
		      int16_t val);

/**
 * @brief Get internal value of sensor for given axis
 *
 * @param emul Pointer to BMI emulator
 * @param axis Axis to access
 *
 * @return Sensor value of given axis. LSB for accelerometer is 0.061mg and for
 *         gyroscope is 0.0037°/s.
 */
int32_t bmi_emul_get_value(struct i2c_emul *emul, enum bmi_emul_axis axis);

/**
 * @brief Set internal value of sensor for given axis
 *
 * @param emul Pointer to BMI emulator
 * @param axis Axis to access
 * @param val New value of given axis. LSB for accelerometer is 0.061mg and for
 *            gyroscope is 0.0037°/s.
 */
void bmi_emul_set_value(struct i2c_emul *emul, enum bmi_emul_axis axis,
			int32_t val);

/**
 * @brief Set if error should be generated when read only register is being
 *        written
 *
 * @param emul Pointer to BMI emulator
 * @param set Check for this error
 */
void bmi_emul_set_err_on_ro_write(struct i2c_emul *emul, bool set);

/**
 * @brief Set if error should be generated when reserved bits of register are
 *        not set to 0 on write I2C message
 *
 * @param emul Pointer to BMI emulator
 * @param set Check for this error
 */
void bmi_emul_set_err_on_rsvd_write(struct i2c_emul *emul, bool set);

/**
 * @brief Set if error should be generated when write only register is read
 *
 * @param emul Pointer to BMI emulator
 * @param set Check for this error
 */
void bmi_emul_set_err_on_wo_read(struct i2c_emul *emul, bool set);

/**
 * @brief Set if effect of simulated command should take place after simulated
 *        time pass from issuing command.
 *
 * @param emul Pointer to BMI emulator
 * @param set Simulate command execution time
 */
void bmi_emul_simulate_cmd_exec_time(struct i2c_emul *emul, bool set);

/**
 * @brief Set number of skipped frames. It will generate skip frame on next
 *        access to FIFO. After that number of skipped frames is reset to 0.
 *
 * @param emul Pointer to BMI emulator
 * @param skip Number of skipped frames
 */
void bmi_emul_set_skipped_frames(struct i2c_emul *emul, uint8_t skip);

/**
 * @brief Clear all FIFO frames, set current frame to empty and reset fifo_skip
 *        counter
 *
 * @param emul Pointer to BMI emulator
 * @param tag_time Indicate if sensor time should be included in empty frame
 * @param header Indicate if header should be included in frame
 */
void bmi_emul_flush_fifo(struct i2c_emul *emul, bool tag_time, bool header);

/**
 * @brief Restore registers backed by NVM, reset sensor time and flush FIFO
 *
 * @param emul Pointer to BMI emulator
 */
void bmi_emul_reset_common(struct i2c_emul *emul, bool tag_time, bool header);

/**
 * @brief Set command end time to @p time ms from now
 *
 * @param emul Pointer to BMI emulator
 * @param time After this amount of ms command should end
 */
void bmi_emul_set_cmd_end_time(struct i2c_emul *emul, int time);

/**
 * @brief Check if command should end
 *
 * @param emul Pointer to BMI emulator
 */
bool bmi_emul_is_cmd_end(struct i2c_emul *emul);

/**
 * @brief Append FIFO @p frame to the emulator list of frames. It can be read
 *        using I2C interface.
 *
 * @param emul Pointer to BMI emulator
 * @param frame Pointer to new FIFO frame. Pointed data has to be valid while
 *              emulator may use this frame (until flush of FIFO or reading
 *              it out through I2C)
 */
void bmi_emul_append_frame(struct i2c_emul *emul, struct bmi_emul_frame *frame);

/**
 * @brief Get length of all frames that are on the emulator list of frames.
 *
 * @param emul Pointer to BMI emulator
 * @param tag_time Indicate if sensor time should be included in empty frame
 * @param header Indicate if header should be included in frame
 */
uint16_t bmi_emul_fifo_len(struct i2c_emul *emul, bool tag_time, bool header);

/**
 * @brief Get next byte that should be returned on FIFO data access.
 *
 * @param emul Pointer to BMI emulator
 * @param byte Which byte of block read command is currently handled
 * @param tag_time Indicate if sensor time should be included in empty frame
 * @param header Indicate if header should be included in frame
 * @param acc_shift How many bits should be right shifted from accelerometer
 *                  data
 * @param gyr_shift How many bits should be right shifted from gyroscope data
 *
 * @return FIFO data byte
 */
uint8_t bmi_emul_get_fifo_data(struct i2c_emul *emul, int byte,
			       bool tag_time, bool header, int acc_shift,
			       int gyr_shift);

/**
 * @brief Saves current internal state of sensors to emulator's registers.
 *
 * @param emul Pointer to BMI emulator
 * @param acc_shift How many bits should be right shifted from accelerometer
 *                  data
 * @param gyr_shift How many bits should be right shifted from gyroscope data
 * @param acc_reg Register which holds LSB of accelerometer sensor
 * @param gyr_reg Register which holds LSB of gyroscope sensor
 * @param sensortime_reg Register which holds LSB of sensor time
 * @param acc_off_en Indicate if accelerometer offset should be included to
 *                   sensor data value
 * @param gyr_off_en Indicate if gyroscope offset should be included to
 *                   sensor data value
 */
void bmi_emul_state_to_reg(struct i2c_emul *emul, int acc_shift,
			   int gyr_shift, int acc_reg, int gyr_reg,
			   int sensortime_reg, bool acc_off_en,
			   bool gyr_off_en);

/**
 * @}
 */

#endif /* __EMUL_BMI_H */
