/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_GPU_CONFIGURATION_H__
#define __CROS_EC_GPU_CONFIGURATION_H__

#include "common.h"
#include "config.h"

/*
 * The Framework bay descriptor consists of a header and a series of blocks
 * after the header that describe the function of the card.
 *
 * Each block starts with a block header which is 4 bytes, and then block
 * data.
 *
 * -------------
 *  HEADER
 * -------------
 * BLOCK1 Header
 * -------------
 * BLOCK1 Data
 * -------------
 * BLOCK2 Header
 * -------------
 * Block2 Data
 * -------------
 */

#define GPU_MAX_BLOCK_LEN (256)
#define GPU_SERIAL_LEN 20

struct gpu_cfg_descriptor {
	/* Expansion bay card magic value that is unique */
	char magic[4];
	/* Length of header following this field */
	uint32_t length;
	/* descriptor version, if EC max version is lower than this, ec cannot parse */
	uint16_t descriptor_version_major;
	uint16_t descriptor_version_minor;
	/* Hardware major version */
	uint16_t hardware_version;
	/* Hardware minor revision */
	uint16_t hardware_revision;
	/* 18 digit Framework Serial that starts with FRA
	 * the first 10 digits must be allocated by framework
	 */
	char serial[20];
	/* Length of descriptor following header*/
	uint32_t descriptor_length;
	/* CRC of descriptor */
	uint32_t descriptor_crc32;
	/* CRC of header before this value */
	uint32_t crc32;
} __packed;

struct gpu_config_header {
	union {
		struct gpu_cfg_descriptor header;
		uint8_t bytes[0x2B];
	};
} __packed;

struct gpu_block_header {
	uint8_t block_type;
	uint8_t block_length;
} __packed;
BUILD_ASSERT(sizeof(struct gpu_block_header) == 2);

enum gpucfg_type {
	GPUCFG_TYPE_UNINITIALIZED = 0,
	GPUCFG_TYPE_GPIO = 1,
	GPUCFG_TYPE_THERMAL_SENSOR = 2,
	GPUCFG_TYPE_FAN = 3,
	GPUCFG_TYPE_POWER = 4,
	GPUCFG_TYPE_BATTERY = 5,
	GPUCFG_TYPE_PCIE = 6,
	GPUCFG_TYPE_DPMUX = 7,
	GPUCFG_TYPE_POWEREN = 8,
	GPUCFG_TYPE_SUBSYS = 9,
	GPUCFG_TYPE_VENDOR = 10,
	GPUCFG_TYPE_PD = 11,
	GPUCFG_TYPE_GPUPWR = 12,
	GPUCFG_TYPE_CUSTOM_TEMP = 13,
	GPUCFG_TYPE_MAX = UINT8_MAX, /**< Force enum to be 8 bits */
} __packed;
BUILD_ASSERT(sizeof(enum gpucfg_type) == sizeof(uint8_t));

enum gpu_gpio_idx {
	GPU_GPIO_INVALID = 0,
	GPU_1G1_GPIO0_EC,
	GPU_1H1_GPIO1_EC,
	GPU_2A2_GPIO2_EC,
	GPU_2L7_GPIO3_EC,
	GPU_2L5_TH_OVERTn, /* cannot be controlled directly */
	GPU_1F2_I2C_S5_INT,
	GPU_1L1_DGPU_PWROK, /* connected to APU */
	GPU_1C3_ALW_CLK, /* ALW I2C CLOCK PIN to EC */
	GPU_1D3_ALW_DAT, /* ALW I2C DATA PIN to EC */
	GPU_1F3_MUX1, /* cannot be controlled directly */
	GPU_1G3_MUX2, /* cannot be controlled directly */
	GPU_2B5_ALERTn,
	GPU_EDP_MUX_SEL, /* Select EDP MUX */
	GPU_ECPWM_EN, /**/
	GPU_PCIE_MUX_SEL, /* select between EDP AUX or SSD PCIE2 CLK*/
	GPU_VSYS_EN,
	GPU_VADP_EN,
	GPU_FAN_EN,
	GPU_3V_5V_EN,
	GPU_GPIO_MAX
} __packed;

