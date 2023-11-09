/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ADC for checking BOARD ID.
 */

#include "gpu_configuration.h"
#include "i2c.h"
#include "util.h"
#include "console.h"
#include "zephyr_console_shim.h"

#include "power.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "gpio/gpio_int.h"

#include "board_host_command.h"
#include "customized_shared_memory.h"
#include "hooks.h"

#include "ej889i.h"
#include "gpu_f75303.h"
#include "board_adc.h"
#include "thermal.h"
#include "fan.h"
#include "board_thermal.h"

#include "gpu.h"

#define CPRINTS(format, args...) cprints(CC_I2C, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ##args)


#define SUPPORTED_DESCRIPTOR_MAJOR 0
#define SUPPORTED_DESCRIPTOR_MINOR 1

/**
 * The type of the CRC values.
 *
 * This type must be big enough to contain at least 32 bits.
 */
typedef uint_fast32_t crc_t;

/**
 * Calculate the initial crc value.
 *
 * \return     The initial crc value.
 */
static inline crc_t crc_init(void)
{
	return 0xffffffff;
}

/**
 * Static table used for the table_driven implementation.
 */
static const crc_t crc_table[256] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
	0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
	0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
	0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
	0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
	0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
	0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
	0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
	0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
	0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
	0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
	0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
	0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
	0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
	0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
	0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
	0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
	0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
	0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
	0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
	0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
	0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
	0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
	0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
	0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
	0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
	0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
	0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
	0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
	0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
	0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

crc_t crc_update(crc_t crc, const void *data, size_t data_len)
{
	const unsigned char *d = (const unsigned char *)data;
	unsigned int tbl_idx;

	while (data_len--) {
		tbl_idx = (crc ^ *d) & 0xff;
		crc = (crc_table[tbl_idx] ^ (crc >> 8)) & 0xffffffff;
		d++;
	}
	return crc & 0xffffffff;
}

/**
 * Calculate the final crc value.
 *
 * \param[in] crc  The current crc value.
 * \return     The final crc value.
 */
static inline crc_t crc_finalize(crc_t crc)
{
	return crc ^ 0xffffffff;
}


bool gpu_cfg_descriptor_valid;
bool gpu_verbose;
struct gpu_cfg_descriptor gpu_descriptor;
uint8_t gpu_read_buff[GPU_MAX_BLOCK_LEN];
uint8_t gpu_subsys_serials[GPU_SUBSYS_MAX][20];
enum gpu_pcie_cfg gpu_pcie_configuration;
enum gpu_vendor  gpu_vendor;

uint8_t address = 0x50;

struct gpu_cfg_gpio gpu_gpio_cfgs[GPU_GPIO_MAX];


struct default_gpu_cfg {
	struct gpu_cfg_descriptor descriptor;

	struct gpu_block_header hdr0;
	enum gpu_pcie_cfg pcie_cfg;

	struct gpu_block_header hdr1;
	struct gpu_cfg_fan fan0_cfg;

	struct gpu_block_header hdr2;
	struct gpu_cfg_fan fan1_cfg;

	struct gpu_block_header hdr3;
	enum gpu_vendor vendor;

	struct gpu_block_header hdr4;
	struct gpu_cfg_gpio     gpio0;
	struct gpu_cfg_gpio     gpio1;
	struct gpu_cfg_gpio     gpio2;
	struct gpu_cfg_gpio     gpio3;
	struct gpu_cfg_gpio     gpio_vsys;
	struct gpu_cfg_gpio     gpio_fan;

	struct gpu_block_header hdr5;
	struct gpu_subsys_pd    pd;

	struct gpu_block_header hdr6;
	struct gpu_cfg_thermal therm;

	struct gpu_block_header hdr7;
	struct gpu_cfg_custom_temp custom_temp;

	struct gpu_block_header hdr8;
	struct gpu_subsys_serial pcba_serial;


} __packed;

static struct default_gpu_cfg gpu_cfg = {
	.descriptor = {
		.magic = {0x32, 0xac, 0x00, 0x00},
		.length = sizeof(struct gpu_cfg_descriptor),
		.descriptor_version_major = 0,
		.descriptor_version_minor = 1,
		.hardware_version = 0x0008,
		.hardware_revision = 0,
		.serial = {'F', 'R', 'A', 'K', 'M', 'B', 'C', 'P', '8', '1',
					'3', '3', '1', 'A', 'S', 'S', 'Y', '0', '\0', '\0'},
		.descriptor_length = sizeof(struct default_gpu_cfg) - sizeof(struct gpu_cfg_descriptor),
		.descriptor_crc32 = 0,
		.crc32 = 0
	},
	.hdr0 = {.block_type = GPUCFG_TYPE_PCIE, .block_length = sizeof(uint8_t)},
	.pcie_cfg = PCIE_8X1,

	.hdr1 = {.block_type = GPUCFG_TYPE_FAN, .block_length = sizeof(struct gpu_cfg_fan)},
	.fan0_cfg = {.idx = 0, .flags = 0, .min_rpm = 1000, .start_rpm = 1000, .max_rpm = 4700},

	.hdr2 = {.block_type = GPUCFG_TYPE_FAN, .block_length = sizeof(struct gpu_cfg_fan)},
	.fan1_cfg = {.idx = 1, .flags = 0, .min_rpm = 1000, .start_rpm = 1000, .max_rpm = 4500},

