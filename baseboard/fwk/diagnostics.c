#include "util.h"
#include "hooks.h"

#include "board.h"
#include "chipset.h"
#include "ec_commands.h"
#include "gpio.h"
#include "driver/temp_sensor/f75303.h"
#include "diagnostics.h"
#include "fan.h"
#include "host_command_customization.h"
#include "power.h"
#include "port80.h"
#include "adc.h"
#include "timer.h"
#include "led_pwm.h"

#include "battery.h"
#include "charge_state.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)


uint32_t hw_diagnostics;
uint32_t hw_diagnostic_tick;
uint32_t hw_diagnostics_ctr;
uint32_t bios_code;
uint8_t bios_hc;

uint8_t bios_complete;
uint8_t fan_seen;
uint8_t s0_seen;
uint8_t run_diagnostics;

int standalone_mode;

void set_standalone_mode(int enable)
{
	CPRINTS("set standalone = %d", enable);
	standalone_mode = enable;
}

int get_standalone_mode(void)
{
	return standalone_mode;
}

void reset_diagnostics(void)
{
	hw_diagnostics =0;
	hw_diagnostics_ctr = 0;
	bios_complete = 0;
	bios_code = 0;
	hw_diagnostic_tick = 0;
	fan_seen = 0;
	s0_seen = 0;
	run_diagnostics = 1;
}

void cancel_diagnostics(void)
{
	run_diagnostics = 0;
}

static void set_diagnostic_leds(int color)
{
	set_pwm_led_color(PWM_LED0, color);
	set_pwm_led_color(PWM_LED1, color);
}
#define TICK_PER_SEC	4
bool diagnostics_tick(void)
{
	if (hw_diagnostics_ctr >= DIAGNOSTICS_MAX) {
		run_diagnostics = 0;
		return false;
	}
	if (run_diagnostics == 0) {
		return false;
	}

	if (bios_complete && hw_diagnostics == 0) {
		/*exit boot condition - everything is ok after minimum 4 seconds of checking*/
		if (fan_seen){
			run_diagnostics = 0;
		}
		return false;
	}

	if (bios_hc == CODE_DDR_TRAINING_START) {
		set_diagnostic_leds(EC_LED_COLOR_GREEN);
		return true;
	}

	if (fan_get_rpm_actual(0) > 100) {
		fan_seen = true;
	}

	if (power_get_state() == POWER_S0) {
		s0_seen = true;
	}

	hw_diagnostic_tick++;

	if (hw_diagnostic_tick < 15*TICK_PER_SEC) {
		/*give us more time for checks to complete*/
		return false;
	}

	if (fan_seen == false) {
		set_hw_diagnostic(DIAGNOSTICS_NOFAN, true);
	}
	if (s0_seen == false) {
		set_hw_diagnostic(DIAGNOSTICS_NO_S0, true);
	}

	if ((charge_get_state() == PWR_STATE_ERROR) && !standalone_mode) {
		set_hw_diagnostic(DIAGNOSTICS_HW_NO_BATTERY, true);
	}

	if (hw_diagnostic_tick & 0x01) {
		/*off*/

		set_diagnostic_leds(-1);
		return true;
	}

	if (hw_diagnostics_ctr == DIAGNOSTICS_START) {
		set_diagnostic_leds(EC_LED_COLOR_WHITE);
        bios_code = port_80_last();
		CPRINTS("Boot issue: HW 0x%08x BIOS: 0x%04x", hw_diagnostics, bios_code);
	} else if (hw_diagnostics_ctr  < DIAGNOSTICS_HW_FINISH) {
		set_diagnostic_leds((hw_diagnostics & (1<<hw_diagnostics_ctr)) ? EC_LED_COLOR_RED : EC_LED_COLOR_GREEN);
	} else if (hw_diagnostics_ctr == DIAGNOSTICS_HW_FINISH) {
		set_diagnostic_leds(EC_LED_COLOR_AMBER);
	} else if (hw_diagnostics_ctr < DIAGNOSTICS_MAX) {
		set_diagnostic_leds((bios_code & (1<<(hw_diagnostics_ctr-DIAGNOSTICS_BIOS_BIT0)))
								? EC_LED_COLOR_BLUE : EC_LED_COLOR_GREEN);
	}



	hw_diagnostics_ctr++;
	return true;

}
#define ADC_NC_DELTA 2000

