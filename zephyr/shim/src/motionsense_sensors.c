/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "common.h"
#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motionsense_sensors.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(shim_cros_motionsense_sensors);

#define SENSOR_MUTEX_NODE DT_PATH(motionsense_mutex)
#define SENSOR_MUTEX_NAME(id) DT_CAT(MUTEX_, id)

#if DT_NODE_EXISTS(SENSOR_MUTEX_NODE)
#define DECLARE_SENSOR_MUTEX(id) K_MUTEX_DEFINE(SENSOR_MUTEX_NAME(id));

/*
 * Declare mutex for
 * each child node of "/motionsense-mutex" node in DT.
 *
 * A mutex can be shared among the motion sensors.
 */
DT_FOREACH_CHILD(SENSOR_MUTEX_NODE, DECLARE_SENSOR_MUTEX)
#endif /* DT_NODE_EXISTS(SENSOR_MUTEX_NODE) */

#define MAT_ITEM(i, id) FLOAT_TO_FP((int32_t)(DT_PROP_BY_IDX(id, mat33, i)))
#define DECLARE_SENSOR_ROT_REF(id)                                   \
	const mat33_fp_t SENSOR_ROT_STD_REF_NAME(id) = {             \
		{ FOR_EACH_FIXED_ARG(MAT_ITEM, (, ), id, 0, 1, 2) }, \
		{ FOR_EACH_FIXED_ARG(MAT_ITEM, (, ), id, 3, 4, 5) }, \
		{ FOR_EACH_FIXED_ARG(MAT_ITEM, (, ), id, 6, 7, 8) }, \
	};

/*
 * Declare 3x3 rotation matrix for
 * each child node of "/motionsense-rotation-ref" node in DT.
 *
 * A rotation matrix can be shared among the motion sensors.
 */
#if DT_NODE_EXISTS(SENSOR_ROT_REF_NODE)
DT_FOREACH_CHILD(SENSOR_ROT_REF_NODE, DECLARE_SENSOR_ROT_REF)
#endif

#define SENSOR_BODYDETECT_NODE DT_PATH(motionsense_bodydetect)
#define SENSOR_BODYDETECT_NAME(id) DT_CAT(BODYDETECT_, id)

#define DECLARE_BODYDETECT_CONFIG(id)                                    \
	const struct body_detect_params SENSOR_BODYDETECT_NAME(          \
		DT_PARENT(id)) = {                                       \
		.var_noise_factor = DT_PROP_OR(id, var_noise_factor, 0), \
		.var_threshold = DT_PROP_OR(id, var_threshold, 0),       \
		.confidence_delta = DT_PROP_OR(id, confidence_delta, 0), \
	};

/*
 * Declare body detection parameters structure for each node with
 * status = "okay" and compatible = "cros-ec,motionsense-bodydetect".
 */
DT_FOREACH_STATUS_OKAY(cros_ec_motionsense_bodydetect,
		       DECLARE_BODYDETECT_CONFIG)

/*
 * Declare sensor driver data for
 * each child node with status = "okay" of
 * "/motionsense-sensor-data" node in DT.
 *
 * A driver data can be shared among the motion sensors.
 */
#define SENSOR_DATA_NAME(id) DT_CAT(SENSOR_DAT_, id)
#define SENSOR_DATA_NODE DT_PATH(motionsense_sensor_data)

#define SENSOR_DATA(inst, compat, create_data_macro) \
	create_data_macro(DT_INST(inst, compat),     \
			  SENSOR_DATA_NAME(DT_INST(inst, compat)))

/*
 * CREATE_SENSOR_DATA is a helper macro that gets
 * compat and create_data_macro as parameters.
 *
 * For each node with compatible = "compat",
 * CREATE_SENSOR_DATA expands "create_data_macro" macro with the node id and
 * the designated name for the sensor driver data to be created. The
 * "create_datda_macro" macro is responsible for creating the sensor driver
 * data with the name.
 *
 * Sensor drivers should provide <chip>-drvinfo.inc file and, in the file,
 * it should have the macro that creates its sensor driver data using device
 * tree and pass the macro via CREATE_SENSOR_DATA.
 *
 * e.g) The below is contents of tcs3400-drvinfo.inc file. The file has
 * CREATE_SENSOR_DATA_TCS3400_CLEAR that creates the static instance of
 * "struct als_drv_data_t" with the given name and initializes it
 * with device tree. Then use CREATE_SENSOR_DATA.
 *
 * ----------- bma255-drvinfo.inc -----------
 * #define CREATE_SENSOR_DATA_TCS3400_CLEAR(id, drvdata_name)      \
 *       static struct als_drv_data_t drvdata_name =               \
 *           ACCELGYRO_ALS_DRV_DATA(DT_CHILD(id, als_drv_data));
 *
 * CREATE_SENSOR_DATA(cros_ec_drvdata_tcs3400_clear,   \
 *                    CREATE_SENSOR_DATA_TCS3400_CLEAR)
 */