	.hdr3 = {.block_type = GPUCFG_TYPE_VENDOR, .block_length = sizeof(gpu_vendor)},
	.vendor = GPU_AMD_R23M,

	.hdr4 = {.block_type = GPUCFG_TYPE_GPIO, .block_length = (sizeof(struct gpu_cfg_gpio) * 6)},
	/* Critical temperature fault input */
	.gpio0 = {.gpio = GPU_1G1_GPIO0_EC, .function = GPIO_FUNC_TEMPFAULT, .flags = GPIO_INPUT, .power_domain = POWER_S3},
	/* DP HPD status from PD */
	.gpio1 = {.gpio = GPU_1H1_GPIO1_EC, .function = GPIO_FUNC_HPD, .flags = GPIO_INPUT, .power_domain = POWER_S5},
	/* AC/DC mode setting */
	.gpio2 = {.gpio = GPU_2A2_GPIO2_EC, .function = GPIO_FUNC_ACDC, .flags = GPIO_OUTPUT_LOW, .power_domain = POWER_S3},
	/* UNUSED */
	.gpio3 = {.gpio = GPU_2L7_GPIO3_EC, .function = GPIO_FUNC_UNUSED, .flags = GPIO_OUTPUT_LOW, .power_domain = POWER_G3},
	/* GPU_VSYS_EN */
	.gpio_vsys = {.gpio = GPU_VSYS_EN, .function = GPIO_FUNC_GPU_PWR, .flags = GPIO_OUTPUT_LOW, .power_domain = POWER_S3},

	.gpio_fan = {.gpio = GPU_FAN_EN, .function = GPIO_FUNC_HIGH, .flags = GPIO_OUTPUT_LOW, .power_domain = POWER_S0},

	.hdr5 = {.block_type = GPUCFG_TYPE_PD, .block_length = sizeof(struct gpu_subsys_pd)},
	.pd = {.gpu_pd_type = PD_TYPE_ETRON_EJ889I, .address = 0x60,
			.flags = 0, .pdo = 0, .rdo = 0, .power_domain = POWER_S5,
			.gpio_hpd = GPU_1H1_GPIO1_EC, .gpio_interrupt = GPU_1F2_I2C_S5_INT
	},

	.hdr6 = {.block_type = GPUCFG_TYPE_THERMAL_SENSOR, .block_length = sizeof(struct gpu_cfg_thermal)},
	.therm = {.thermal_type = GPU_THERM_F75303, .address = 0x4D},

	.hdr7 = {.block_type = GPUCFG_TYPE_CUSTOM_TEMP, .block_length = sizeof(struct gpu_cfg_custom_temp)},
	.custom_temp = {.idx = 2, .temp_fan_off = C_TO_K(48), .temp_fan_max = C_TO_K(69)},

	.hdr8 = {.block_type = GPUCFG_TYPE_SUBSYS, .block_length = sizeof(struct gpu_subsys_serial)},
	.pcba_serial = {.gpu_subsys = GPU_PCB, .serial = {'F', 'R', 'A', 'G', 'M', 'A', 'S', 'P', '8', '1',
					'3', '3', '1', 'P', 'C', 'B', '0', '0', '\0', '\0'},}
};

struct default_ssd_cfg {
	struct gpu_cfg_descriptor descriptor;

	struct gpu_block_header hdr0;
	enum gpu_pcie_cfg pcie_cfg;

	struct gpu_block_header hdr1;
	struct gpu_cfg_fan fan0_cfg;

	struct gpu_block_header hdr2;
	struct gpu_cfg_fan fan1_cfg;

	struct gpu_block_header hdr3;
	enum gpu_vendor vendor;

	struct gpu_block_header hdr4;
	struct gpu_cfg_gpio     gpio0;
	struct gpu_cfg_gpio     gpio1;
	struct gpu_cfg_gpio     gpio2;
	struct gpu_cfg_gpio     gpio3;
	struct gpu_cfg_gpio     gpio_edpaux;
	struct gpu_cfg_gpio     gpio_vsys;
	struct gpu_cfg_gpio     gpio_fan;


} __packed;

static struct default_ssd_cfg ssd_cfg = {
	.descriptor = {
		.magic = {0x32, 0xac, 0x00, 0x00},
		.length = sizeof(struct gpu_cfg_descriptor),
		.descriptor_version_major = 0,
		.descriptor_version_minor = 1,
		.hardware_version = 0x0008,
		.hardware_revision = 0,
		.serial = {'F', 'R', 'A', 'G', 'M', 'B', 'S', 'P', '8', '1',
					'3', '3', '1', 'D', 'U', 'M', 'M', 'Y', '\0', '\0'},
		.descriptor_length = sizeof(struct default_ssd_cfg) - sizeof(struct gpu_cfg_descriptor),
		.descriptor_crc32 = 0,
		.crc32 = 0
	},
	.hdr0 = {.block_type = GPUCFG_TYPE_PCIE, .block_length = sizeof(uint8_t)},
	.pcie_cfg = PCIE_4X2,

	.hdr1 = {.block_type = GPUCFG_TYPE_FAN, .block_length = sizeof(struct gpu_cfg_fan)},
	.fan0_cfg = {.idx = 0, .flags = 0, .min_rpm = 1000, .start_rpm = 1000, .max_rpm = 3700},

	.hdr2 = {.block_type = GPUCFG_TYPE_FAN, .block_length = sizeof(struct gpu_cfg_fan)},
	.fan1_cfg = {.idx = 1, .flags = 0, .min_rpm = 1000, .start_rpm = 1000, .max_rpm = 3700},

