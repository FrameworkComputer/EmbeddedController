# SPDX-License-Identifier: Apache-2.0
zephyr_library()

zephyr_include_directories(include)
zephyr_include_directories_ifdef(
	CONFIG_HOST_SERVICE ../../include/host_service)
zephyr_library_sources_ifdef(CONFIG_HOST_SERVICE zephyr_host_service.c)
zephyr_library_sources_ifdef(CONFIG_SYS_MNG sys_mng.c)

add_subdirectory_ifdef(CONFIG_HECI heci)