#define CREATE_SENSOR_DATA(compat, create_data_macro)                     \
	LISTIFY(DT_NUM_INST_STATUS_OKAY(compat), SENSOR_DATA, (), compat, \
		create_data_macro)

/*
 * sensor_drv_list.inc is included three times in this file. This is the first
 * time and it is for creating sensor driver-specific data. So we ignore
 * CREATE_MOTION_SENSOR() that creates motion sensor at this time.
 */
#define CREATE_MOTION_SENSOR(s_compat, s_chip, s_type, s_drv, s_min_freq, \
			     s_max_freq)

/*
 * Here, we declare all sensor driver data. How to create the data is
 * defined in <chip>-drvinfo.inc file and ,in turn, the file is included
 * in sensor_drv_list.inc.
 */
#if DT_NODE_EXISTS(SENSOR_DATA_NODE)
#include "motionsense_driver/sensor_drv_list.inc"
#endif

/*
 * Get the address of the mutex which is referred by phandle.
 * See motionsense-sensor-base.yaml and cros-ec,motionsense-mutex.yaml
 * for DT example and details.
 */
#define SENSOR_MUTEX(id)                        \
	IF_ENABLED(DT_NODE_HAS_PROP(id, mutex), \
		   (.mutex = &SENSOR_MUTEX_NAME(DT_PHANDLE(id, mutex)), ))

/*
 * Set the interrupt pin which is referred by the phandle.
 */
#define SENSOR_INT_SIGNAL(id)                        \
	IF_ENABLED(DT_NODE_HAS_PROP(id, int_signal), \
		   (.int_signal = GPIO_SIGNAL(DT_PHANDLE(id, int_signal)), ))

/*
 * Set flags based on values defined in the node.
 */
#define SENSOR_FLAGS(id)                                        \
	.flags = 0 IF_ENABLED(DT_NODE_HAS_PROP(id, int_signal), \
			      (| MOTIONSENSE_FLAG_INT_SIGNAL)),

/*
 * Get I2C port number which is referred by phandle.
 * See motionsense-sensor-base.yaml for DT example and details.
 */
#define SENSOR_I2C_PORT(id)                    \
	IF_ENABLED(DT_NODE_HAS_PROP(id, port), \
		   (.port = I2C_PORT(DT_PHANDLE(id, port)), ))

/*
 * Get I2C or SPI address.
 * See motionsense-sensor-base.yaml for DT example and details.
 */
#define SENSOR_I2C_SPI_ADDR_FLAGS(id)                        \
	IF_ENABLED(DT_NODE_HAS_PROP(id, i2c_spi_addr_flags), \
		   (.i2c_spi_addr_flags =                    \
			    DT_STRING_TOKEN(id, i2c_spi_addr_flags), ))

/*
 * Get the address of rotation matrix which is referred by phandle.
 * See motionsense-sensor-base.yaml and cros-ec,motionsense-rotation-ref.yaml
 * for DT example and details.
 */
#define SENSOR_ROT_STD_REF(id)                                    \
	IF_ENABLED(DT_NODE_HAS_PROP(id, rot_standard_ref),        \
		   (.rot_standard_ref = &SENSOR_ROT_STD_REF_NAME( \
			    DT_PHANDLE(id, rot_standard_ref)), ))

/*
 * Get the address of driver-specific data which is referred by phandle.
 * See motionsense-sensor-base.yaml for DT example and details.
 */
#define SENSOR_DRV_DATA(id)                        \
	IF_ENABLED(DT_NODE_HAS_PROP(id, drv_data), \
		   (.drv_data =                    \
			    &SENSOR_DATA_NAME(DT_PHANDLE(id, drv_data)), ))

/*
 * Get the address of body detect specific data.
 * See motionsense-sensor-base.yaml for DT example and details.
 */
#define SENSOR_BODYDETECT(id)                                \
	IF_ENABLED(DT_NODE_EXISTS(DT_CHILD(id, bodydetect)), \
		   (.bd_params = &SENSOR_BODYDETECT_NAME(id), ))

/*
 * Get odr and ec_rate for the motion sensor.
 * See motionsense-sensor-base.yaml and cros-ec,motionsense-sensor-config.yaml
 * for DT example and details.
 */