static void diagnostic_check_tempsensor_deferred(void)
{
	int temps = 0;

	int low_adc[2];
	int high_adc[2];
	int device_id[2];

	f75303_get_val(F75303_IDX_LOCAL, &temps);
	if (temps == 0)
			set_hw_diagnostic(DIAGNOSTICS_THERMAL_SENSOR, true);



	gpio_set_flags(GPIO_TP_BOARD_ID, GPIO_PULL_UP);
	gpio_set_flags(GPIO_AD_BOARD_ID, GPIO_PULL_UP);
	usleep(5);
	high_adc[0] = adc_read_channel(ADC_TP_BOARD_ID);
	high_adc[1] = adc_read_channel(ADC_AUDIO_BOARD_ID);
	gpio_set_flags(GPIO_TP_BOARD_ID, GPIO_PULL_DOWN);
	gpio_set_flags(GPIO_AD_BOARD_ID, GPIO_PULL_DOWN);
	usleep(5);
	low_adc[0] = adc_read_channel(ADC_TP_BOARD_ID);
	low_adc[1] = adc_read_channel(ADC_AUDIO_BOARD_ID);

	gpio_set_flags(GPIO_TP_BOARD_ID, GPIO_FLAG_NONE);
	gpio_set_flags(GPIO_AD_BOARD_ID, GPIO_FLAG_NONE);
	usleep(10);
	device_id[0] = get_hardware_id(ADC_TP_BOARD_ID);
	device_id[1] = get_hardware_id(ADC_AUDIO_BOARD_ID);

	if ((device_id[0] <= BOARD_VERSION_1 ||  device_id[0] >= BOARD_VERSION_14 ||
		(high_adc[0] - low_adc[0]) > ADC_NC_DELTA) && !standalone_mode) {
		set_hw_diagnostic(DIAGNOSTICS_TOUCHPAD, true);
	}

	if ((device_id[1] <= BOARD_VERSION_1 ||  device_id[1] >= BOARD_VERSION_14 ||
		(high_adc[1] - low_adc[1]) > ADC_NC_DELTA) && !standalone_mode) {
		set_hw_diagnostic(DIAGNOSTICS_AUDIO_DAUGHTERBOARD, true);
	}
    CPRINTS("TP  Ver %d, delta %d", device_id[0], high_adc[0] - low_adc[0]);
    CPRINTS("Aud Ver %d, delta %d", device_id[1], high_adc[1] - low_adc[1]);

}
DECLARE_DEFERRED(diagnostic_check_tempsensor_deferred);

static void diagnostics_check_devices(void)
{
	hook_call_deferred(&diagnostic_check_tempsensor_deferred_data, 2000*MSEC);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME,
		diagnostics_check_devices,
		HOOK_PRIO_DEFAULT);

void set_hw_diagnostic(enum diagnostics_device_idx idx, bool error)
{
	if (error)
		hw_diagnostics |= 1 << idx;
	else
		hw_diagnostics &= ~(1 << idx);
}

void set_bios_diagnostic(uint8_t code)
{
	bios_hc = code;
	if (code == CODE_PORT80_COMPLETE) {
		bios_complete = true;
		CPRINTS("BIOS COMPLETE");
	}

	if (code == CODE_DDR_FAIL)
		set_hw_diagnostic(DIAGNOSTICS_NO_DDR, true);
	if (code == CODE_NO_EDP && !standalone_mode)
		set_hw_diagnostic(DIAGNOSTICS_NO_EDP, true);
}