	.hdr3 = {.block_type = GPUCFG_TYPE_VENDOR, .block_length = sizeof(gpu_vendor)},
	.vendor = GPU_SSD,

	/* Power enable for SSD1 */
	.hdr4 = {.block_type = GPUCFG_TYPE_GPIO, .block_length = sizeof(struct gpu_cfg_gpio) * 7},
	.gpio0 = {.gpio = GPU_1G1_GPIO0_EC, .function = GPIO_FUNC_SSD1_POWER, .flags = GPIO_OUTPUT_LOW, .power_domain = POWER_S3},
	/* Power enable for SSD2 */
	.gpio1 = {.gpio = GPU_1H1_GPIO1_EC, .function = GPIO_FUNC_SSD2_POWER, .flags = GPIO_OUTPUT_LOW, .power_domain = POWER_S3},
	/* UNUSED */
	.gpio2 = {.gpio = GPU_2A2_GPIO2_EC, .function = GPIO_FUNC_UNUSED, .flags = GPIO_OUTPUT_LOW, .power_domain = POWER_G3},
	/* UNUSED */
	.gpio3 = {.gpio = GPU_2L7_GPIO3_EC, .function = GPIO_FUNC_UNUSED, .flags = GPIO_OUTPUT_LOW, .power_domain = POWER_G3},
	/* set mux configuration on mainboard for SSD */
	.gpio_edpaux = {.gpio = GPU_PCIE_MUX_SEL, .function = GPIO_FUNC_HIGH, .flags = GPIO_OUTPUT_LOW, .power_domain = POWER_S3},
	/* GPU_VSYS_EN */
	.gpio_vsys = {.gpio = GPU_VSYS_EN, .function = GPIO_FUNC_HIGH, .flags = GPIO_OUTPUT_LOW, .power_domain = POWER_S3},

	.gpio_fan = {.gpio = GPU_FAN_EN, .function = GPIO_FUNC_HIGH, .flags = GPIO_OUTPUT_LOW, .power_domain = POWER_S0},

};

const struct gpio_dt_spec * gpu_gpio_to_dt(enum gpu_gpio_idx gpio_idx)
{
	switch (gpio_idx) {
	case GPU_1G1_GPIO0_EC:
		return GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio00_ec);
	case GPU_1H1_GPIO1_EC:
		return GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio01_ec);
	case GPU_2A2_GPIO2_EC:
		return GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio02_ec);
	case GPU_2L7_GPIO3_EC:
		return GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio03_ec);
	case GPU_1F2_I2C_S5_INT:
		return GPIO_DT_FROM_NODELABEL(gpio_gpu_i2c_s5_int);
	case GPU_2B5_ALERTn:
		return GPIO_DT_FROM_NODELABEL(gpio_gpu_alert_l);
	case GPU_ECPWM_EN:
		return GPIO_DT_FROM_NODELABEL(gpio_ec_pwm_en_l);
	case GPU_EDP_MUX_SEL:
		return GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw);
	case GPU_PCIE_MUX_SEL: /* select between EDP AUX or SSD PCIE2 CLK*/
		if (board_get_version() >= BOARD_VERSION_7)
			return GPIO_DT_FROM_NODELABEL(gpio_ssd_gpu_sel);
		return NULL;
	case GPU_VSYS_EN:
		return GPIO_DT_FROM_NODELABEL(gpio_gpu_vsys_en);
	case GPU_VADP_EN:
		return GPIO_DT_FROM_NODELABEL(gpio_gpu_vdap_en);
	case GPU_FAN_EN:
		if (board_get_version() >= BOARD_VERSION_8)
			return GPIO_DT_FROM_NODELABEL(gpio_gpu_fan_en);
		__fallthrough;
	/* the following GPIOs cannot be controlled directly */
	case GPU_1F3_MUX1:
	case GPU_1G3_MUX2:
	case GPU_1L1_DGPU_PWROK:
	case GPU_1C3_ALW_CLK: /* ALW I2C CLOCK PIN to EC */
	case GPU_1D3_ALW_DAT: /* ALW I2C DATA PIN to EC */
	default:
		return NULL;
	}
	return NULL;
}

const char * gpu_gpio_idx_to_name(enum gpu_gpio_idx idx)
{
	switch (idx) {
	case GPU_GPIO_INVALID:
		return "INVALID";
	case GPU_1G1_GPIO0_EC:
		return "GPIO0";
	case GPU_1H1_GPIO1_EC:
		return "GPIO1";
	case GPU_2A2_GPIO2_EC:
		return "GPIO2";
	case GPU_2L7_GPIO3_EC:
		return "GPIO3";
	case GPU_2L5_TH_OVERTn:
		return "OVERTn";
	case GPU_1F2_I2C_S5_INT:
		return "S5_INT";
	case GPU_1L1_DGPU_PWROK:
		return "PWROK";
	case GPU_1C3_ALW_CLK:
		return "CLK";
	case GPU_1D3_ALW_DAT:
		return "DAT";
	case GPU_1F3_MUX1:
		return "MUX1";
	case GPU_1G3_MUX2:
		return "MUX2";
	case GPU_2B5_ALERTn:
		return "ALERTn";
	case GPU_ECPWM_EN:
		return "ECPWM_EN";
	case GPU_EDP_MUX_SEL:
		return "EDP_MUX_SEL";
	case GPU_PCIE_MUX_SEL:
		return "PCIE_MUX_SEL";
	case GPU_VSYS_EN:
		return "VSYS_EN";
	case GPU_VADP_EN:
		return "VADP_EN";
	case GPU_FAN_EN:
		return "FAN_EN";
	default:
		return "UNKNOWN IDX";
	}
}

