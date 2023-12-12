# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(heci_core.c)
zephyr_library_sources(heci_intf.c)
zephyr_library_sources(heci_system_state.c)
zephyr_library_sources_ifdef(CONFIG_HECI_ROUTER heci_router.c)
zephyr_library_sources_ifdef(CONFIG_DUMMY_SMHI smhi_client.c)
zephyr_library_sources_ifdef(CONFIG_HECI_INTF_IPC intfs/ipc_intf.c)
zephyr_library_sources_ifdef(CONFIG_HECI_INTF_IPM intfs/ipm_intf.c)
zephyr_linker_sources(RAM_SECTIONS heci-sections.ld)
