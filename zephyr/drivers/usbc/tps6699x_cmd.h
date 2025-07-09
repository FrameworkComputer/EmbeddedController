/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_USBC_TPS6699X_H_
#define ZEPHYR_DRIVERS_USBC_TPS6699X_H_

#include "tps6699x_reg.h"

/**
 * @brief Read Vendor Id
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_vendor_id(const struct i2c_dt_spec *i2c, union reg_vendor_id *buf);

/**
 * @brief Read Device Id
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_device_id(const struct i2c_dt_spec *i2c, union reg_device_id *buf);

/**
 * @brief Read Protocol Version
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_protocol_version(const struct i2c_dt_spec *i2c,
			    union reg_protocol_version *buf);

/**
 * @brief Read Mode
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_mode(const struct i2c_dt_spec *i2c, union reg_mode *buf);

/**
 * @brief Read UID
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_uid(const struct i2c_dt_spec *i2c, union reg_uid *buf);

/**
 * @brief Read or Write Customer Use Register
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 * @param int flag set to I2C_MSG_READ for read and I2C_MSG_WRITE for write
 *
 * @return 0 on success, else -EIO
 */
int tps_rw_customer_use(const struct i2c_dt_spec *i2c,
			union reg_customer_use *buf, int flag);

/**
 * @brief Read or Write Command for I2C1
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 * @param int flag set to I2C_MSG_READ for read and I2C_MSG_WRITE for write
 *
 * @return 0 on success, else -EIO
 */
int tps_rw_command_for_i2c1(const struct i2c_dt_spec *i2c,
			    union reg_command *buf, int flag);

/**
 * @brief Read or Write Data for command 1
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 * @param int flag set to I2C_MSG_READ for read and I2C_MSG_WRITE for write
 *
 * @return 0 on success, else -EIO
 */
int tps_rw_data_for_cmd1(const struct i2c_dt_spec *i2c, union reg_data *buf,
			 int flag);

/**
 * @brief Read or Write Command for I2C2
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 * @param int flag set to I2C_MSG_READ for read and I2C_MSG_WRITE for write
 *
 * @return 0 on success, else -EIO
 */
int tps_rw_command_for_i2c2(const struct i2c_dt_spec *i2c,
			    union reg_command *buf, int flag);

/**
 * @brief Read or Write Data for command 2
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 * @param int flag set to I2C_MSG_READ for read and I2C_MSG_WRITE for write
 *
 * @return 0 on success, else -EIO
 */
int tps_rw_data_for_cmd2(const struct i2c_dt_spec *i2c, union reg_data *buf,
			 int flag);

/**
 * @brief Read Device Capabilities
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_device_capabilities(const struct i2c_dt_spec *i2c,
			       union reg_device_capabilities *buf);

/**
 * @brief Read Version
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_version(const struct i2c_dt_spec *i2c, union reg_version *buf);

/**
 * @brief Read Interrupt Event
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_interrupt_event(const struct i2c_dt_spec *i2c,
			   union reg_interrupt *buf);

/**
 * @brief Read or Write Interrupt Clear
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 * @param int flag set to I2C_MSG_READ for read and I2C_MSG_WRITE for write
 *
 * @return 0 on success, else -EIO
 */
int tps_rw_interrupt_clear(const struct i2c_dt_spec *i2c,
			   union reg_interrupt *buf, int flag);

/**
 * @brief Read or Write Interrupt Mask
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 * @param int flag set to I2C_MSG_READ for read and I2C_MSG_WRITE for write
 *
 * @return 0 on success, else -EIO
 */
int tps_rw_interrupt_mask(const struct i2c_dt_spec *i2c,
			  union reg_interrupt *buf, int flag);

/**
 * @brief Read Status
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_status(const struct i2c_dt_spec *i2c, union reg_status *buf);

/**
 * @brief Read Discovered Svids
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_discovered_svids(const struct i2c_dt_spec *i2c,
			    union reg_discovered_svids *buf);

/**
 * @brief Read Active RDO Contract
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_active_rdo_contract(const struct i2c_dt_spec *i2c,
			       union reg_active_rdo_contract *buf);

/**
 * @brief Read Active PDO Contract
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_active_pdo_contract(const struct i2c_dt_spec *i2c,
			       union reg_active_pdo_contract *buf);

/**
 * @brief Read or Write Port Configuration
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 * @param int flag set to I2C_MSG_READ for read and I2C_MSG_WRITE for write
 *
 * @return 0 on success, else -EIO
 */
int tps_rw_port_configuration(const struct i2c_dt_spec *i2c,
			      union reg_port_configuration *buf, int flag);