const char * gpu_gpio_fn_to_name(enum gpu_gpio_purpose p)
{
	switch (p) {
	case GPIO_FUNC_UNUSED:
	return "UNUSED";
	case GPIO_FUNC_HIGH:
		return "HIGH";
	case GPIO_FUNC_TEMPFAULT:
		return "TEMPFAULT";
	case GPIO_FUNC_ACDC:
		return "ACDC";
	case GPIO_FUNC_HPD:
		return "HPD";
	case GPIO_FUNC_PD_INT:
		return "PD_INT";
	case GPIO_FUNC_SSD1_POWER:
		return "SSD1_POWER";
	case GPIO_FUNC_SSD2_POWER:
		return "SSD2_POWER";
	case GPIO_FUNC_EC_PWM_EN:
		return "ECPWM_EN";
	case GPIO_FUNC_EDP_MUX_SEL:
		return "EDP_MUX_SEL";
	case GPIO_FUNC_VSYS_EN:
		return "VSYS_EN";
	case GPIO_FUNC_VADP_EN:
		return "VADP_EN";
	case GPIO_FUNC_GPU_PWR:
		return "GPUPWR";
	default: 
		return "UNKNOWN IDX";
	}
}


bool gpu_present(void)
{
	switch (gpu_vendor) {
		case GPU_AMD_R23M:
			return true;
		default:
			return false;
	}
}

const struct gpio_int_config * gpu_gpio_to_dt_int(enum gpu_gpio_idx gpio_idx)
{
	switch (gpio_idx) {
	/* case GPU_1G1_GPIO0_EC:
		return GPIO_INT_FROM_NODELABEL(gpio_gpu_b_gpio00_ec); */
	case GPU_1H1_GPIO1_EC:
		return GPIO_INT_FROM_NODELABEL(int_dp_hot_plug);
	/* case GPU_2A2_GPIO2_EC:
		return GPIO_INT_FROM_NODELABEL(gpio_gpu_b_gpio02_ec);
	case GPU_2L7_GPIO3_EC:
		return GPIO_INT_FROM_NODELABEL(gpio_gpu_b_gpio03_ec); */
	case GPU_1F2_I2C_S5_INT:
		return GPIO_INT_FROM_NODELABEL(int_gpu_pd);
	/* case GPU_2B5_ALERTn:
		return GPIO_INT_FROM_NODELABEL(gpio_gpu_alert_l); */
	default:
		return NULL;
	} 
}


void set_gpu_gpio(enum gpu_gpio_purpose gpiofn, int level)
{
	int i;
	const struct gpio_dt_spec * dt_gpio;
	enum power_state ps = power_get_state();

	if (gpiofn >= GPIO_FUNC_MAX) {
		return;
	}
	for(i = 0; i < GPU_GPIO_MAX; i++) {
		if (gpu_gpio_cfgs[i].function == gpiofn) {
			dt_gpio = gpu_gpio_to_dt(gpu_gpio_cfgs[i].gpio);
			if (dt_gpio == NULL)
				continue;
			if (ps >= gpu_gpio_cfgs[i].power_domain) {
				if (gpu_verbose)
					CPRINTS("GPUGPIO %s %s=%d", gpu_gpio_idx_to_name(gpu_gpio_cfgs[i].gpio),
						gpu_gpio_fn_to_name(gpu_gpio_cfgs[i].function), level);
				gpio_pin_set_dt(dt_gpio, level);
			} else {
				gpio_pin_set_dt(dt_gpio, 0);
			}
		}
	}
}

int get_gpu_gpio(enum gpu_gpio_purpose gpiofn)
{
	int i;
	const struct gpio_dt_spec * dt_gpio;

	if (gpiofn >= GPIO_FUNC_MAX) {
		return -1;
	}
	for(i = 0; i < GPU_GPIO_MAX; i++) {
		if (gpu_gpio_cfgs[i].function == gpiofn) {
			dt_gpio = gpu_gpio_to_dt(gpu_gpio_cfgs[i].gpio);
			if (dt_gpio == NULL)
				continue;
			return gpio_pin_get_dt(dt_gpio);
		}
	}
	return -1;
}
void set_gpu_gpios_configuration(void)
{
	int i;
	const struct gpio_dt_spec * dt_gpio;

	for(i = 0; i < GPU_GPIO_MAX; i++) {
		dt_gpio = gpu_gpio_to_dt(gpu_gpio_cfgs[i].gpio);
		if (dt_gpio == NULL)
			continue;
		if (gpu_verbose)
			CPRINTS("GPUGPIO CFG:%s %s=0x%X",
						gpu_gpio_idx_to_name(gpu_gpio_cfgs[i].gpio),
						gpu_gpio_fn_to_name(gpu_gpio_cfgs[i].function),
						gpu_gpio_cfgs[i].flags);
		gpio_pin_configure_dt(dt_gpio, gpu_gpio_cfgs[i].flags);
	}
}
/*
 * 
 */
