/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Honeybuns family-specific configuration */
#include "console.h"
#include "cros_board_info.h"
#include "driver/mp4245.h"
#include "driver/tcpm/tcpm.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

#define POWER_BUTTON_SHORT_USEC (300 * MSEC)
#define POWER_BUTTON_LONG_USEC (5000 * MSEC)
#define POWER_BUTTON_DEBOUNCE_USEC (30)

#define BUTTON_EVT_CHANGE BIT(0)
#define BUTTON_EVT_INFO BIT(1)

enum power { POWER_OFF, POWER_ON };

enum button {
	BUTTON_RELEASE,
	BUTTON_PRESS,
	BUTTON_PRESS_POWER_ON,
	BUTTON_PRESS_SHORT,
	BUTTON_PRESS_LONG,
};

#define LED_ON_OFF_BIT BIT(0)
#define LED_COLOR_BIT BIT(2)
#define LED_FLASH_SEQ_LENGTH 8

enum led_color {
	GREEN,
	YELLOW,
	OFF,
};

static enum power dock_state;
#ifdef SECTION_IS_RW
static int button_level;
static int button_level_pending;
static int dock_mf;
static int led_count;
#endif

/******************************************************************************/

__maybe_unused static void board_power_sequence(int enable)
{
	int i;

	if (enable) {
		for (i = 0; i < board_power_seq_count; i++) {
			gpio_set_level(board_power_seq[i].signal,
				       board_power_seq[i].level);
			CPRINTS("power seq: rail = %d", i);
			if (board_power_seq[i].delay_ms)
				crec_msleep(board_power_seq[i].delay_ms);
		}
	} else {
		for (i = board_power_seq_count - 1; i >= 0; i--) {
			gpio_set_level(board_power_seq[i].signal,
				       !board_power_seq[i].level);
			CPRINTS("sequence[%d]: level = %d", i,
				!board_power_seq[i].level);
		}
	}

	dock_state = enable;
	CPRINTS("board: Power rails %s", dock_state ? "on" : "off");
}

/******************************************************************************/
/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "i2c1",
	  .port = I2C_PORT_I2C1,
	  .kbps = 400,
	  .scl = GPIO_EC_I2C1_SCL,
	  .sda = GPIO_EC_I2C1_SDA },
	{ .name = "i2c3",
	  .port = I2C_PORT_I2C3,
	  .kbps = 400,
	  .scl = GPIO_EC_I2C3_SCL,
	  .sda = GPIO_EC_I2C3_SDA },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

#ifdef SECTION_IS_RW
static void baseboard_set_led(enum led_color color)
{
	/*
	 * TODO(b/164157329): The power button feature should be connected to a
	 * 2 color LED which is part of the button. Currently, the power button
	 * LED is a single color LED which is controlled by on the of the power
	 * rails. Using the status LED now to demonstrate the LED behavior
	 * associated with a power button press.
	 */
	CPRINTS("led: color = %d", color);

	/* Not all boards may have LEDs under EC control */
#if defined(GPIO_PWR_BUTTON_RED) && defined(GPIO_PWR_BUTTON_GREEN)
	if (color == OFF) {
		gpio_set_level(GPIO_PWR_BUTTON_RED, 1);
		gpio_set_level(GPIO_PWR_BUTTON_GREEN, 1);
	} else if (color == GREEN) {
		gpio_set_level(GPIO_PWR_BUTTON_RED, 1);
		gpio_set_level(GPIO_PWR_BUTTON_GREEN, 0);
	} else if (color == YELLOW) {
		gpio_set_level(GPIO_PWR_BUTTON_RED, 0);
		gpio_set_level(GPIO_PWR_BUTTON_GREEN, 0);
	}
#endif
}

static void baseboard_led_callback(void);
DECLARE_DEFERRED(baseboard_led_callback);