/**
 * @brief Read or Write Port Control
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 * @param int flag set to I2C_MSG_READ for read and I2C_MSG_WRITE for write
 *
 * @return 0 on success, else -EIO
 */
int tps_rw_port_control(const struct i2c_dt_spec *i2c,
			union reg_port_control *buf, int flag);

/**
 * @brief Read Boot Flags
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_boot_flags(const struct i2c_dt_spec *i2c, union reg_boot_flags *buf);

/**
 * @brief Read or Write Transmit Source Capabilities
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 * @param int flag set to I2C_MSG_READ for read and I2C_MSG_WRITE for write
 *
 * @return 0 on success, else -EIO
 */
int tps_rw_transmit_source_capabilities(
	const struct i2c_dt_spec *i2c,
	union reg_transmit_source_capabilities *buf, int flag);

/**
 * @brief Read or Write Transmit Sink Capabilities
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 * @param int flag set to I2C_MSG_READ for read and I2C_MSG_WRITE for write
 *
 * @return 0 on success, else -EIO
 */
int tps_rw_transmit_sink_capabilities(const struct i2c_dt_spec *i2c,
				      union reg_transmit_sink_capabilities *buf,
				      int flag);

/**
 * @brief Read PD Status
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_pd_status(const struct i2c_dt_spec *i2c, union reg_pd_status *buf);

/**
 * @brief Read Reeived Source Capabilities
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_received_source_capabilities(
	const struct i2c_dt_spec *i2c,
	union reg_received_source_capabilities *buf);

/**
 * @brief Read or Write Global System Configuration
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 * @param int flag set to I2C_MSG_READ for read and I2C_MSG_WRITE for write
 *
 * @return 0 on success, else -EIO
 */
int tps_rw_global_system_configuration(
	const struct i2c_dt_spec *i2c,
	union reg_global_system_configuration *buf, int flag);

/**
 * @brief Read or Write Autonegotiate Sink
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 * @param int flag set to I2C_MSG_READ for read and I2C_MSG_WRITE for write
 *
 * @return 0 on success, else -EIO
 */
int tps_rw_autonegotiate_sink(const struct i2c_dt_spec *i2c,
			      union reg_autonegotiate_sink *buf, int flag);

/**
 * @brief Read ADC Results
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_adc_results(const struct i2c_dt_spec *i2c,
		       union reg_adc_results *buf);

/**
 * @brief Read Power Path status
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_power_path_status(const struct i2c_dt_spec *i2c,
			     union reg_power_path_status *buf);

/**
 * @brief Read or Write TX Identity Register
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 * @param int flag set to I2C_MSG_READ for read and I2C_MSG_WRITE for write
 *
 * @return 0 on success, else -EIO
 */
int tps_rw_tx_identity(const struct i2c_dt_spec *i2c,
		       union reg_tx_identity *buf, int flag);

/**
 * @brief Read Received SOP Identity data object
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_received_sop_identity_data_object(
	const struct i2c_dt_spec *i2c,
	union reg_received_identity_data_object *buf);

/**
 * @brief Read Received SOP Prime Identity data object
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_received_sop_prime_identity_data_object(
	const struct i2c_dt_spec *i2c,
	union reg_received_identity_data_object *buf);

/**
 * @brief Read or Write Connection Manager Control
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 * @param int flag set to I2C_MSG_READ for read and I2C_MSG_WRITE for write
 *
 * @return 0 on success, else -EIO
 */
int tps_rw_connection_manager_control(const struct i2c_dt_spec *i2c,
				      union reg_connection_manager_control *buf,
				      int flag);

/**
 * @brief Read Connection Manager Status
 *
 * @param i2c device pointer to i2c device
 * @param buf pointer where data is stored
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_connection_manager_status(const struct i2c_dt_spec *i2c,
				     union reg_connection_manager_status *buf);

/**
 * @brief Read the data status register (0x5F)
 *
 * @param i2c device pointer to i2c device
 * @param status Output location for contents
 *
 * @return 0 on success, else -EIO
 */
int tps_rd_data_status_reg(const struct i2c_dt_spec *i2c,
			   union reg_data_status *status);

/**
 * @brief Perform bulk transfers to the PDC
 *
 * @param i2c device pointer to i2c device
 * @param broadcast_address I2C address to stream data to
 * @param buf Data payload to transmit
 * @param buf_len Number of bytes from `buf` to transmit
 *
 * @return 0 on success, or negative error code
 */
int tps_stream_data(const struct i2c_dt_spec *i2c,
		    const uint8_t broadcast_address, const uint8_t *buf,
		    size_t buf_len);

#endif /* ZEPHYR_DRIVERS_USBC_TPS6699X_H_ */
