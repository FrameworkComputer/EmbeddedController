/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include "comm-host.h"
#include "ec_flash.h"

#define STR0(name)  #name
#define STR(name)   STR0(name)

static const char * const part_name[] = {"unknown", "RO", "RW"};

enum ec_current_image get_version(enum ec_current_image *version_ptr)
{
	struct ec_response_get_version r;
	char *build_info = (char *)ec_inbuf;
	int res;

	res = ec_command(EC_CMD_GET_VERSION, 0, NULL, 0, &r, sizeof(r));
	if (res < 0)
		return res;
	res = ec_command(EC_CMD_GET_BUILD_INFO, 0, NULL, 0,
			 ec_inbuf, ec_max_insize);
	if (res < 0)
		return res;

	/* Ensure versions are null-terminated before we print them */
	r.version_string_ro[sizeof(r.version_string_ro) - 1] = '\0';
	r.version_string_rw[sizeof(r.version_string_rw) - 1] = '\0';
	build_info[ec_max_insize - 1] = '\0';

	/* Print versions */
	printf("RO version:    %s\n", r.version_string_ro);
	printf("RW version:    %s\n", r.version_string_rw);
	printf("Firmware copy: %s\n",
	       (r.current_image < sizeof(part_name)/sizeof(part_name[0]) ?
		part_name[r.current_image] : "?"));
	printf("Build info:    %s\n", build_info);

	if (version_ptr)
		*version_ptr = r.current_image;

	return 0;
}

int flash_partition(enum ec_current_image part, const uint8_t *payload,
		    uint32_t offset, uint32_t size)
{
	struct ec_params_reboot_ec rst_req;
	int res;
	enum ec_current_image current = EC_IMAGE_UNKNOWN;

	res = get_version(&current);
	if (res < 0) {
		fprintf(stderr, "Get version failed : %d\n", res);
		return -1;
	}

	if (current == part) {
		rst_req.cmd = part == EC_IMAGE_RO ?
			      EC_REBOOT_JUMP_RW : EC_REBOOT_JUMP_RO;
		ec_command(EC_CMD_REBOOT_EC, 0, &rst_req, sizeof(rst_req),
			   NULL, 0);
		/* wait EC reboot */
		usleep(1000000);
	}

	printf("Erasing partition %s : 0x%x bytes at 0x%08x\n",
	       part_name[part], size, offset);
	res = ec_flash_erase(offset, size);
	if (res < 0) {
		fprintf(stderr, "Erase failed : %d\n", res);
		return -1;
	}

	printf("Writing partition %s : 0x%x bytes at 0x%08x\n",
	       part_name[part], size, offset);
	res = ec_flash_write(payload, offset, size);
	if (res < 0) {
		fprintf(stderr, "Write failed : %d\n", res);
		return -1;
	}

	printf("Verifying partition %s : 0x%x bytes at 0x%08x\n",
	       part_name[part], size, offset);
	res = ec_flash_verify(payload, offset, size);
	if (res < 0) {
		fprintf(stderr, "Verify failed : %d\n", res);
		return -1;
	}

	printf("Done.\n");

	return get_version(NULL);
}

/* black magic to include the EC firmware binary as a payload inside
 * the flashing program without relying on external tools such as objcopy or a
 * binary to header converter. */
__asm__ (".section .payload, \"ax\"\n"
	 "_payload_start:\n"
	 ".incbin \""STR(OUTDIR)"/"STR(PROJECT)".bin\"\n");

int main(int argc, char *argv[])
{
	extern uint8_t data[] asm("_payload_start");

	if (comm_init() < 0)
		return -3;

	flash_partition(EC_IMAGE_RW, data + CONFIG_FW_RW_OFF,
			CONFIG_FW_RW_OFF, CONFIG_FW_RW_SIZE);
	flash_partition(EC_IMAGE_RO, data + CONFIG_FW_RO_OFF,
			CONFIG_FW_RO_OFF, CONFIG_FW_RO_SIZE);

	return 0;
}
