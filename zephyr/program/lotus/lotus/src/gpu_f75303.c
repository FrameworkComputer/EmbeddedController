#include "gpio/gpio_int.h"
#include "gpio.h"
#include "gpu.h"
#include "i2c.h"
#include "system.h"
#include "thermal.h"
#include "gpu_configuration.h"
#include "console.h"
#include "hooks.h"
#include "chipset.h"


LOG_MODULE_REGISTER(gpu_f75303, LOG_LEVEL_DBG);


#define GPU_F75303_I2C_ADDR_FLAGS 0x4D

#define GPU_F75303_REG_LOCAL_ALERT   0x05
#define GPU_F75303_REG_REMOTE1_ALERT 0x07
#define GPU_F75303_REG_REMOTE2_ALERT 0x15

#define GPU_F75303_REG_REMOTE1_THERM 0x19
#define GPU_F75303_REG_REMOTE2_THERM 0x1A
#define GPU_F75303_REG_LOCAL_THERM   0x21


uint8_t gpu_f75303_address;

void gpu_f75303_init(struct gpu_cfg_thermal * sensor)
{
    if (sensor) {
        gpu_f75303_address = sensor->address;
        gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_gpu_power_en));
    } else {
        if (gpu_f75303_address) {
            gpu_f75303_address = 0;
            gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_gpu_power_en));
        }
    }
}

bool gpu_f75303_present(void) {
	return gpu_f75303_address != 0;
}


static void gpu_board_f75303_poweron_config(void)
{
	int idx, rv;
	static const uint8_t temp[6] = {105, 105, 105, 110, 110, 110};
	static const uint8_t reg_arr[6] = {
		GPU_F75303_REG_LOCAL_ALERT,
		GPU_F75303_REG_REMOTE1_ALERT,
		GPU_F75303_REG_REMOTE2_ALERT,
		GPU_F75303_REG_REMOTE1_THERM,
		GPU_F75303_REG_REMOTE2_THERM,
		GPU_F75303_REG_LOCAL_THERM,
	};

	if (gpu_f75303_present() && chipset_in_state(CHIPSET_STATE_ON)) {
		for (idx = 0; idx < sizeof(reg_arr); idx++) {
			rv = i2c_write8(I2C_PORT_GPU0, GPU_F75303_I2C_ADDR_FLAGS,
					reg_arr[idx], temp[idx]);

			if (rv != EC_SUCCESS)
				LOG_INF("gpu f75303 init reg 0x%02x failed", reg_arr[idx]);

			k_msleep(1);
		}
	}
}
DECLARE_DEFERRED(gpu_board_f75303_poweron_config);

void gpu_power_enable_handler(void)
{
	/* we needs to re-initial the thermal sensor and gpu when gpu power enable */
	if (gpu_f75303_address && gpu_power_enable())
		hook_call_deferred(&gpu_board_f75303_poweron_config_data, 500 * MSEC);

}