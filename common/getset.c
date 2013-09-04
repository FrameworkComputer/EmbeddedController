/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Generic get/set value stuff. */

#include "common.h"
#include "console.h"
#include "getset.h"
#include "host_command.h"
#include "util.h"

/* Declare and initialize the values */
#define GSV_ITEM(n, v) v,
#include "getset_value_list.h"
uint32_t gsv[] = {
	GSV_LIST
};
#undef GSV_ITEM

BUILD_ASSERT(ARRAY_SIZE(gsv) == NUM_GSV_PARAMS);

static enum ec_status get_set_value(struct ec_cmd_get_set_value *ptr)
{
	unsigned int index = ptr->flags & EC_GSV_PARAM_MASK;
	if (index >= NUM_GSV_PARAMS)
		return EC_RES_INVALID_PARAM;

	/* Handle flags correctly - we may add new ones some day */
	if (ptr->flags & EC_GSV_SET)
		gsv[index] = ptr->value;

	ptr->value = gsv[index];
	return EC_RES_SUCCESS;
}

static int host_command_get_set_value(struct host_cmd_handler_args *args)
{
	const struct ec_cmd_get_set_value *p = args->params;
	struct ec_cmd_get_set_value *r = args->response;
	args->response_size = sizeof(*r);

	if (p != r)
		memcpy(r, p, sizeof(*p));

	return get_set_value(r);
}
DECLARE_HOST_COMMAND(EC_CMD_GET_SET_VALUE,
		     host_command_get_set_value,
		     EC_VER_MASK(0));


#ifdef CONFIG_CMD_GSV

#define STRINGIFY0(name)  #name
#define STRINGIFY(name)  STRINGIFY0(name)
#define GSV_ITEM(n, v) STRINGIFY(n),
#include "getset_value_list.h"
static const char const *gsv_name[] = {
	GSV_LIST
};
#undef GSV_ITEM

static int console_command_get_set_value(int argc, char **argv)
{
	unsigned int i;
	struct ec_cmd_get_set_value s = {0, 0};
	char *e;
	int ret;

	if (argc < 2) {
		for (i = 0; i < NUM_GSV_PARAMS; i++)
			ccprintf("%s = 0x%08x\n", gsv_name[i], gsv[i]);
		return EC_SUCCESS;
	}

	for (i = 0; i < NUM_GSV_PARAMS; i++)
		if (!strcasecmp(gsv_name[i], argv[1]))
			break;

	if (i >= NUM_GSV_PARAMS) {
		ccprintf("Can't find param \"%s\"\n", argv[1]);
		return EC_ERROR_UNKNOWN;
	}
	s.flags = i;

	if (argc > 2) {
		s.flags |= EC_GSV_SET;
		s.value = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_INVAL;
	}

	ret = get_set_value(&s);

	if (ret == EC_RES_SUCCESS)
		ccprintf("%s = 0x%08x\n", argv[1], s.value);

	return ret;
}
DECLARE_CONSOLE_COMMAND(gsv, console_command_get_set_value,
			"[name [value]]",
			"get/set the value of named parameters",
			NULL);

#endif	/* CONFIG_CMD_GSV */
