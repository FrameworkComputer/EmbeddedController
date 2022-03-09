# SPDX-License-Identifier: Apache-2.0

zephyr_library_include_directories(include)
zephyr_library_sources(
	ap_events.c
	)
zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ
	ap_pwrseq_chipset_api.c
	power_signals.c
	signal_gpio.c
	signal_vw.c
	signal_adc.c
	)
zephyr_library_sources_ifdef(CONFIG_X86_NON_DSX_PWRSEQ
	x86_non_dsx_common_pwrseq_sm_handler.c)
zephyr_library_sources_ifdef(CONFIG_X86_NON_DSX_PWRSEQ_CONSOLE
	x86_non_dsx_common_pwrseq_console.c)
zephyr_library_sources_ifdef(CONFIG_X86_NON_DSX_PWRSEQ_ADL
	x86_non_dsx_adlp_pwrseq_sm.c)