void set_gpu_gpios_powerstate(void)
{
	int i;
	const struct gpio_dt_spec * dt_gpio;
	enum power_state ps = power_get_state();

	switch(ps) {
	case POWER_G3S5:
	case POWER_S3S5:
		ps = POWER_S5;
		break;
	case POWER_S5S3:
	case POWER_S0S3:
		ps = POWER_S3;
		break;
	case POWER_S3S0:
	case POWER_S0ixS0:
		ps = POWER_S0;
		break;
	default:
		break;
	}

	for(i = 0; i < GPU_GPIO_MAX; i++) {
		dt_gpio = gpu_gpio_to_dt(gpu_gpio_cfgs[i].gpio);
		if (dt_gpio == NULL)
			continue;

		if (ps >= gpu_gpio_cfgs[i].power_domain) {
			if (gpu_gpio_cfgs[i].function ==GPIO_FUNC_HIGH) {
				if (gpu_verbose)
					CPRINTS("GPU %s=HIGH", gpu_gpio_idx_to_name(gpu_gpio_cfgs[i].gpio));
				gpio_pin_set_dt(dt_gpio, 1);
			}
		} else {
			if (gpu_verbose)
				CPRINTS("GPU %s=0", gpu_gpio_idx_to_name(gpu_gpio_cfgs[i].gpio));
			gpio_pin_set_dt(dt_gpio, 0);
		}
	}
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, set_gpu_gpios_powerstate, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, set_gpu_gpios_powerstate, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, set_gpu_gpios_powerstate, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, set_gpu_gpios_powerstate, HOOK_PRIO_DEFAULT);

void set_gpu_ac(void)
{
	int level = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_hw_acav_in));
	set_gpu_gpio(GPIO_FUNC_ACDC, level);
}
DECLARE_HOOK(HOOK_AC_CHANGE, set_gpu_ac, HOOK_PRIO_FIRST);

void reset_mux_status(void)
{
	uint8_t gpu_status = *host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_CONTROL);

	/* When the system shuts down, the gpu mux needs to switch to iGPU */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw), 0);
	gpu_status &= 0xFC;
	gpu_status &= ~GPU_MUX;
	*host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_CONTROL) = gpu_status;
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, reset_mux_status, HOOK_PRIO_DEFAULT);

static void reset_smart_access_graphic(void)
{
	/* smart access graphic default should be hybrid mode */
	if (chipset_in_state(CHIPSET_STATE_ON))
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw), 0);
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, reset_smart_access_graphic, HOOK_PRIO_DEFAULT);