static void baseboard_led_callback(void)
{
	/*
	 * Flash LED on transition using a simple 3 bit counter. Bit 0 controls
	 * LED on/off and bit 2 controls which color to set during the on phase.
	 */
	int color = led_count & LED_COLOR_BIT ? dock_mf : dock_mf ^ 1;

	/*
	 * TODO(b/164157329): This function implements a simple flashing
	 * transition when the MF preference bit is changed via a long power
	 * button press sequence. This might need to move to the board function
	 * if not required/desired on all variants.
	 */

	if (led_count & LED_ON_OFF_BIT)
		baseboard_set_led(color);
	else
		baseboard_set_led(OFF);

	/* Flash sequence is 8 steps */
	if (++led_count < LED_FLASH_SEQ_LENGTH)
		hook_call_deferred(&baseboard_led_callback_data, 150 * MSEC);
}

static void baseboard_change_mf_led(void)
{
	led_count = 0;
	baseboard_led_callback();
}

void baseboard_set_mst_lane_control(int mf)
{
	/*
	 * The parameter mf reflects the desired lane control value. If the
	 * current value does not match the desired, then the MST hub must first
	 * be put into reset, so the MST hub will latch in the correct value
	 * when it's taken out of reset.
	 */
	if (mf != gpio_get_level(GPIO_MST_HUB_LANE_SWITCH)) {
		/* put MST into reset */
		gpio_set_level(GPIO_MST_RST_L, 0);
		crec_msleep(1);
		gpio_set_level(GPIO_MST_HUB_LANE_SWITCH, mf);
		CPRINTS("MST: lane control = %s", mf ? "high" : "low");
		crec_msleep(1);
		/* lane control is set, take MST out of reset */
		gpio_set_level(GPIO_MST_RST_L, 1);
	}
}

static void baseboard_enable_mp4245(void)
{
	int mv;
	int ma;

	mp4245_set_voltage_out(5000);
	mp4245_votlage_out_enable(1);
	crec_msleep(MP4245_VOUT_5V_DELAY_MS);
	mp3245_get_vbus(&mv, &ma);
	CPRINTS("mp4245: vout @ %d mV enabled", mv);
}

#endif /* SECTION_IS_RW */

static void baseboard_init(void)
{
#ifdef SECTION_IS_RW
	uint32_t fw_config;
#endif

	/* Turn on power rails */
	board_power_sequence(1);
	CPRINTS("board: Power rails enabled");

#ifdef SECTION_IS_RW
	/* Force TC state machine to start in TC_ERROR_RECOVERY */
	system_clear_reset_flags(EC_RESET_FLAG_POWER_ON);
	/* Make certain SN5S330 PPC does full initialization */
	system_set_reset_flags(EC_RESET_FLAG_EFS);

	/*
	 * Dock multi function (mf) preference is stored in bit 0 of fw_config
	 * field of the CBI. If this value is programmed, then make sure the
	 * MST_LANE_CONTROL gpio matches the mf bit.
	 */
	if (!cbi_get_fw_config(&fw_config)) {
		dock_mf = CBI_FW_MF_PREFERENCE(fw_config);
		baseboard_set_mst_lane_control(dock_mf);
	} else {
		dock_mf = dock_get_mf_preference();
		cbi_set_fw_config(dock_mf);
		CPRINTS("cbi: setting default result = %s",
			cbi_get_fw_config(&fw_config) ? "pass" : "fail");
	}

#ifdef GPIO_USBC_UF_ATTACHED_SRC
	/* Configure UF usbc ppc and check usbc state */
	baseboard_config_usbc_usb3_ppc();
#endif /* GPIO_USBC_UF_ATTACHED_SRC */

	/* Enable power button interrupt */
	gpio_enable_interrupt(GPIO_PWR_BTN);
	/* Set dock mf preference LED */
	baseboard_set_led(dock_mf);
	/* Setup VBUS to default value */
	baseboard_enable_mp4245();

#else
	/* Set up host port usbc to present Rd on CC lines */
	if (baseboard_usbc_init(USB_PD_PORT_HOST))
		CPRINTS("usbc: Failed to set up sink path");
	else
		CPRINTS("usbc: sink path configure success!");
#endif /* SECTION_IS_RW */
}
/*
 * Power sequencing must run before any other chip init is attempted, so run
 * power sequencing as soon as I2C bus is initialized.
 */
