# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_include_directories(include)
zephyr_library_sources("src/common.c")
zephyr_library_sources_ifdef(CONFIG_NISSA_SUB_BOARD "src/sub_board.c")
zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ "src/board_power.c")

if(DEFINED CONFIG_BOARD_NIVVIKS)
	project(nivviks)
	zephyr_library_sources(
			"nivviks/src/led.c"
			"nivviks/src/form_factor.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "nivviks/src/fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "nivviks/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "nivviks/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_NEREID OR DEFINED CONFIG_BOARD_NEREID_CX)
	project(nereid)
	zephyr_library_sources(
			"src/led.c"
			"nereid/src/hdmi.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "nereid/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "nereid/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_NOKRIS)
	project(nokris)
	zephyr_library_sources(
			"nokris/src/led.c"
			"nokris/src/form_factor.c"
			"nokris/src/hdmi.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "nokris/src/fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "nokris/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "nokris/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_NAKTAL)
        project(naktal)
        zephyr_library_sources(
                        "naktal/src/led.c"
                        "naktal/src/hdmi.c"
        )
        zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "naktal/src/usbc.c")
        zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "naktal/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_CRAASK)
	zephyr_library_sources(
			"craask/src/fan.c"
			"craask/src/form_factor.c"
			"craask/src/keyboard.c"
			"craask/src/kb_backlight.c"
			"craask/src/led.c"
			"craask/src/thermal.c"
			"craask/src/touch_panel.c"
	)
	project(craask)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "craask/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "craask/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_PUJJO)
	project(pujjo)
	zephyr_library_sources(
			"pujjo/src/led.c"
			"pujjo/src/hdmi.c"
			"pujjo/src/form_factor.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "pujjo/src/fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "pujjo/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "pujjo/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_XIVU)
	project(xivu)
	zephyr_library_sources(
			"xivu/src/form_factor.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "xivu/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "xivu/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_JOXER)
	project(joxer)
	zephyr_library_sources(
			"joxer/src/board.c"
			"joxer/src/led.c"
			"joxer/src/keyboard.c"
			"joxer/src/form_factor.c"
			"joxer/src/sub_board.c"
			"joxer/src/thermal.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "joxer/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "joxer/src/charger.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "joxer/src/fan.c")
endif()
if(DEFINED CONFIG_BOARD_YAVIKS)
	project(yaviks)
	zephyr_library_sources(
			"yaviks/src/led.c"
			"yaviks/src/keyboard.c"
			"yaviks/src/board.c"
			"yaviks/src/sub_board.c"
			"yaviks/src/thermal.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "yaviks/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "yaviks/src/charger.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "yaviks/src/fan.c")
endif()
if(DEFINED CONFIG_BOARD_YAVILLA)
	project(yavilla)
	zephyr_library_sources(
			"yavilla/src/led.c"
			"yavilla/src/keyboard.c"
			"yavilla/src/board.c"
			"yavilla/src/thermal.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "yavilla/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "yavilla/src/charger.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "yavilla/src/fan.c")
endif()
if(DEFINED CONFIG_BOARD_YAVISTA)
	project(yavista)
	zephyr_library_sources(
			"yavista/src/led.c"
			"yavista/src/keyboard.c"
			"yavista/src/board.c"
			"yavista/src/sub_board.c"
			"yavista/src/thermal.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "yavista/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "yavista/src/charger.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "yavista/src/fan.c")
endif()
if(DEFINED CONFIG_BOARD_XIVUR)
	project(xivur)
	zephyr_library_sources(
			"xivur/src/led.c"
			"xivur/src/board.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "xivur/src/fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "xivur/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "xivur/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_ULDREN)
	project(uldren)
	zephyr_library_sources(
			"uldren/src/form_factor.c"
			"uldren/src/kb_backlight.c"
			"uldren/src/sub_board.c"
			"uldren/src/mp2964_update.c"
			"uldren/src/sensor.c"
			"uldren/src/usb_mux_config.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "uldren/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "uldren/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_GOTHRAX)
	project(gothrax)
	zephyr_library_sources(
			"gothrax/src/hdmi.c"
			"gothrax/src/usb_mux_config.c"
			"gothrax/src/form_factor.c"
			"gothrax/src/sub_board.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "gothrax/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "gothrax/src/charger.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "gothrax/src/fan.c")
endif()
if(DEFINED CONFIG_BOARD_CRAASKOV)
	project(craaskov)
	zephyr_include_directories("craaskov/include")
	zephyr_library_sources(
			"craaskov/src/keyboard.c"
			"craaskov/src/hdmi.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "craaskov/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "craaskov/src/charger.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
	"craaskov/src/keyboard.c"
	"craaskov/src/keyboard_customization.c"
	)
endif()
if(DEFINED CONFIG_BOARD_QUANDISO)
	project(quandiso)
	zephyr_library_sources(
			"quandiso/src/board.c"
			"quandiso/src/sub_board.c"
			"quandiso/src/keyboard.c"
			"quandiso/src/led.c"
			"quandiso/src/thermal.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "quandiso/src/charger.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "quandiso/src/fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "quandiso/src/usbc.c")
endif()
if(DEFINED CONFIG_BOARD_PIRRHA)
	project(pirrha)
	zephyr_include_directories("pirrha/include")
	zephyr_library_sources(
			"pirrha/src/led.c"
			"pirrha/src/board.c"
			"pirrha/src/battery.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "pirrha/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "pirrha/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_ANRAGGAR)
	project(anraggar)
	zephyr_include_directories("anraggar/include")
	zephyr_library_sources(
			"anraggar/src/board.c"
			"anraggar/src/sub_board.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "anraggar/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "anraggar/src/charger.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
			"anraggar/src/keyboard_customization.c"
	)
endif()
if(DEFINED CONFIG_BOARD_GLASSWAY)
	project(glassway)
	zephyr_library_sources(
			"glassway/src/sub_board.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "glassway/src/fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "glassway/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "glassway/src/charger.c")
endif()
