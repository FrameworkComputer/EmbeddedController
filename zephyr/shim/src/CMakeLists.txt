# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources(console.c)
zephyr_library_sources(crc.c)
zephyr_library_sources(gpio.c)
zephyr_library_sources(gpio_int.c)

add_subdirectory("buttons")
add_subdirectory("cbi")
add_subdirectory("led_driver")

if (DEFINED CONFIG_SOC_FAMILY_INTEL_ISH)
  zephyr_library_sources(ish_system.c)
elseif (DEFINED CONFIG_ARCH_POSIX)
  zephyr_library_sources(ztest_system.c)
else()
  zephyr_library_sources(system.c)
endif()

zephyr_library_sources_ifdef(no_libgcc                  libgcc_${ARCH}.S)

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_ANX7452 anx7452_usb_mux.c)
zephyr_library_sources_ifdef(CONFIG_NAMED_ADC_CHANNELS      adc.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BATTERY
                                                            battery.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BC12_DETECT_RT9490
                                                            bc12_rt9490.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CEC         cec.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CEC_BITBANG
                                                            cec_bitbang.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER_RT9490
                                                            chg_rt9490.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER     charger.c)
zephyr_library_sources_ifdef(CONFIG_X86_NON_DSX_PWRSEQ      chipset_api.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOST_INTERFACE_ESPI
                                                            espi.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN         fan.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FLASH_CROS  flash.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_GPIO_ID
							    gpio_id.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOOKS       hooks.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD     host_command.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE
                                                            console_buffer.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_IOEX_CROS_DRV
                                                            ioex.c
                                                            ioex_drv.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_KB8010 kb8010_usb_mux.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CROS_EC_RAW_KB
                                                            keyboard_raw.c)
zephyr_library_sources_ifdef(CONFIG_CROS_EC_KEYBOARD_INPUT  keyboard_input.c)
zephyr_library_sources_ifdef(CONFIG_CROS_EC_KEYSCAN         keyscan.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LOG_BACKEND_CONSOLE_BUFFER
                             log_backend_console_buffer.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_MKBP_EVENT  mkbp_event.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_MOTIONSENSE
                                                            motionsense_sensors.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_PANIC       panic.c)
zephyr_library_sources_ifdef(CONFIG_CROS_EC_POWER_SIGNAL_LIST
							    power.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_PWM_HC      pwm_hc.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_LED_PWM     pwm_led.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_RANDOM      random.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_RTC         rtc.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_SWITCHCAP_GPIO
							    switchcap_gpio.c)
zephyr_library_sources_ifdef(CONFIG_CROS_EC_SWITCHCAP_LN9310
							    switchcap_ln9310.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_TEMP_SENSOR temp_sensors.c
                                                            thermal.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_TIMER       hwtimer.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_I2C         i2c.c)
zephyr_library_sources_ifdef(CONFIG_SHIMMED_TASKS           tasks.c)
zephyr_library_sources_ifdef(CONFIG_WATCHDOG                watchdog.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_CHARGER bc12.c)
zephyr_library_sources_ifdef(CONFIG_USB_BC12                bc12_upstream.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_BC12_DETECT_PI3USB9201
	                                                     bc12_pi3usb9201.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_PPC    ppc.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_PD_TCPM_NCT38XX
                                                            tcpc_nct38xx.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC        tcpc.c)

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBA        usba.c)
zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ_HOST_SLEEP    power_host_sleep_api.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USB_MUX     usb_muxes.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_INTEL_BB
                                                            bb_retimer_usb_mux.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC_RETIMER_INTEL_HB
                                                            bb_retimer_usb_mux.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_SYSTEM_SAFE_MODE system_safe_mode.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_SHA256_HW_ZEPHYR
	                                                    sha256_hw.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOST_COMMAND_MEMORY_DUMP
	                                                    register_memory_dump.c)
if (DEFINED CONFIG_HOST_SERVICE)
zephyr_library_sources_ifdef(CONFIG_HECI
                                                            host_command_heci.c)
endif()

if(CONFIG_PIGWEED_LOG_TOKENIZED_LIB)
  pw_add_library(platform_ec_log.tokenized_handler STATIC
    HEADERS
      ${PLATFORM_EC}/zephyr/shim/include/zephyr_console_shim.h
    SOURCES
      pw_log_zephyr_tokenized.cc
    PUBLIC_INCLUDES
      ${PLATFORM_EC}/zephyr/shim/include
    PUBLIC_DEPS
      pw_log_zephyr.tokenized_lib
  )

  pw_zephyrize_libraries_ifdef(CONFIG_PIGWEED_LOG_TOKENIZED_LIB
                              platform_ec_log.tokenized_handler
  )

  include("${ZEPHYR_PIGWEED_MODULE_DIR}/pw_log_tokenized/backend.cmake")
  pw_set_backend(pw_log_tokenized.handler platform_ec_log.tokenized_handler)

endif()