DECLARE_HOOK(HOOK_INIT, baseboard_init, HOOK_PRIO_INIT_I2C + 1);

#ifdef SECTION_IS_RW
static void baseboard_power_on(void)
{
	int port_max = board_get_usb_pd_port_count();
	int port;

	CPRINTS("pwrbtn: power on: mf = %d", dock_mf);
	/* Adjust system flags to full PPC init occurs */
	system_clear_reset_flags(EC_RESET_FLAG_POWER_ON);
	system_set_reset_flags(EC_RESET_FLAG_EFS);
	/* Enable power rails and release reset signals */
	board_power_sequence(1);
	/* Set VBUS to 5V and enable output from mp4245 */
	baseboard_enable_mp4245();
	/* Set dock mf preference LED */
	baseboard_set_led(dock_mf);
	/*
	 * Lane control (realtek MST) must be set prior to releasing MST
	 * reset.
	 */
	baseboard_set_mst_lane_control(dock_mf);
	/*
	 * When the power to the PPC is turned off, then back on, the PPC will
	 * default into dead battery mode. Dead battery resistors are disabled
	 * as part of the full PPC intializaiton sequence. This is required to
	 * force a detach event with port parter which can be attached as usbc
	 * source when honeybuns power rails are off.
	 */
	for (port = 0; port < port_max; port++) {
		ppc_init(port);
		crec_msleep(1000);
		/* Inform TC state machine that it can resume */
		pd_set_suspend(port, 0);
	}
	/* Enable usbc interrupts */
	board_enable_usbc_interrupts();

#ifdef GPIO_USBC_UF_ATTACHED_SRC
	baseboard_config_usbc_usb3_ppc();
#endif
}

static void baseboard_power_off(void)
{
	int port_max = board_get_usb_pd_port_count();
	int port;

	CPRINTS("pwrbtn: power off");
	/* Put ports in TC suspend state */
	for (port = 0; port < port_max; port++)
		pd_set_suspend(port, 1);

	/* Disable ucpd peripheral (prevents interrupts) */
	tcpm_release(USB_PD_PORT_HOST);
	/* Disable PPC/TCPC interrupts */
	board_disable_usbc_interrupts();

#ifdef GPIO_USBC_UF_ATTACHED_SRC
	/* Disable PPC interrupts for PS8803 managed port */
	baseboard_usbc_usb3_enable_interrupts(0);
#endif
	/* Set dock power button/MF preference LED */
	baseboard_set_led(OFF);
	/* Go into power off state */
	board_power_sequence(0);
}

static void baseboard_toggle_mf(void)
{
	uint32_t fw_config;

	if (!cbi_get_fw_config(&fw_config)) {
		/* Update the user MF preference stored in CBI */
		fw_config ^= CBI_FW_MF_MASK;
		cbi_set_fw_config(fw_config);
		/* Update variable used to track user MF preference */
		dock_mf = CBI_FW_MF_PREFERENCE(fw_config);
		/* Flash led for visual indication of user MF change */
		baseboard_change_mf_led();

		/*
		 * Suspend, then release host port to force new MF setting to
		 * take effect.
		 */
		pd_set_suspend(USB_PD_PORT_HOST, 1);
		crec_msleep(250);
		pd_set_suspend(USB_PD_PORT_HOST, 0);
	}
}

/*
 * Main task entry point for UCPD task
 */