int parse_gpu_header(void)
{
	uint32_t i;
	int rv = EC_SUCCESS;
	crc_t crc = crc_init();

	address = 0x50;
	rv = i2c_read_offset16_block(I2C_PORT_GPU0, address,
								0,
								(void *)&gpu_descriptor, sizeof(gpu_descriptor));
	if (rv != EC_SUCCESS) {
		CPRINTS("%s trying address 0x52", __func__);
		address = 0x52;
		rv = i2c_read_offset16_block(I2C_PORT_GPU0, address,
									0,
									(void *)&gpu_descriptor, sizeof(gpu_descriptor));

		if (rv != EC_SUCCESS) {
			CPRINTS("%s hdr read failed", __func__);
			return EC_ERROR_INVAL;
		}
	}

	crc = crc_update(crc, &gpu_descriptor, sizeof(struct gpu_cfg_descriptor) - sizeof(uint32_t));

	for (i = 0; i < 4; i++) {
		if (gpu_descriptor.magic[i] == 0xff) {
			CPRINTS("magic invalid");
			return EC_ERROR_CRC;
		}
	}

	if (crc_finalize(crc) != gpu_descriptor.crc32) {
		CPRINTS("GPU header crc fail!: %X != %X", crc_finalize(crc), gpu_descriptor.crc32);
		return EC_ERROR_CRC;
	}

	if (gpu_descriptor.descriptor_version_major > SUPPORTED_DESCRIPTOR_MAJOR) {
		CPRINTS("unsupported gpu major version %d", gpu_descriptor.descriptor_version_major);
		return EC_ERROR_INVAL;
	}

	if (gpu_descriptor.descriptor_version_minor > SUPPORTED_DESCRIPTOR_MINOR &&
		gpu_descriptor.descriptor_version_major <= SUPPORTED_DESCRIPTOR_MAJOR) {
		CPRINTS("unsupported gpu minor version %d", gpu_descriptor.descriptor_version_minor);
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;

}

uint8_t load_from;
int load_configuration_block(uint16_t offset, uint8_t *data, int len)
{
	int rv = EC_SUCCESS;
	if (load_from == 0) {
		rv = i2c_read_offset16_block(I2C_PORT_GPU0, address,
									offset, data, len);
	} else if (load_from == 1) {
		memcpy(data, ((uint8_t *)&gpu_cfg) + offset, len);
	} else if (load_from == 2) {
		memcpy(data, ((uint8_t *)&ssd_cfg) + offset, len);
	} else {
		rv = EC_ERROR_INVAL;
	}
	return rv;
}
int parse_gpu_eeprom(void)
{

	int rv;
	uint32_t i;
	uint8_t * v;
	int offset = 0;
	crc_t crc = crc_init();
	struct gpu_block_header hdr;
	load_from = 0;
	gpu_cfg_descriptor_valid = false;

	rv = parse_gpu_header();

	if (rv != EC_SUCCESS) {
		/* run device detection */
		/* check if GPU PD is present */
		rv = i2c_read8(I2C_PORT_GPU0, 0x60, 0x800E, &i);
		if (rv == EC_SUCCESS) {
			/* assume R23M GPU */
			CPRINTS("Detected EEPROM and PD: Defaulting to R23M");
			memcpy(&gpu_descriptor, &gpu_cfg.descriptor, sizeof(struct gpu_cfg_descriptor));
			load_from = 1;
			rv = EC_SUCCESS;
		} else {
			rv = i2c_read8(I2C_PORT_GPU0, 0x50, 0x00, &i);
			if (rv == EC_SUCCESS) {
				CPRINTS("Detected blank EEPROM only: defaulting to dual ssd");
				memcpy(&gpu_descriptor, &ssd_cfg.descriptor, sizeof(struct gpu_cfg_descriptor));

				load_from = 2;
				rv = EC_SUCCESS;
			}
		}
	}

	if (rv != EC_SUCCESS) {
		return rv;
	}

	crc = crc_init();

	offset = sizeof(gpu_descriptor);
	while (offset < (gpu_descriptor.descriptor_length + sizeof(struct gpu_cfg_descriptor))) {
		rv = load_configuration_block(offset,
									(void *)&hdr, sizeof(hdr));
		if (rv != EC_SUCCESS) {
			CPRINTS("block read failed");
			return EC_ERROR_INVAL;
		}
		crc = crc_update(crc, &hdr, sizeof(hdr));
		offset += sizeof(hdr);

		if (hdr.block_length > GPU_MAX_BLOCK_LEN) {
			CPRINTS("ERR:block %d over length!",hdr.block_type);
			offset += hdr.block_length;
			continue;
		}

		if (gpu_verbose)
			CPRINTS("GPUCFG Block:%d Len:%d", hdr.block_type, hdr.block_length);

		rv = load_configuration_block(offset,
									gpu_read_buff,
									hdr.block_length);

		crc = crc_update(crc, gpu_read_buff, hdr.block_length);

		if (rv != EC_SUCCESS) {
			CPRINTS("block read failed");
			return EC_ERROR_INVAL;
		}
		switch (hdr.block_type) {
		case GPUCFG_TYPE_GPIO:
			{
				for(v = gpu_read_buff; v < gpu_read_buff + hdr.block_length; v += sizeof(struct gpu_cfg_gpio)) {
					struct gpu_cfg_gpio *gpiocfg = (struct gpu_cfg_gpio *)v;
					if (gpiocfg->gpio < GPU_GPIO_MAX) {
						memcpy(&gpu_gpio_cfgs[gpiocfg->gpio], gpiocfg, sizeof(struct gpu_cfg_gpio));
					}
				}
			}
			break;
		case GPUCFG_TYPE_THERMAL_SENSOR:
			struct gpu_cfg_thermal *tm = (struct gpu_cfg_thermal *)gpu_read_buff;
			if (tm->thermal_type == GPU_THERM_F75303)
			{
				gpu_f75303_init(tm);
			}
			break;
		case GPUCFG_TYPE_CUSTOM_TEMP:
			struct gpu_cfg_custom_temp *tc = (struct gpu_cfg_custom_temp *)gpu_read_buff;
			/* TODO dont hard code index here */
			if (tc->idx < 8) {
				thermal_params[tc->idx].temp_fan_max = tc->temp_fan_max;
				thermal_params[tc->idx].temp_fan_off = tc->temp_fan_off;
			}
			break;
		case GPUCFG_TYPE_FAN:
			struct gpu_cfg_fan *fan = (struct gpu_cfg_fan *)gpu_read_buff;
			if (fan->idx < 2) {
				fan_configure_gpu(fan);
			}
			break;
		case GPUCFG_TYPE_POWER:
			break;
		case GPUCFG_TYPE_BATTERY:
			break;
		case GPUCFG_TYPE_PCIE:
			gpu_pcie_configuration = gpu_read_buff[0];
			*host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_CONTROL) =
				(*host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_CONTROL) & (~GPU_PCIE_MASK)) +
				((gpu_pcie_configuration << 6) & GPU_PCIE_MASK);
			break;
		case GPUCFG_TYPE_VENDOR:
			gpu_vendor = gpu_read_buff[0];
			*host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_TYPE) = gpu_read_buff[0];
			if (gpu_vendor == GPU_AMD_R23M) {
				*host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_CONTROL) |= GPU_PRESENT;
			}
			
		case GPUCFG_TYPE_DPMUX:
			break;
		case GPUCFG_TYPE_SUBSYS:
			{
				struct gpu_subsys_serial *subsys = (struct gpu_subsys_serial *)gpu_read_buff;
				if (subsys->gpu_subsys && subsys->gpu_subsys < GPU_SUBSYS_MAX) {
					memcpy(gpu_subsys_serials[subsys->gpu_subsys-1], subsys->serial, sizeof(subsys->serial));
				}
			}
			break;
		case GPUCFG_TYPE_PD:
			{
				struct gpu_subsys_pd *pd = (struct gpu_subsys_pd *)gpu_read_buff;
				if (pd->gpu_pd_type == PD_TYPE_ETRON_EJ889I) {
					ej889i_init(pd);
				}
			}
			break;
		default:
			CPRINTS("descriptor block unknown type: %d", hdr.block_type);
			return EC_ERROR_UNIMPLEMENTED;
			break;
		}
		offset += hdr.block_length;
	}
	crc = crc_finalize(crc);
	if (crc != gpu_descriptor.descriptor_crc32 && load_from == 0) {
		CPRINTS("CRC fail!: %X != %X", crc, gpu_descriptor.descriptor_crc32);
		return EC_ERROR_CRC;
	}
	gpu_cfg_descriptor_valid = true;
	CPRINTS("GPU descriptor read complete");

	set_gpu_gpios_configuration();

	set_gpu_gpios_powerstate();
	return EC_SUCCESS;
}
DECLARE_DEFERRED(parse_gpu_eeprom);

