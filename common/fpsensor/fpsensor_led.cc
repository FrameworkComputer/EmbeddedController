/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/clock.h>

#include <fpsensor/fpsensor_led.h>
#include <limits>

LOG_MODULE_REGISTER(fp_led, LOG_LEVEL_INF);

namespace
{

#define LED_NODE DT_ALIAS(pwm_fp_led)
const struct device *const led_dev = DEVICE_DT_GET(DT_PARENT(LED_NODE));
constexpr uint32_t led_idx = DT_NODE_CHILD_IDX(LED_NODE);

void set_led_brightness(uint8_t percentage)
{
#ifdef CONFIG_PM_DEVICE
	if (percentage > 0) {
		pm_device_action_run(led_dev, PM_DEVICE_ACTION_RESUME);
	}
	led_set_brightness(led_dev, led_idx, percentage);
	if (percentage == 0) {
		pm_device_action_run(led_dev, PM_DEVICE_ACTION_SUSPEND);
	}
#else
	led_set_brightness(led_dev, led_idx, percentage);
#endif
}

int init()
{
	if (!device_is_ready(led_dev)) {
		LOG_ERR("LED device is not ready");
		return -ENODEV;
	}
	set_led_brightness(0);
	return 0;
}
SYS_INIT(init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

}

#ifdef CONFIG_CROS_EC_RW
namespace
{

consteval uint8_t perceivedToEmitted(float perceived, int gamma = 3)
{
	/* Constrain input */
	float p = (perceived < 0.0f) ?
			  0.0f :
			  ((perceived > 100.0f) ? 100.0f : perceived);
	/* Normalize perceived brightness to a 0.0 - 1.0 range */
	float normalized = p / 100.0f;
	/* Apply the gamma power curve */
	float curve_value = 1.0f;
	for (int i = 0; i < gamma; ++i) {
		curve_value *= normalized;
	}
	/* Scale to 100% and round to nearest integer */
	return static_cast<uint8_t>(curve_value * 100 + 0.5f);
}

constexpr uint8_t BRIGHTNESS_ENROLL =
	perceivedToEmitted(CONFIG_PLATFORM_EC_FP_LED_BRIGHTNESS_ENROLL);
constexpr uint8_t BRIGHTNESS_MATCH =
	perceivedToEmitted(CONFIG_PLATFORM_EC_FP_LED_BRIGHTNESS_MATCH);
constexpr uint8_t BRIGHTNESS_MATCH_OK =
	perceivedToEmitted(CONFIG_PLATFORM_EC_FP_LED_BRIGHTNESS_MATCH_OK);
constexpr uint8_t BRIGHTNESS_MATCH_NOK =
	perceivedToEmitted(CONFIG_PLATFORM_EC_FP_LED_BRIGHTNESS_MATCH_NOK);
constexpr uint8_t BRIGHTNESS_OFF = perceivedToEmitted(0);

enum class Mode {
	OFF,
	ENROLL,
	MATCH,
	MATCH_OK,
	MATCH_NOK,
};

Mode led_mode;
int64_t timer_scheduled_at_ticks = 0;

K_SEM_DEFINE(led_lock, 1, 1);

bool ignore_off(Mode mode)
{
	return mode == Mode::OFF || mode == Mode::MATCH_OK ||
	       mode == Mode::MATCH_NOK;
}

void led_update_handler(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(led_update_dwork, led_update_handler);

void led_update_handler(struct k_work *work)
{
	k_sem_take(&led_lock, K_FOREVER);
	bool is_timer_expired = k_uptime_ticks() >= timer_scheduled_at_ticks;
	if (is_timer_expired) {
		led_mode = Mode::OFF;
		set_led_brightness(BRIGHTNESS_OFF);
	}
	k_sem_give(&led_lock);
}

void set_led(uint8_t brightness)
{
	/* Always called with led_lock taken */
	int ret = k_work_cancel_delayable(&led_update_dwork);
	if (ret < 0) {
		LOG_ERR("Failed to cancel LED update work: %d", ret);
	}
	/* In case the work was not cancelled, set the timer to max to prevent
	 * the handler from turning the LED off */
	timer_scheduled_at_ticks = std::numeric_limits<int64_t>::max();
	set_led_brightness(brightness);
}

void set_led_with_timeout(uint8_t brightness, int32_t delay_ms)
{
	/* Always called with led_lock taken */
	set_led_brightness(brightness);
	timer_scheduled_at_ticks =
		k_uptime_ticks() + k_ms_to_ticks_floor64(delay_ms);
	int ret = k_work_reschedule(&led_update_dwork, K_MSEC(delay_ms));
	if (ret < 0) {
		LOG_ERR("Failed to reschedule LED update work: %d", ret);
		/* Fallback to turning LED off */
		set_led_brightness(BRIGHTNESS_OFF);
	}
}

void enroll()
{
	k_sem_take(&led_lock, K_FOREVER);
	if (led_mode == Mode::ENROLL) {
		k_sem_give(&led_lock);
		return;
	}
	led_mode = Mode::ENROLL;
	set_led(BRIGHTNESS_ENROLL);
	k_sem_give(&led_lock);

	LOG_INF("Fingerprint led enroll.");
}

void match()
{
	k_sem_take(&led_lock, K_FOREVER);
	if (led_mode == Mode::MATCH) {
		k_sem_give(&led_lock);
		return;
	}
	led_mode = Mode::MATCH;
	set_led_with_timeout(BRIGHTNESS_MATCH,
			     CONFIG_PLATFORM_EC_FP_LED_DURATION_MATCH_MS);
	k_sem_give(&led_lock);

	LOG_INF("Fingerprint led match.");
}

void match_ok()
{
	k_sem_take(&led_lock, K_FOREVER);
	led_mode = Mode::MATCH_OK;
	set_led_with_timeout(BRIGHTNESS_MATCH_OK,
			     CONFIG_PLATFORM_EC_FP_LED_DURATION_MATCH_OK_MS);
	k_sem_give(&led_lock);

	LOG_INF("Fingerprint led match OK.");
}

void match_nok()
{
	k_sem_take(&led_lock, K_FOREVER);
	led_mode = Mode::MATCH_NOK;
	set_led_with_timeout(BRIGHTNESS_MATCH_NOK,
			     CONFIG_PLATFORM_EC_FP_LED_DURATION_MATCH_NOK_MS);
	k_sem_give(&led_lock);

	LOG_INF("Fingerprint led match NOK.");
}

void off()
{
	k_sem_take(&led_lock, K_FOREVER);
	if (ignore_off(led_mode)) {
		k_sem_give(&led_lock);
		return;
	}
	set_led(BRIGHTNESS_OFF);
	led_mode = Mode::OFF;
	k_sem_give(&led_lock);

	LOG_INF("Fingerprint led off.");
}

} /* namespace */

namespace fp_led
{

void update_mode(uint32_t sensor_mode)
{
	if (sensor_mode & FP_MODE_ENROLL_SESSION)
		enroll();
	else if (sensor_mode & FP_MODE_MATCH)
		match();
	else
		off();
}

void update_match(bool success)
{
	if (success)
		match_ok();
	else
		match_nok();
}

} /* namespace fp_led */
#endif /* CONFIG_CROS_EC_RW */