void power_button_task(void *u)
{
	int timer_us = POWER_BUTTON_DEBOUNCE_USEC * 4;
	enum button state = BUTTON_RELEASE;
	uint32_t evt;

	/*
	 * Capture current button level in case it's being pressed when the dock
	 * is powered on. Note timer_us is initialized for debounce time to
	 * double check.
	 */
	button_level = gpio_get_level(GPIO_PWR_BTN);

	while (1) {
		evt = task_wait_event(timer_us);
		timer_us = -1;

		if (evt == BUTTON_EVT_INFO) {
			/* Only used for console command for debug */
			CPRINTS("pwrbtn: pwr = %d, state = %d, level = %d",
				dock_state, state, button_level);
			continue;
		}

		switch (state) {
		case BUTTON_RELEASE:
			/*
			 * Default wait state: Only need to check if the button
			 * is pressed and start the short press timer.
			 */
			if (evt & BUTTON_EVT_CHANGE &&
			    button_level == BUTTON_PRESSED_LEVEL) {
				state = BUTTON_PRESS;
				timer_us = (POWER_BUTTON_SHORT_USEC -
					    POWER_BUTTON_DEBOUNCE_USEC);
			}
			break;
		case BUTTON_PRESS:
			/*
			 * Validate short press by ensuring that button is still
			 * pressed after short press timer expires.
			 */
			if (evt & BUTTON_EVT_CHANGE &&
			    button_level == BUTTON_RELEASED_LEVEL) {
				state = BUTTON_RELEASE;
			} else {
				/* Start long press timer */
				timer_us = POWER_BUTTON_LONG_USEC -
					   POWER_BUTTON_SHORT_USEC;
				/*
				 * If dock is currently off, then change to the
				 * power on state. If dock is already on, then
				 * advance to short press state.
				 */
				if (dock_state == POWER_OFF) {
					baseboard_power_on();
					state = BUTTON_PRESS_POWER_ON;
				} else {
					state = BUTTON_PRESS_SHORT;
				}
			}
			break;
		case BUTTON_PRESS_POWER_ON:
			/*
			 * Short press recognized and dock was just powered
			 * on. If button is no longer pressed, then just return
			 * to the default state. Else, button is still pressed
			 * after long press timer has expired.
			 */
			if (evt & BUTTON_EVT_CHANGE &&
			    button_level == BUTTON_RELEASED_LEVEL) {
				state = BUTTON_RELEASE;
			} else {
				state = BUTTON_PRESS_LONG;
				baseboard_toggle_mf();
			}
			break;
		case BUTTON_PRESS_SHORT:
			/*
			 * Short press was recognized and dock power state was
			 * already on. If button is now released, then turn dock
			 * off.
			 */
			if (evt & BUTTON_EVT_CHANGE &&
			    button_level == BUTTON_RELEASED_LEVEL) {
				state = BUTTON_RELEASE;
				baseboard_power_off();
			} else {
				state = BUTTON_PRESS_LONG;
				baseboard_toggle_mf();
			}
			break;
		case BUTTON_PRESS_LONG:
			if (evt & BUTTON_EVT_CHANGE &&
			    button_level == BUTTON_RELEASED_LEVEL) {
				state = BUTTON_RELEASE;
			}
			break;
		}
	}
}

static void baseboard_power_button_debounce(void)
{
	int level = gpio_get_level(GPIO_PWR_BTN);

	/* Sanity check, level should be same after debounce interval */
	if (level != button_level_pending)
		return;

	button_level = level;
	task_set_event(TASK_ID_POWER_BUTTON, BUTTON_EVT_CHANGE);
}
DECLARE_DEFERRED(baseboard_power_button_debounce);

void baseboard_power_button_evt(int level)
{
	button_level_pending = level;

	hook_call_deferred(&baseboard_power_button_debounce_data,
			   POWER_BUTTON_DEBOUNCE_USEC);
}

static int command_pwr_btn(int argc, const char **argv)
{
	if (argc == 1) {
		task_set_event(TASK_ID_POWER_BUTTON, BUTTON_EVT_INFO);
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "on")) {
		baseboard_power_on();
	} else if (!strcasecmp(argv[1], "off")) {
		baseboard_power_off();
	} else if (!strcasecmp(argv[1], "mf")) {
		baseboard_toggle_mf();
	} else {
		return EC_ERROR_PARAM1;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pwr_btn, command_pwr_btn, "<on|off|mf>",
			"Simulate Power Button Press");

#endif