void gpu_module_gpio_safe(void)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio00_ec), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio01_ec), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio02_ec), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio03_ec), 0);

	/* tristate all EC general purpose GPIOs */
	gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio00_ec), GPIO_INPUT);
	gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio01_ec), GPIO_INPUT);
	gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio02_ec), GPIO_INPUT);
	gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio03_ec), GPIO_INPUT);

	if (board_get_version() >= BOARD_VERSION_7)
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd_gpu_sel), 0);
	if (board_get_version() >= BOARD_VERSION_8)
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_fan_en), 0);

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_vsys_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_3v_5v_en), 0);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw), 0);

}
void deinit_gpu_module(void)
{
	gpu_cfg_descriptor_valid = 0;
	gpu_vendor = GPU_VENDOR_INITIALIZING;
	address = 0x50;
	memset(&gpu_descriptor, 0x00, sizeof(struct gpu_cfg_descriptor));
	memset(gpu_subsys_serials, 0x00, sizeof(gpu_subsys_serials));
	memset(gpu_gpio_cfgs, 0x00, sizeof(gpu_gpio_cfgs));

	*host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_CONTROL) =
		(*host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_CONTROL) & (~GPU_PCIE_MASK));
	*host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_TYPE) = GPU_VENDOR_INITIALIZING;
	*host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_CONTROL) &= ~GPU_PRESENT;

	reset_mux_status();

	gpu_f75303_init(NULL);

	ej889i_init(NULL);

	gpu_module_gpio_safe();

	fan_configure_gpu(NULL);

	/* reset to APU only defaults */
	thermal_params[2].temp_fan_max = C_TO_K(62); /* QTH1 */
	thermal_params[2].temp_fan_off = C_TO_K(48); /* QTH1 */

}

void init_gpu_module(void)
{
	deinit_gpu_module();

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_3v_5v_en), 1);
	/* wait for power to come up to GPU PD and EEPROM */
	hook_call_deferred(&parse_gpu_eeprom_data, 150*MSEC);

}

void init_uma_fan(void)
{
	gpu_gpio_cfgs[0].gpio = GPU_FAN_EN;
	gpu_gpio_cfgs[0].function = GPIO_FUNC_HIGH;
	gpu_gpio_cfgs[0].flags = GPIO_OUTPUT_LOW;
	gpu_gpio_cfgs[0].power_domain = POWER_S0;

	set_gpu_gpios_configuration();
	set_gpu_gpios_powerstate();
}

static enum ec_status get_gpu_serial(struct host_cmd_handler_args *args)
{
	const struct ec_params_gpu_serial *p = args->params;
	struct ec_response_get_gpu_serial *r = args->response;
	if (!gpu_cfg_descriptor_valid) {
		return EC_RES_UNAVAILABLE;
	}

	r->idx = p->idx;
	r->valid = true;
	if (p->idx == 0) {
		memcpy(r->serial, gpu_descriptor.serial, sizeof(r->serial));
	} else if (p->idx < GPU_SUBSYS_MAX){
		memcpy(r->serial, gpu_subsys_serials[p->idx-1], sizeof(r->serial));
	}
	
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_GPU_SERIAL, get_gpu_serial, EC_VER_MASK(0));


static enum ec_status ec_response_get_gpu_config(struct host_cmd_handler_args *args)
{
	struct ec_response_get_gpu_config *r = args->response;
	if (!gpu_cfg_descriptor_valid) {
		return EC_RES_UNAVAILABLE;
	}

	r->gpu_pcie_config = gpu_pcie_configuration;
	r->gpu_vendor = gpu_vendor;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_GPU_PCIE, ec_response_get_gpu_config, EC_VER_MASK(0));

static int program_eeprom(const char * serial, struct gpu_cfg_descriptor * descriptor, size_t len)
{
	int rv;
	crc_t crc;
	size_t addr = 0;
	int d, i;
	CPRINTS("Programming EEPROM");
	memset(descriptor->serial, 0x00, GPU_SERIAL_LEN);
	strncpy(descriptor->serial, serial, GPU_SERIAL_LEN);

	crc = crc_init();
	crc = crc_update(crc, (uint8_t *)descriptor + sizeof(struct gpu_cfg_descriptor), len - sizeof(struct gpu_cfg_descriptor));
	descriptor->descriptor_crc32 = crc_finalize(crc);

	crc = crc_init();
	crc = crc_update(crc, descriptor, sizeof(struct gpu_cfg_descriptor)-sizeof(uint32_t));
	descriptor->crc32 = crc_finalize(crc);

	for(addr = 0; addr < len; addr += 32) {
		rv = i2c_write_offset16_block(I2C_PORT_GPU0, 0x50,
									addr, (uint8_t *)descriptor + addr, MIN(len - addr, 32));
		i = 0;
		do {
			k_msleep(5);
			rv = i2c_read8(I2C_PORT_GPU0, 0x50, 0x00, &d);   
		} while(++i < 32 && rv != EC_SUCCESS);
		
	}
	return rv;
}