#define SET_CONFIG_EC(cfg_id, cfg_suffix)                  \
	[SENSOR_CONFIG_##cfg_suffix] = {                   \
		.odr = DT_PROP_OR(cfg_id, odr, 0),         \
		.ec_rate = DT_PROP_OR(cfg_id, ec_rate, 0), \
	}

/* Get configs */
#define CREATE_SENSOR_CONFIG(cfgs_id)                                         \
	.config = {                                                           \
		IF_ENABLED(DT_NODE_EXISTS(DT_CHILD(cfgs_id, ap)),             \
			   (SET_CONFIG_EC(DT_CHILD(cfgs_id, ap), AP), ))      \
			IF_ENABLED(DT_NODE_EXISTS(DT_CHILD(cfgs_id, ec_s0)),  \
				   (SET_CONFIG_EC(DT_CHILD(cfgs_id, ec_s0),   \
						  EC_S0), ))                  \
				IF_ENABLED(DT_NODE_EXISTS(                    \
						   DT_CHILD(cfgs_id, ec_s3)), \
					   (SET_CONFIG_EC(DT_CHILD(cfgs_id,   \
								   ec_s3),    \
							  EC_S3), ))          \
					IF_ENABLED(DT_NODE_EXISTS(DT_CHILD(   \
							   cfgs_id, ec_s5)),  \
						   (SET_CONFIG_EC(            \
							    DT_CHILD(cfgs_id, \
								     ec_s5),  \
							    EC_S5), ))        \
	}

#define SENSOR_CONFIG(id)                                 \
	IF_ENABLED(DT_NODE_EXISTS(DT_CHILD(id, configs)), \
		   (CREATE_SENSOR_CONFIG(DT_CHILD(id, configs)), ))

/* Get and assign the basic information for a motion sensor */
#define SENSOR_BASIC_INFO(id)                                                \
	.name = DT_NODE_FULL_NAME(id),                                       \
	.active_mask = DT_STRING_TOKEN(id, active_mask),                     \
	.location = DT_STRING_TOKEN(id, location),                           \
	.default_range = (int)(DT_PROP(id, default_range)),                  \
	SENSOR_I2C_SPI_ADDR_FLAGS(id) SENSOR_MUTEX(id) SENSOR_I2C_PORT(id)   \
		SENSOR_ROT_STD_REF(id) SENSOR_DRV_DATA(id) SENSOR_CONFIG(id) \
			SENSOR_INT_SIGNAL(id) SENSOR_FLAGS(id)               \
				SENSOR_BODYDETECT(id)

/* Create motion sensor node with node ID */
#define DO_MK_SENSOR_ENTRY(id, s_chip, s_type, s_drv, s_min_freq, s_max_freq) \
	[SENSOR_ID(id)] = { SENSOR_BASIC_INFO(id).chip = s_chip,              \
			    .type = s_type, .drv = &s_drv,                    \
			    .min_frequency = s_min_freq,                      \
			    .max_frequency = s_max_freq },

/* Construct an entry iff the alternate_for property is missing. */
#define MK_SENSOR_ENTRY(inst, s_compat, s_chip, s_type, s_drv, s_min_freq,    \
			s_max_freq)                                           \
	COND_CODE_0(DT_NODE_HAS_PROP(DT_INST(inst, s_compat), alternate_for), \
		    (DO_MK_SENSOR_ENTRY(DT_INST(inst, s_compat), s_chip,      \
					s_type, s_drv, s_min_freq,            \
					s_max_freq)),                         \
		    ())

/* Construct an entry iff the alternate_for property exists. */
#define MK_SENSOR_ALT_ENTRY(inst, s_compat, s_chip, s_type, s_drv, s_min_freq, \
			    s_max_freq)                                        \
	COND_CODE_1(DT_NODE_HAS_PROP(DT_INST(inst, s_compat), alternate_for),  \
		    (DO_MK_SENSOR_ENTRY(DT_INST(inst, s_compat), s_chip,       \
					s_type, s_drv, s_min_freq,             \
					s_max_freq)),                          \
		    ())

#undef CREATE_SENSOR_DATA
/*
 * Sensor driver-specific data creation stage is already done. So this
 * time we ignore CREATE_SENSOR_DATA().
 */
#define CREATE_SENSOR_DATA(compat, create_data_macro)
#undef CREATE_MOTION_SENSOR