enum gpu_gpio_purpose {
	GPIO_FUNC_UNUSED,
	GPIO_FUNC_HIGH, /* set high */
	GPIO_FUNC_TEMPFAULT,
	GPIO_FUNC_ACDC,
	GPIO_FUNC_HPD,
	GPIO_FUNC_PD_INT,
	GPIO_FUNC_SSD1_POWER,
	GPIO_FUNC_SSD2_POWER,
	GPIO_FUNC_EC_PWM_EN,
	GPIO_FUNC_EDP_MUX_SEL,
	GPIO_FUNC_VSYS_EN,
	GPIO_FUNC_VADP_EN,
	GPIO_FUNC_GPU_PWR,
	GPIO_FUNC_MAX,
};

enum power_request_source_t {
	POWER_REQ_INIT,
	POWER_REQ_POWER_ON,
	POWER_REQ_GPU_3V_5V,
	POWER_REQ_COUNT,
};

struct gpu_cfg_gpio {
	uint8_t gpio;
	uint8_t function;
	uint32_t flags;
	/* Follow enum power_state, if the system power state is lower than this power state,
	 * it will be turned off (low)
	 */
	uint8_t power_domain;
} __packed;

enum gpu_thermal_sensor {
	GPU_THERM_INVALID,
	GPU_THERM_F75303,
} __packed;
struct gpu_cfg_thermal {
	uint8_t thermal_type;
	uint8_t address;
	uint32_t reserved;
	uint32_t reserved2;
} __packed;

struct gpu_cfg_custom_temp {
	uint8_t idx;
	uint16_t temp_fan_off;
	uint16_t temp_fan_max;
} __packed;

struct gpu_cfg_fan {
	uint8_t idx;
	uint8_t flags;
	uint16_t min_rpm;
	uint16_t min_temp;
	uint16_t start_rpm;
	uint16_t max_rpm;
	uint16_t max_temp;
} __packed;

struct gpu_cfg_power {
	uint8_t device_idx;
	uint8_t battery_power;
	uint8_t average_power;
	uint8_t long_term_power;
	uint8_t short_term_power;
	uint8_t peak_power;
} __packed;

struct gpu_cfg_battery {
	uint16_t max_current;
	uint16_t max_mv;
	uint16_t min_mv;
	uint16_t max_charge_current;
} __packed;


enum gpu_subsys_type {
	GPU_ASSEMBLY = 0, /* Populated in header, not valid for extended structure */
	GPU_PCB = 1,
	GPU_LEFT_FAN = 2,
	GPU_RIGHT_FAN = 3,
	GPU_HOUSING = 4,
	GPU_SUBSYS_MAX = 10,
};
struct gpu_subsys_serial {
	uint8_t gpu_subsys;
	char serial[GPU_SERIAL_LEN];
} __packed;

enum gpu_pcie_cfg {
	PCIE_8X1 = 0,
	PCIE_4X1 = 1,
	PCIE_4X2 = 2,
} __packed;
BUILD_ASSERT(sizeof(enum gpu_pcie_cfg) == sizeof(uint8_t));


enum gpu_vendor {
	GPU_VENDOR_INITIALIZING = 0,
	GPU_FAN_ONLY = 1,
	GPU_AMD_R23M = 2,
	GPU_SSD = 3,
	GPU_PCIE_ACCESSORY = 4
} __packed;
BUILD_ASSERT(sizeof(enum gpu_vendor) == sizeof(uint8_t));

enum gpu_pd {
	PD_TYPE_INVALID = 0,
	PD_TYPE_ETRON_EJ889I = 1,
	PD_TYPE_MAX = 0xFF
} __packed;
struct gpu_subsys_pd {
	uint8_t gpu_pd_type;
	uint8_t address;
	uint32_t flags;
	uint32_t pdo;
	uint32_t rdo;
	uint8_t power_domain;
	uint8_t gpio_hpd;
	uint8_t gpio_interrupt;
} __packed;

void init_gpu_module(void);
void init_uma_fan(void);
void deinit_gpu_module(void);
void gpu_module_gpio_safe(void);
int parse_gpu_eeprom(void);
void set_gpu_gpios_powerstate(void);
void control_5valw_power(enum power_request_source_t pwr, bool enable);

const struct gpio_dt_spec *gpu_gpio_to_dt(enum gpu_gpio_idx gpio_idx);
const struct gpio_int_config *gpu_gpio_to_dt_int(enum gpu_gpio_idx gpio_idx);
void set_gpu_gpio(enum gpu_gpio_purpose gpiofn, int level);
int get_gpu_gpio(enum gpu_gpio_purpose gpiofn);

bool gpu_present(void);

#endif /* __CROS_EC_GPU_CONFIGURATION_H__ */
