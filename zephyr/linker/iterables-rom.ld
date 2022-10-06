#ifdef CONFIG_PLATFORM_EC_HOSTCMD
ITERABLE_SECTION_ROM(host_command, 4)
#endif

#ifdef CONFIG_PLATFORM_EC_MKBP_EVENT
ITERABLE_SECTION_ROM(mkbp_event_source, 4)
#endif

#ifdef CONFIG_PLATFORM_EC_HOOKS
#include "../shim/include/hook_types.h"
#define HOOK_TYPE_SECTION(type) \
	ITERABLE_SECTION_ROM(zephyr_shim_hook_##type, 4)
FOR_EACH(HOOK_TYPE_SECTION, (
), HOOK_TYPES_LIST)
#endif