/*
 * CREATE_MOTION_SENSOR is a help macro that read the sensor information from
 * device tree and creates an entry in motion_sensors array which is used
 * by motion sense task. The help macro gets compatible value of the
 * sensor node and several driver specific information like CHIP_ID,
 * SENSOR_TYPE, driver instance name, and min/max frequency.
 *
 * <chip>-drvinfo.inc file which is provided by sensor driver should use
 * CREATE_MOTION_SENSOR to provide driver specific information.
 *
 * e.g) The below is contents of tcs3400-drvinfo.inc file. The file has
 * CREATE_MOTION_SENSOR like below to create the sensor entry. The file uses
 * the help macro two times since the chip supports two functions
 * ALS clear and ALS RGB.

 * ------------- tcs3400-drvinfo.inc -------------
 * // Here, we call CREATE_MOTION_SENSOR to create a motion_sensor_t entry
 * // for each TCS3400 clear instance (compatible = "cros-ec,tcs3400-clear")
 * // in device tree.
 * CREATE_MOTION_SENSOR(cros_ec_tcs3400_clear, MOTIONSENSE_CHIP_TCS3400,   \
 *       MOTIONSENSE_TYPE_LIGHT, tcs3400_drv,            \
 *       TCS3400_LIGHT_MIN_FREQ, TCS3400_LIGHT_MAX_FREQ)

 *
 * // Here, we call CREATE_MOTION_SENSOR to create a motion_sensor_t entry
 * // for each TCS3400 RGB instance (compatible = "cros-ec,tcs3400-rgb")
 * // in device tree.
 *
 * CREATE_MOTION_SENSOR(cros_ec_tcs3400_rgb, MOTIONSENSE_CHIP_TCS3400, \
 *       MOTIONSENSE_TYPE_LIGHT_RGB, tcs3400_rgb_drv, 0, 0)
 * -----------------------------------------------
 */
#define CREATE_MOTION_SENSOR(s_compat, s_chip, s_type, s_drv, s_min_freq, \
			     s_max_freq)                                  \
	LISTIFY(DT_NUM_INST_STATUS_OKAY(s_compat), MK_SENSOR_ENTRY, (),   \
		s_compat, s_chip, s_type, s_drv, s_min_freq, s_max_freq)

/*
 * Here, we include sensor_drv_list.inc AGAIN but this time it only
 * uses CREATE_MOTION_SENSOR to create the motion sensor entries.
 */
struct motion_sensor_t motion_sensors[] = {
#if DT_NODE_EXISTS(SENSOR_NODE)
#include "motionsense_driver/sensor_drv_list.inc"
#endif
};

/*
 * Remap the CREATE_MOTION_SENSOR to call MK_SENSOR_ALT_ENTRY to create a list
 * of alternate sensors that will be used at runtime.
 */
#undef CREATE_MOTION_SENSOR
#define CREATE_MOTION_SENSOR(s_compat, s_chip, s_type, s_drv, s_min_freq,   \
			     s_max_freq)                                    \
	LISTIFY(DT_NUM_INST_STATUS_OKAY(s_compat), MK_SENSOR_ALT_ENTRY, (), \
		s_compat, s_chip, s_type, s_drv, s_min_freq, s_max_freq)

/*
 * The list of alternate motion sensors that may be used at runtime to replace
 * an entry in the motion_sensors array.
 */
__maybe_unused struct motion_sensor_t motion_sensors_alt[] = {
#if DT_NODE_EXISTS(SENSOR_ALT_NODE)
#include "motionsense_driver/sensor_drv_list.inc"
#endif
};

#ifdef CONFIG_DYNAMIC_MOTION_SENSOR_COUNT
unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);
#else
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);
#endif

/*
 * Create a list of ALS sensors needed by motion sense
 *
 * The following example adds tcs3400 als sensor to motion_als_sensors array
 *
 * motionsense-sensors {
 *         lid_accel: bma255 {
 *             :
 *         };
 *             :
 *             :
 *         als_clear: tcs3400 {
 *             :
 *         };
 * };
 *
 * motionsense-sensor-info {
 *       compatible = "cros-ec,motionsense-sensor-info";
 *
 *       // list of entries for motion_als_sensors
 *       als-sensors = <&als_clear>;
 *                  :
 *                  :
 * };
 */
#if DT_NODE_HAS_PROP(SENSOR_INFO_NODE, als_sensors)
#define ALS_SENSOR_ENTRY_WITH_COMMA(i, id) \
	&motion_sensors[SENSOR_ID(DT_PHANDLE_BY_IDX(id, als_sensors, i))],
const struct motion_sensor_t *motion_als_sensors[] = { LISTIFY(
	DT_PROP_LEN(SENSOR_INFO_NODE, als_sensors), ALS_SENSOR_ENTRY_WITH_COMMA,
	(), SENSOR_INFO_NODE) };
BUILD_ASSERT(ARRAY_SIZE(motion_als_sensors) == ALS_COUNT);
#endif

