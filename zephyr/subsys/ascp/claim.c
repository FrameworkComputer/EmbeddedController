/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "host_command.h"
#include "system.h"
#include "util.h"

#include <ascp/ascp.h>

static enum ec_status fp_command_ascp_claim(struct host_cmd_handler_args *args)
{
	if (sizeof(struct ec_response_fp_ascp_claim) > args->response_max) {
		return EC_RES_RESPONSE_TOO_BIG;
	}

	struct ec_response_fp_ascp_claim *r = args->response;
	args->response_size = sizeof(struct ec_response_fp_ascp_claim);

	int ret = ascp_get_claim(r);

	if (ret < 0) {
		return EC_RES_ERROR;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_ASCP_CLAIM, fp_command_ascp_claim,
		     EC_VER_MASK(0));

static void print_bytes(const char *label, const uint8_t *bytes, size_t len)
{
	printk("%s: ", label);
	for (size_t i = 0; i < len; ++i) {
		printk("%02x", bytes[i]);
	}
	printk("\n");
}

static int command_fp_command_ascp_claim(const struct shell *sh, int argc,
					 const char **argv)
{
	struct ec_response_fp_ascp_claim res;
	int ret = ascp_get_claim(&res);
	if (ret != EC_SUCCESS) {
		shell_error(sh, "Failure getting ASCP claim.");
		return ret;
	}
	print_bytes("pk_m", res.pk_m, sizeof(res.pk_m));
	print_bytes("s_goog", res.s_goog, sizeof(res.s_goog));
	print_bytes("pk_d", res.pk_d, sizeof(res.pk_d));
	print_bytes("s_m", res.s_m, sizeof(res.s_m));
	print_bytes("pk_f", res.pk_f, sizeof(res.pk_f));
	print_bytes("h_f", res.h_f, sizeof(res.h_f));
	print_bytes("s_d", res.s_d, sizeof(res.s_d));

	return EC_SUCCESS;
}
SHELL_CMD_REGISTER(fpascp, NULL, "ASCP claim data",
		   command_fp_command_ascp_claim);