static enum ec_status hc_program_gpu_eeprom(struct host_cmd_handler_args *args)
{
	const struct ec_params_program_gpu_serial *p = args->params;
	struct ec_response_program_gpu_serial *r = args->response;

	if (p->magic == 0x0D) {
		r->valid = 1;
		program_eeprom(p->serial, (void *)&gpu_cfg, sizeof(gpu_cfg));
	} else if (p->magic == 0x55) {
		r->valid = 1;
		program_eeprom(p->serial, (void *)&ssd_cfg, sizeof(ssd_cfg));
	} else {
		r->valid = 0;
	}

	
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PROGRAM_GPU_EEPROM, hc_program_gpu_eeprom, EC_VER_MASK(0));

/*******************************************************************************/
/*                       EC console command debug writing GPU EEPROM           */
/*******************************************************************************/
static int cmd_gpucfg(int argc, const char **argv)
{
	struct gpu_cfg_descriptor descriptor; 
	int i;
	if (argc > 1) {
		if (!strncmp(argv[1], "read", 4)) {
			parse_gpu_eeprom();
		} else if (!strncmp(argv[1], "write", 4)) {
			/* write gpu SERIAL_NUMBER */
			if (argc > 3) {
				if (!strncmp(argv[2], "gpu", 3)) {
					program_eeprom(argv[3], (void *)&gpu_cfg, sizeof(gpu_cfg));
				} else if (!strncmp(argv[2], "ssd", 3)) {
					program_eeprom(argv[3], (void *)&ssd_cfg, sizeof(ssd_cfg));
				}
			}
		} else if (!strncmp(argv[1], "erase", 4)) {
			memset(&descriptor, 0xFF, sizeof(struct gpu_cfg_descriptor));
			i2c_write_offset16_block(I2C_PORT_GPU0, 0x50,
									0x0000, (void *)&descriptor, sizeof(struct gpu_cfg_descriptor));
			CPRINTS("ERASE");
		} else if (!strncmp(argv[1], "verbose", 7)) {
			CPRINTS("GPU VERBOSE");
			gpu_verbose = 1;
		}

	} else {
			CPRINTS("GPU Descriptor %s", gpu_cfg_descriptor_valid ? "Valid": "Invalid");
			CPRINTS("  From %s", load_from == 0 ? "EEPROM" : load_from == 1 ? "GPUCFG" : "SSDCFG");
			CPRINTS("  Header: V:%d.%d HW:0x%X SN:%s CRC:0x%X",
					gpu_descriptor.descriptor_version_major,
					gpu_descriptor.descriptor_version_minor,
					gpu_descriptor.hardware_version,
					gpu_descriptor.serial,
					gpu_descriptor.crc32);
			CPRINTS("    Len: %d Dcrc32:0x%X",
					gpu_descriptor.descriptor_length,
					gpu_descriptor.descriptor_crc32);

			CPRINTS(" SN: %s", gpu_descriptor.serial);
			for (i = 0; i < GPU_SUBSYS_MAX; i++) {
				if (gpu_subsys_serials[i][0])
					CPRINTS(" SubsysSN%d: %s", i, &gpu_subsys_serials[i][0]);
			}

			CPRINTS(" MMIO GPU_CONTROL=0x%X", *host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_CONTROL));
			CPRINTS(" MMIO GPU_TYPE   =0x%X", *host_get_memmap(EC_CUSTOMIZED_MEMMAP_GPU_TYPE));
			CPRINTS(" Interposer");
			CPRINTS("   LEFT: %d, RIGHT %d RAW %d, %d",
				get_hardware_id(ADC_GPU_BOARD_ID_0),
				get_hardware_id(ADC_GPU_BOARD_ID_1),
				adc_read_channel(ADC_GPU_BOARD_ID_0),
				adc_read_channel(ADC_GPU_BOARD_ID_1));

			CPRINTS(" GPIOS");
			CPRINTS("   GPIO0     %d", gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio00_ec)));
			CPRINTS("   GPIO1     %d", gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio01_ec)));
			CPRINTS("   GPIO2     %d", gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio02_ec)));
			CPRINTS("   GPIO3     %d", gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_b_gpio03_ec)));
			CPRINTS("   S5_INT    %d", gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_i2c_s5_int)));
			CPRINTS("   ALERTn    %d", gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_alert_l)));
			CPRINTS("   EDPMUX    %d", gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_mux_pwm_sw)));
			CPRINTS("   SSDMUX    %d", gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ssd_gpu_sel)));
			CPRINTS("   VSYSEN    %d", gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_vsys_en)));
			CPRINTS("   VADP_EN   %d", gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_vdap_en)));
			CPRINTS("   FAN_EN    %d", gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_fan_en)));
			CPRINTS("   GPUPWR_EN %d", gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_dgpu_pwr_en)));
			CPRINTS("   ECPWM_EN  %d", gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_pwm_en_l)));
			CPRINTS("   ALW_EN    %d", gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_gpu_3v_5v_en)));
			CPRINTS("   BAY DOOR  %s", get_gpu_latch() ? "Closed" : "Open");
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(gpucfg, cmd_gpucfg,
			"[gpucfg read/write/erase/verbose]",
			"read and write gpu descriptor");