/*
 * Enable interrupts for motion sensors
 *
 * e.g) list of named-gpio nodes
 * motionsense-sensor-info {
 *        compatible = "cros-ec,motionsense-sensor-info";
 *
 *         // list of GPIO interrupts that have to
 *         // be enabled at initial stage
 *        sensor-irqs = <&int_imu &int_als_rgb>;
 * };
 */
#if DT_NODE_HAS_PROP(SENSOR_INFO_NODE, sensor_irqs)
#define SENSOR_GPIO_ENABLE_INTERRUPT(i, id) \
	gpio_enable_dt_interrupt(           \
		GPIO_INT_FROM_NODE(DT_PHANDLE_BY_IDX(id, sensor_irqs, i)));
static void sensor_enable_irqs(void){
	LISTIFY(DT_PROP_LEN(SENSOR_INFO_NODE, sensor_irqs),
		SENSOR_GPIO_ENABLE_INTERRUPT, (), SENSOR_INFO_NODE)
} DECLARE_HOOK(HOOK_INIT, sensor_enable_irqs, HOOK_PRIO_DEFAULT);
#endif

/* Handle the alternative motion sensors */
#define CHECK_SSFC_AND_ENABLE_ALT_SENSOR(id)                                 \
	do {                                                                 \
		if (cros_cbi_ssfc_check_match(CBI_SSFC_VALUE_ID(             \
			    DT_PHANDLE(id, alternate_ssfc_indicator)))) {    \
			LOG_INF("Replacing \"%s\" for \"%s\" based on SSFC", \
				motion_sensors[SENSOR_ID(DT_PHANDLE(         \
						       id, alternate_for))]  \
					.name,                               \
				motion_sensors_alt[SENSOR_ID(id)].name);     \
			ENABLE_ALT_MOTION_SENSOR(id);                        \
		}                                                            \
	} while (0)

#define ALT_SENSOR_CHECK_SSFC_ID(id)                                          \
	COND_CODE_1(UTIL_AND(DT_NODE_HAS_PROP(id, alternate_for),             \
			     DT_NODE_HAS_PROP(id, alternate_ssfc_indicator)), \
		    (CHECK_SSFC_AND_ENABLE_ALT_SENSOR(id);), ())

#if DT_NODE_EXISTS(SENSOR_ALT_NODE)

int motion_sense_probe(enum sensor_alt_id alt_idx)
{
	int res;

	LOG_INF("Probing \"%s\" chip %d type %d loc %d",
		motion_sensors_alt[alt_idx].name,
		motion_sensors_alt[alt_idx].chip,
		motion_sensors_alt[alt_idx].type,
		motion_sensors_alt[alt_idx].location);

	__ASSERT(motion_sensors_alt[alt_idx].drv->probe != NULL,
		 "No probing function for alt sensor: %d", alt_idx);
	res = motion_sensors_alt[alt_idx].drv->probe(
		&motion_sensors_alt[alt_idx]);
	LOG_INF("%sfound\n", (res != EC_SUCCESS ? "not " : ""));

	return res;
}

void motion_sensors_check_ssfc(void){
	DT_FOREACH_CHILD(SENSOR_ALT_NODE, ALT_SENSOR_CHECK_SSFC_ID)
}
#endif /* DT_NODE_EXISTS(SENSOR_ALT_NODE) */

#define DEF_MOTION_ISR_NAME_ENUM(id) \
	DT_STRING_UPPER_TOKEN(DT_PHANDLE(id, int_signal), enum_name)
#define DEF_MOTION_ISR_NAME_ENUM_WITH_SUFFIX(name) DT_CAT(name, _ISR)
#define DEF_MOTION_ISR_NAME(id) \
	DEF_MOTION_ISR_NAME_ENUM_WITH_SUFFIX(DEF_MOTION_ISR_NAME_ENUM(id))

#define DEF_MOTION_ISR(id)                                               \
	void DEF_MOTION_ISR_NAME(id)(enum gpio_signal signal)            \
	{                                                                \
		__ASSERT(motion_sensors[SENSOR_ID(id)].drv->interrupt,   \
			 "No interrupt handler for signal: %x", signal); \
		motion_sensors[SENSOR_ID(id)].drv->interrupt(signal);    \
	}

#define DEF_MOTION_CHECK_ISR(id) \
	COND_CODE_1(DT_NODE_HAS_PROP(id, int_signal), (DEF_MOTION_ISR(id)), ())

#if DT_NODE_EXISTS(SENSOR_NODE)
DT_FOREACH_CHILD(SENSOR_NODE, DEF_MOTION_CHECK_ISR)
#endif
