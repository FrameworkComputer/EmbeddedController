/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_SHIM_INCLUDE_TEMP_SENSOR_TEMP_SENSOR_H_
#define ZEPHYR_SHIM_INCLUDE_TEMP_SENSOR_TEMP_SENSOR_H_

#include "charger/chg_rt9490.h"
#include "temp_sensor.h"

#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_PLATFORM_EC_TEMP_SENSOR

#define PCT2075_COMPAT nxp_pct2075
#define TMP112_COMPAT cros_ec_temp_sensor_tmp112
#define F75303_COMPAT cros_ec_temp_sensor_f75303
#define SB_TSI_COMPAT cros_ec_temp_sensor_sb_tsi
#define THERMISTOR_COMPAT cros_ec_temp_sensor_thermistor
#define TEMP_SENSORS_COMPAT cros_ec_temp_sensors

#define TEMP_SENSORS_NODEID DT_INST(0, TEMP_SENSORS_COMPAT)

#define TEMP_RT9490_FN(node_id, fn) \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, thermistor), (fn(node_id)), ())

#define FOREACH_TEMP_SENSOR(fn)                                             \
	DT_FOREACH_STATUS_OKAY(PCT2075_COMPAT, fn)                          \
	DT_FOREACH_STATUS_OKAY(TMP112_COMPAT, fn)                           \
	DT_FOREACH_STATUS_OKAY(F75303_COMPAT, fn)                           \
	DT_FOREACH_STATUS_OKAY_VARGS(RT9490_CHG_COMPAT, TEMP_RT9490_FN, fn) \
	DT_FOREACH_STATUS_OKAY(SB_TSI_COMPAT, fn)                           \
	DT_FOREACH_STATUS_OKAY(THERMISTOR_COMPAT, fn)

#define HAS_POWER_GOOD_PIN(node_id) DT_NODE_HAS_PROP(node_id, power_good_pin) ||

#define ANY_INST_HAS_POWER_GOOD_PIN \
	(DT_FOREACH_CHILD(TEMP_SENSORS_NODEID, HAS_POWER_GOOD_PIN) 0)

/*
 * Get the enum temp_sensor_id value from a child node under
 * "cros-ec,temp-sensors".
 *
 * Example devicetree fragment:
 *
 *     temp_charger_thermistor: charger-thermistor {
 *         compatible = "cros-ec,temp-sensor-thermistor";
 *         thermistor = <&thermistor_3V3_30K9_47K_4050B>;
 *         adc = <&adc_temp_charger>;
 *     };
 *
 *     named-temp-sensors {
 *         compatible = "cros-ec,temp-sensors";
 *         temp_charger: charger-thermistor {
 *             temp_host_high = <100>;
 *             temp_host_halt = <105>;
 *             temp_host_release_high = <80>;
 *             sensor = <&temp_charger_thermistor>;
 *         };
 *     };
 *
 * Example usage to get the temperature sensor ID:
 *
 *     TEMP_SENSOR_ID(DT_NODELABEL(temp_charger))
 *
 * @param node_id: node id of a child of "cros-ec,temp-sensors" node
 */
#define TEMP_SENSOR_ID(node_id) DT_CAT(TEMP_SENSOR_, node_id)

/*
 * Get the enum temp_sensor_id value from a hardware device node.
 *
 * Example devicetree fragment:
 *
 *     temp_charger_thermistor: charger-thermistor {
 *         compatible = "cros-ec,temp-sensor-thermistor";
 *         thermistor = <&thermistor_3V3_30K9_47K_4050B>;
 *         adc = <&adc_temp_charger>;
 *     };
 *
 *     named-temp-sensors {
 *         compatible = "cros-ec,temp-sensors";
 *         temp_charger: charger-thermistor {
 *             temp_host_high = <100>;
 *             temp_host_halt = <105>;
 *             temp_host_release_high = <80>;
 *             sensor = <&temp_charger_thermistor>;
 *         };
 *     };
 *
 * Example usage to get the temperature sensor ID:
 *
 *     TEMP_SENSOR_ID_BY_DEV(DT_NODELABEL(temp_charger_thermistor))
 *
 * which equals:
 *
 *     TEMP_SENSOR_ID(DT_NODELABEL(temp_charger))
 *
 * @param node_id: node id of a hardware device node
 */
#define TEMP_SENSOR_ID_BY_DEV(node_id) DT_CAT(TEMP_SENSOR_DEV, node_id)

#define TEMP_SENSOR_ID_DEV(named_id)                          \
	TEMP_SENSOR_ID_BY_DEV(DT_PHANDLE(named_id, sensor)) = \
		TEMP_SENSOR_ID(named_id)

enum temp_sensor_id {
	DT_FOREACH_CHILD_SEP(TEMP_SENSORS_NODEID, TEMP_SENSOR_ID, (, )),
	DT_FOREACH_CHILD_SEP(TEMP_SENSORS_NODEID, TEMP_SENSOR_ID_DEV, (, )),
	TEMP_SENSOR_COUNT,
};

/* PCT2075 access array */
/*
 * Get the PCT2075 sensor ID from a hardware device node.
 *
 * @param node_id: node id of a hardware PCT2075 sensor node
 */
#define PCT2075_SENSOR_ID(node_id) DT_CAT(PCT2075_, node_id)
#define PCT2075_SENSOR_ID_WITH_COMMA(node_id) PCT2075_SENSOR_ID(node_id),

/* clang-format off */
enum pct2075_sensor {
	DT_FOREACH_STATUS_OKAY(PCT2075_COMPAT, PCT2075_SENSOR_ID_WITH_COMMA)
	PCT2075_COUNT,
};
/* clang-format on */

#undef PCT2075_SENSOR_ID_WITH_COMMA

/* TMP112 access array */
/*
 * Get the TMP112 sensor ID from a hardware device node.
 *
 * @param node_id: node id of a hardware TMP112 sensor node
 */
#define TMP112_SENSOR_ID(node_id) DT_CAT(TMP112_, node_id)
#define TMP112_SENSOR_ID_WITH_COMMA(node_id) TMP112_SENSOR_ID(node_id),

/* clang-format off */
enum tmp112_sensor {
	DT_FOREACH_STATUS_OKAY(TMP112_COMPAT, TMP112_SENSOR_ID_WITH_COMMA)
	TMP112_COUNT,
};
/* clang-format on */

#undef TMP112_SENSOR_ID_WITH_COMMA

/* F75303 access array */
/*
 * Get the F75303 sensor ID.
 *
 * The F75303 driver only supports a single device instance on the board. Each
 * device supports 3 temperature sensor types: local, remote1, and remote2.
 * Use the temperature sensor type as the sensor ID.
 *
 * @param node_id: node id of a hardware F75303 sensor node
 */
#define F75303_SENSOR_ID(node_id) DT_STRING_TOKEN(node_id, temperature_type)

struct zephyr_temp_sensor {
	/* Read sensor value in K into temp_ptr; return non-zero if error. */
	int (*read)(const struct temp_sensor_t *sensor, int *temp_ptr);
	void (*update_temperature)(int idx);
	const struct thermistor_info *thermistor;
#if ANY_INST_HAS_POWER_GOOD_PIN
	const struct device *power_good_dev;
	gpio_pin_t power_good_pin;
#endif /* ANY_INST_HAS_POWER_GOOD_PIN */
};

#endif /* CONFIG_PLATFORM_EC_TEMP_SENSOR */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SHIM_INCLUDE_TEMP_SENSOR_TEMP_SENSOR_H_ */
